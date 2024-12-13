# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "NullReader",
    "AcquisitionReader",
    "StuckReader",
]

from abc import abstractmethod
from typing import Protocol, final

import numpy as np
from typing_extensions import override

from ._cpp_utils import CppExpression
from ._events import EventType


class AcquisitionReader:
    @abstractmethod
    def cpp_expression(self) -> CppExpression:
        raise NotImplementedError()


@final
class NullReader(AcquisitionReader):
    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def cpp_expression(self) -> CppExpression:
        return CppExpression(
            f"tcspc::null_reader<{self._event_type.cpp_type_name()}>()"
        )


@final
class StuckReader(AcquisitionReader):
    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def cpp_expression(self) -> CppExpression:
        return CppExpression(
            f"tcspc::stuck_reader<{self._event_type.cpp_type_name()}>()"
        )


class PyAcquisitionReader(Protocol):
    """
    Acquisition reader implemented in Python and supplied as an argument upon
    creation of the execution context.
    """

    @abstractmethod
    def __call__(self, buffer: np.ndarray) -> int | None:
        """
        Read acquired data into the given buffer.

        The element type of the buffer will match that of the ``Acquire``
        processor. An implementation of this method should read at most the
        number of elements that fit in `buffer`, and return the number read.
        If there is no more data to be written (because the acquisition
        finished, either successfully or with an error), return ``None``.

        Implementations must not store `buffer`; accessing it after returning
        will crash the Python interpreter (this dangerous behavior is a
        compromise for performance, to avoid copying the data unnecessarily).
        """
        raise NotImplementedError()
