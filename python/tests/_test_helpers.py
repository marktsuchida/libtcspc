# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import CppTypeName
from libtcspc._events import EventType
from typing_extensions import override


class _NamedEvent(EventType):
    def __init__(self, cpp_type: CppTypeName) -> None:
        self._cpp_type = cpp_type

    @override
    def _cpp_type_name(self) -> CppTypeName:
        return self._cpp_type
