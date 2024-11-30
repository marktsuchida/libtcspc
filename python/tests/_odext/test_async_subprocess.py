# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import asyncio
import concurrent.futures
import sys
import threading
import time
from pathlib import Path

import psutil
import pytest
from libtcspc import _odext
from libtcspc._odext import _async


def test_async_subprocess_does_not_leak():
    # Demonstrate, and test, how to run a subprocess without leaking it upon
    # cancellation. (Explicit terminate() is necessary!)
    # For now, we assume that Process.communicate() does not raise anything
    # other than CancelledError unless the process spontaneously terminates (in
    # which case, of course, we do not need to worry about leaking).
    pid = []
    started = threading.Event()

    async def run_subproc_forever():
        proc = await asyncio.create_subprocess_exec(
            Path(sys.executable),
            "-qc",
            "import time; time.sleep(3600)",
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        pid.append(proc.pid)
        started.set()
        try:
            stdout, stderr = await proc.communicate()
        except asyncio.CancelledError:
            proc.terminate()  # Process leaks without this!
            await proc.wait()
            raise
        return proc.returncode

    fut = _async.submit(run_subproc_forever())
    started.wait()
    time.sleep(0.01)  # Intent: ensure communicate() starts.
    assert psutil.pid_exists(pid[0])
    fut.cancel()
    with pytest.raises(concurrent.futures.CancelledError):
        fut.result()
    # It may take a bit for the pid to go away.
    deadline = time.perf_counter() + 1.0
    while time.perf_counter() < deadline:
        if not psutil.pid_exists(pid[0]):
            break
        time.sleep(0.001)
    assert not psutil.pid_exists(pid[0])


def test_run_subprocess():
    fut = _async.submit(
        _odext._run_subprocess_nostdin(
            Path(sys.executable), "-qc", "print('hello')"
        )
    )
    retcode, stdout, stderr = fut.result()
    assert retcode == 0
    assert stdout == "hello\n"
    assert stderr == ""


def test_run_subprocess_nostdin_cancel():
    fut = _async.submit(
        _odext._run_subprocess_nostdin(
            Path(sys.executable),
            "-qc",
            "import time; time.sleep(3600)",
        )
    )
    time.sleep(0.01)
    fut.cancel()
    with pytest.raises(concurrent.futures.CancelledError):
        fut.result()
