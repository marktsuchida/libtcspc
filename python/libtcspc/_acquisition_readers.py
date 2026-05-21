# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod
from typing import final

import numpy as np
from typing_extensions import override

from ._cpp_utils import _CppExpression
from ._events import EventType


class AcquisitionReader(ABC):
    """Base class for built-in C++-side acquisition data sources usable with `Acquire`.

    Subclasses wrap a C++ reader type that fills buckets supplied by
    `Acquire`. Acquisition readers implemented in Python use the
    separate `PyAcquisitionReader` interface and are bound at execution
    time via a `Param`.
    """

    @abstractmethod
    def _cpp_expression(self) -> _CppExpression: ...


@final
class NullReader(AcquisitionReader):
    """Acquisition reader that immediately signals end of stream.

    Useful as a placeholder when an `Acquire` source is required
    structurally but no data is expected to be read.

    Parameters
    ----------
    event_type : EventType
        Element type that the reader would otherwise produce. Must
        match the element type of the `Acquire` processor.

    See Also
    --------
    :cpp:`tcspc::null_reader`
        The underlying C++ reader.
    :py:obj:`Acquire`
        Source processor that drives the reader.
    """

    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def _cpp_expression(self) -> _CppExpression:
        return _CppExpression(
            f"tcspc::null_reader<{self._event_type._cpp_type_name()}>()"
        )


@final
class StuckReader(AcquisitionReader):
    """Acquisition reader that waits indefinitely without producing data.

    Used to exercise cancellation paths. When this reader is in use,
    the only way to terminate the surrounding `Acquire` is to call
    ``halt()`` on its `AcquireAccess`.

    Parameters
    ----------
    event_type : EventType
        Element type that the reader would otherwise produce. Must
        match the element type of the `Acquire` processor.

    See Also
    --------
    :cpp:`tcspc::stuck_reader`
        The underlying C++ reader.
    :py:obj:`AcquireAccess`
        Runtime access object providing ``halt()``.
    """

    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def _cpp_expression(self) -> _CppExpression:
        return _CppExpression(
            f"tcspc::stuck_reader<{self._event_type._cpp_type_name()}>()"
        )


class PyAcquisitionReader(ABC):
    """
    Acquisition reader implemented in Python and supplied as an argument upon
    creation of the execution context.

    See Also
    --------
    :py:obj:`Acquire`
        Source processor that drives the reader.
    :py:obj:`AcquireAccess`
        Runtime access object providing ``halt()``.
    """

    @abstractmethod
    def __call__(self, buffer: np.ndarray) -> int | None:
        """
        Read acquired data into the given buffer.

        The element type of the buffer will match that of the ``Acquire``
        processor. An implementation of this method should read at most the
        number of elements that fit in `buffer`, and return the number read.
        If the end of the data stream has been reached, return ``None``
        instead. To signal a read error, raise an exception; do not return
        ``None`` for error conditions.

        Implementations must not store `buffer`; accessing it after returning
        will crash the Python interpreter (this dangerous behavior is a
        compromise for performance, to avoid copying the data unnecessarily).
        """
        ...
