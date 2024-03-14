# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from . import _cpp_utils


class EventType:
    def __init__(self, cpp_type: str) -> None:
        self.cpp_type = cpp_type

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}({self.cpp_type})>"

    def __eq__(self, other) -> bool:
        return isinstance(other, EventType) and _cpp_utils.is_same_type(
            self.cpp_type, other.cpp_type
        )
