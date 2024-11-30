# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

"""
Run coroutines in a private, background event loop.
"""

__all__ = ["submit"]

import asyncio
import atexit
import concurrent.futures
import functools
import threading
from collections.abc import Coroutine


class _BackgroundEventLoop:
    def __init__(self, *, daemon: bool = False) -> None:
        self._task_futures: set[concurrent.futures.Future] = set()
        self._loop: asyncio.AbstractEventLoop | None = None
        latch = threading.Event()
        self._daemon = daemon
        self._loop_thread = threading.Thread(
            target=self._run_loop, args=(latch,), daemon=daemon
        )
        self._loop_thread.start()
        latch.wait()  # Ensure self._loop is set before returning.

    def _run_loop(self, latch: threading.Event) -> None:
        assert self._loop is None
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        latch.set()
        self._loop.run_forever()
        self._loop.close()
        self._loop = None

    # Note: Python may hang on exit if not shut down.
    def shutdown(self) -> None:
        assert self._loop is not None

        futures = list(self._task_futures)  # Copy for safe iteration.
        for fut in futures:
            fut.cancel()
        concurrent.futures.wait(futures)
        self._task_futures.clear()

        async def stop_loop():
            asyncio.get_running_loop().stop()

        asyncio.run_coroutine_threadsafe(stop_loop(), self._loop)
        # Waiting for stop_loop() would hang because the loop was stopped.

        if not self._daemon:
            self._loop_thread.join()
            assert self._loop is None

    def submit(self, coro: Coroutine) -> concurrent.futures.Future:
        assert self._loop is not None
        fut = asyncio.run_coroutine_threadsafe(coro, self._loop)
        self._task_futures.add(fut)
        fut.add_done_callback(lambda f: self._task_futures.remove(f))
        return fut


# There does not seem to be a way to clean up a non-daemon thread at 'atexit'
# time: joining a thread in an atexit handler can (will?) hang (confirmed on
# macOS). We use a dameon thread and do a best-effort cleanup. See unit tests.
@functools.cache
def _loop() -> _BackgroundEventLoop:
    loop = _BackgroundEventLoop(daemon=True)
    atexit.register(lambda: loop.shutdown())
    return loop


def submit(coro: Coroutine) -> concurrent.futures.Future:
    return _loop().submit(coro)
