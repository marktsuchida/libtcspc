# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

"""
Build Python extension modules on demand, from run-time generated C++ code.
"""

# This is a little similar to projects such as cppimport, with the important
# difference that here we are interested in building dynamically generated
# code, as opposed to C++ source files in the project tree.

# At least for now, we use the Meson build system to handle the build setup.

__all__ = ["ExtensionBuilder", "ExtensionImporter"]

import asyncio
import atexit
import functools
import importlib.util
import json
import os
import platform
import shutil
import tempfile
import textwrap
import time
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any, Self

from . import _async

Module = Any


def _quote_meson_str(s: str) -> str:
    return f"'{s.replace("'", "\\'")}'"


@functools.cache
def _meson_exe():
    meson_exe = shutil.which("meson")
    if not meson_exe:
        raise RuntimeError("Cannot find the meson executable")
    return Path(meson_exe)


# Run a subprocess, allowing clean cancellation (see unit test).
async def _run_subprocess_nostdin(
    program: Path,
    *args: str,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> tuple[int, str, str]:
    kwargs: dict[str, Any] = dict(
        stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    if cwd is not None:
        kwargs["cwd"] = cwd
    if env is not None:
        kwargs["env"] = env

    proc = await asyncio.create_subprocess_exec(program, *args, **kwargs)
    try:
        stdout, stderr = await proc.communicate()
    except asyncio.CancelledError:
        proc.terminate()
        await proc.wait()
        raise
    assert proc.returncode is not None
    return proc.returncode, stdout.decode(), stderr.decode()


async def _run_meson(
    *args: str, cwd: Path, env: Mapping[str, str]
) -> tuple[int, str, str]:
    return await _run_subprocess_nostdin(_meson_exe(), *args, cwd=cwd, env=env)


class ExtensionBuilder:
    _DISCARDED, _CREATED, _CODE_WRITTEN, _CONFIGURED, _BUILT = range(5)

    def __init__(
        self,
        *,
        cxx: str | None = None,
        cpp_std: str | None = None,
        include_dirs: Sequence[str] = (),
        code_text: str | None = None,
        tempdir: Path | None = None,
    ) -> None:
        self._proj_dir = tempfile.TemporaryDirectory(
            prefix="odext-", dir=tempdir
        )
        self._proj_path = Path(self._proj_dir.name)

        self._config_env = {}
        self._config_env["PATH"] = os.environ["PATH"]
        if cxx:
            self._config_env["CXX"] = cxx

        self._write_project(cpp_std=cpp_std, include_dirs=include_dirs)
        self._stage = self._CREATED

        if code_text is not None:
            self.set_code(code_text)

        self._stats: dict[str, float] = {}  # TODO API to access?

    def cleanup(self) -> None:
        # Perform cleanup if using outside of with-statement.
        self._proj_dir.cleanup()
        self._stage = self._DISCARDED

    def __del__(self) -> None:
        if self._stage > self._DISCARDED:
            self.cleanup()

    def __enter__(self) -> Self:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.cleanup()

    def _write_project(
        self,
        *,
        cpp_std: str | None = None,
        include_dirs: Sequence[str] = (),
    ) -> None:
        rendered_default_options = ", ".join(
            _quote_meson_str(o)
            for o in ([f"cpp_std={cpp_std}"] if cpp_std else [])
        )
        rendered_include_dirs = ", ".join(
            _quote_meson_str(d) for d in include_dirs
        )

        with open(self._proj_path / "meson.build", "w") as f:
            f.write(
                textwrap.dedent(f"""\
                    project(
                        'odext_module',
                        'cpp',
                        default_options: [{rendered_default_options}],
                    )
                    python = import('python').find_installation(pure: false)
                    extmod = python.extension_module(
                        'odext_module',
                        'odext_module.cpp',
                        include_directories: [{rendered_include_dirs}],
                    )
            """)
            )

    def _write_code(self, code_text: str) -> None:
        with open(self._proj_path / "odext_module.cpp", "w") as f:
            f.write(code_text)

    async def async_set_code(self, code_text: str) -> None:
        assert self._stage >= self._CREATED
        await asyncio.get_running_loop().run_in_executor(
            None, lambda: self._write_code(code_text)
        )
        if self._stage < self._CODE_WRITTEN:
            self._stage = self._CODE_WRITTEN

    def set_code(self, code_text: str) -> None:
        return _async.submit(self.async_set_code(code_text)).result()

    def _configure_args(self) -> list[str]:
        args = ["setup", "builddir", "--buildtype=release"]
        if platform.system() == "Windows":
            args.append("--vsenv")
        return args

    def _read_config_log(self) -> str:
        with open(self._proj_path / "builddir/meson-logs/meson-log.txt") as f:
            return f.read()

    def _get_extmod_path(self) -> Path:
        with open(
            self._proj_path / "builddir/meson-info/intro-targets.json"
        ) as f:
            targets = json.load(f)
        for target in targets:
            if target["name"].startswith("odext_module."):
                assert len(target["filename"]) == 1
                return Path(target["filename"][0])
        else:
            raise RuntimeError("Cannot find the build target in meson-info")

    async def async_configure(self) -> None:
        start = time.perf_counter()
        assert self._stage == self._CODE_WRITTEN  # Configure only once.
        returncode, stdout, stderr = await _run_meson(
            *self._configure_args(),
            cwd=self._proj_path,
            env=self._config_env,
        )
        if returncode != 0:
            try:
                log_text = await asyncio.get_running_loop().run_in_executor(
                    None, self._read_config_log
                )
            except OSError:
                log_text = "(unavailable)"
            raise RuntimeError(
                f"Build configuration failed: {stdout}\n"
                + f"Contents of meson-log.txt:\n{log_text}"
            )

        self._extmod_path = await asyncio.get_running_loop().run_in_executor(
            None, self._get_extmod_path
        )
        self._stage = self._CONFIGURED
        self._stats["configure"] = time.perf_counter() - start

    def configure(self) -> None:
        return _async.submit(self.async_configure()).result()

    async def async_build(self) -> Path:
        assert self._stage >= self._CODE_WRITTEN
        if self._stage < self._CONFIGURED:
            await self.async_configure()

        start = time.perf_counter()
        returncode, stdout, stderr = await _run_meson(
            "compile",
            cwd=self._proj_path / "builddir",
            env=self._config_env,
        )
        if returncode != 0:
            raise RuntimeError(f"Build failed: {stdout}")
        self._staged = self._BUILT
        self._stats["build"] = time.perf_counter() - start
        return self._extmod_path

    def build(self) -> Path:
        return _async.submit(self.async_build()).result()


class ExtensionImporter:
    def __init__(self) -> None:
        self._module_dir: tempfile.TemporaryDirectory | None = None

    def _module_path(self) -> Path:
        if self._module_dir is None:
            self._module_dir = tempfile.TemporaryDirectory(
                prefix="odext-modules-"
            )
            atexit.register(self._module_dir.cleanup)
        return Path(self._module_dir.name)

    def import_module(
        self, name: str, path: Path, *, ok_to_move=False
    ) -> Module:
        # The name must match the module name built into the extension module.
        # The name must not have been previously imported. We cannot allow the
        # same name even if the module contents are identical, because it is
        # not trivial to guarantee the latter (Windows PE binaries typically
        # contain a timestamp). We also cannot allow the same name if a
        # previous import attempt failed, because in general we cannot cleanly
        # recover from failed imports.
        final_path = path.parent / (name + path.suffix)
        if final_path.exists():
            raise RuntimeError(
                f"A module with name {name} has already been imported"
            )
        if ok_to_move and not path.is_symlink():
            shutil.move(path, final_path)
        else:
            shutil.copy2(path, final_path)
        return self._import(name, final_path)

    def _import(self, name: str, path: Path) -> Module:
        spec = importlib.util.spec_from_file_location(name, path)
        if not spec:
            raise ImportError(
                f"Cannot create import spec for generated module {name} at {path}"
            )
        if not spec.loader:  # Unlikely, I think.
            raise ImportError(
                f"Failed to obtain loader for generated module {name} at {path}"
            )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
