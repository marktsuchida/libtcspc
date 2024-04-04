# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from ._events import EventType


class ObjectPool:
    def __init__(
        self,
        object_type: EventType,
        *,
        initial_count: int = 0,
        max_count: int = -1,
    ) -> None:
        self._object_type = object_type
        self._init_count = initial_count
        self._max_count = max_count

    @property
    def cpp(self) -> str:
        t, i, m = self._object_type.cpp_type, self._init_count, self._max_count
        if m >= 0:
            return f"std::make_shared<tcspc::object_pool<{t}>>({i}, {m})"
        else:
            return f"std::make_shared<tcspc::object_pool<{t}>>({i})"
