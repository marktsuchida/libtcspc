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

__all__ = [
    "Builder",
    "ExtensionImporter",
]

import asyncio
import atexit
import functools
import importlib.util
import json
import logging
import os
import platform
import shutil
import subprocess
import tempfile
import time
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any, Literal

from typing_extensions import Self

from . import _async

Module = Any


def _quote_meson_str(s: str) -> str:
    # Note: This does not work on arbitrary strings.
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def _quote_meson_path(p: str) -> str:
    if platform.system() == "Windows":
        p = p.replace("\\", "/")
    return _quote_meson_str(p)


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


class Builder:
    _DISCARDED, _CREATED, _CODE_WRITTEN, _CONFIGURED, _BUILT = range(5)

    def __init__(
        self,
        binary_type: Literal[
            "extension_module", "executable"
        ] = "extension_module",
        *,
        cxx: str | None = None,
        cpp_std: str | None = None,
        include_dirs: Iterable[str | Path] = (),
        extra_source_files: Iterable[str | Path] = (),
        pch_includes: Iterable[str] = (),
        pch_sys_includes: Iterable[str] = (),
        code_text: str | None = None,
        tempdir: Path | None = None,
    ) -> None:
        self._binary_type = binary_type

        self._proj_dir = tempfile.TemporaryDirectory(
            prefix="odext-", dir=tempdir
        )
        self._proj_path = Path(self._proj_dir.name)

        # It would be nice to be able to isolate the env vars (copying only
        # PATH), but on Windows the compiler cannot find things (even with
        # --vsenv) in an environment retaining only PATH and SYSTEMROOT.
        self._config_env = dict(os.environ)
        if cxx:
            self._config_env["CXX"] = cxx

        self._write_project(
            cpp_std=cpp_std,
            include_dirs=(str(i) for i in include_dirs),
            extra_source_files=(str(s) for s in extra_source_files),
            pch_includes=pch_includes,
            pch_sys_includes=pch_sys_includes,
        )
        self._stage = self._CREATED

        if code_text is not None:
            self.set_code(code_text)

        self._stats: dict[str, float] = {}  # TODO API to access?

    def cleanup(self) -> None:
        # Perform cleanup if using outside of with-statement.
        self._proj_dir.cleanup()
        self._stage = self._DISCARDED

    def __del__(self) -> None:
        if hasattr(self, "_stage") and self._stage > self._DISCARDED:
            self.cleanup()

    def __enter__(self) -> Self:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.cleanup()

    def _write_project(
        self,
        *,
        cpp_std: str | None = None,
        include_dirs: Iterable[str] = (),
        extra_source_files: Iterable[str] = (),
        pch_includes: Iterable[str] = (),
        pch_sys_includes: Iterable[str] = (),
    ) -> None:
        rendered_default_options = ", ".join(
            _quote_meson_str(o)
            for o in ([f"cpp_std={cpp_std}"] if cpp_std else [])
        )
        rendered_include_dirs = ", ".join(
            _quote_meson_path(d) for d in include_dirs
        )
        rendered_extra_sources = ", ".join(
            _quote_meson_path(s) for s in extra_source_files
        )

        pch_text = "".join(
            f'#include "{inc}"\n' for inc in pch_includes
        ) + "".join(f"#include <{inc}>\n" for inc in pch_sys_includes)
        if len(pch_text):
            os.mkdir(self._proj_path / "pch")
            with open(self._proj_path / "pch/pch.hpp", "w") as f:
                f.write(pch_text)
            cpp_pch = "cpp_pch: 'pch/pch.hpp',"
        else:
            cpp_pch = ""

        with open(self._proj_path / "meson.build", "w") as f:
            f.write(
                f"""project(
                        'odext_project',
                        'cpp',
                        default_options: [{rendered_default_options}],
                    )
                """
            )
            if extra_source_files:
                f.write(f"extra_sources = files({rendered_extra_sources})\n")
            if self._binary_type == "extension_module":
                f.write(
                    f"""py = import('python').find_installation(pure: false)
                        py.extension_module(
                            'odext_target',
                            [
                                'source.cpp',
                                extra_sources,
                            ],
                            include_directories: [{rendered_include_dirs}],
                            {cpp_pch}
                        )
                    """
                )
            elif self._binary_type == "executable":
                f.write(
                    f"""executable(
                            'odext_target',
                            [
                                'source.cpp',
                                extra_sources,
                            ],
                            include_directories: [{rendered_include_dirs}],
                            {cpp_pch}
                        )
                    """
                )

    def _write_code(self, code_text: str) -> None:
        if filt_cmd := os.environ.get("LIBTCSPC_PY_GENERATED_CODE_FILTER"):
            r = subprocess.run(
                filt_cmd,
                shell=True,
                input=code_text.encode(),
                capture_output=True,
            )
            if r.returncode == 0:
                code_text = r.stdout.decode()
            else:
                logging.debug(f"Generated-code filter failed: {filt_cmd}")
        source_path = self._proj_path / "source.cpp"
        with open(source_path, "w") as f:
            f.write(code_text)
        if os.environ.get("LIBTCSPC_PY_LOG_GENERATED_CODE") == "1":
            logging.debug(f"Wrote generated code:\n{code_text}")

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

    def _get_target_path(self) -> Path:
        with open(
            self._proj_path / "builddir/meson-info/intro-targets.json"
        ) as f:
            targets = json.load(f)
        for target in targets:
            if target["name"] == "odext_target" or target["name"].startswith(
                "odext_target."
            ):
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
                "Build configuration failed:\n"
                + f"Stdout: {stdout}\n"
                + f"Stderr: {stderr}\n"
                + f"Contents of meson-log.txt:\n{log_text}"
            )

        self._target_path = await asyncio.get_running_loop().run_in_executor(
            None, self._get_target_path
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
        return self._target_path

    def build(self) -> Path:
        return _async.submit(self.async_build()).result()


class ExtensionImporter:
    def __init__(self) -> None:
        self._module_dir: tempfile.TemporaryDirectory | None = None

    def _module_path(self) -> Path:
        # On Windows, we cannot delete an imported .pyd until after Python has
        # exited. For now, we leak the temp files, which should hopefully not
        # be that harmful because the temporary directory is cleared by Storage
        # Sense (if enabled).
        # It's hard to fix this from here; applications could provide a known
        # temporary directory that they then clear the next time they start up.
        if self._module_dir is None:
            self._module_dir = tempfile.TemporaryDirectory(
                prefix="odext-modules-", ignore_cleanup_errors=True
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
        final_path = self._module_path() / (name + path.suffix)
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
