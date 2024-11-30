# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import asyncio
import sys

from libtcspc._odext import _async


def test_start_stop():
    loop = _async._BackgroundEventLoop()
    loop.shutdown()


def test_task_runs():
    async def set_result() -> int:
        return 42

    loop = _async._BackgroundEventLoop()
    try:
        fut = loop.submit(set_result())
        assert fut.result() == 42
    finally:
        loop.shutdown()


def test_task_cancelled_on_shutdown():
    async def sleep_forever() -> None:
        await asyncio.sleep(sys.float_info.max)

    loop = _async._BackgroundEventLoop()
    try:
        # Test with many tasks to increase the likelihood of detecting races
        # between cleanup on loop shutdown and the resulting task completion.
        futures = [loop.submit(sleep_forever()) for i in range(100)]
    finally:
        loop.shutdown()

    assert all(f.cancelled() for f in futures)


def test_default_loop_task_runs():
    async def set_result() -> int:
        return 42

    fut = _async.submit(set_result())
    assert fut.result() == 42


def test_default_loop_task_canceled_on_exit():
    # Failure of this test could result in pytest hanging on exit.
    async def sleep_forever() -> None:
        await asyncio.sleep(sys.float_info.max)

    _async.submit(sleep_forever())
