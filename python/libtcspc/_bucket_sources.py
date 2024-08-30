# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from ._cpp_utils import CppExpression
from ._events import EventType


class BucketSource:
    @property
    def cpp(self) -> CppExpression:
        raise NotImplementedError()


class NewDeleteBucketSource(BucketSource):
    def __init__(self, object_type: EventType) -> None:
        self._object_type = object_type

    @property
    def cpp(self) -> CppExpression:
        t = self._object_type.cpp_type
        return CppExpression(f"tcspc::new_delete_bucket_source<{t}>::create()")


class RecyclingBucketSource(BucketSource):
    def __init__(
        self,
        object_type: EventType,
        *,
        blocking: bool = False,
        clear_recycled: bool = False,
        max_bucket_count: int = -1,
    ) -> None:
        self._object_type = object_type
        self._blocking = blocking
        self._clear = clear_recycled
        self._max_count = max_bucket_count

    @property
    def cpp(self) -> CppExpression:
        t, b, c, m = (
            self._object_type.cpp_type,
            "true" if self._blocking else "false",
            "true" if self._clear else "false",
            self._max_count,
        )
        arg = str(m) if m >= 0 else ""
        return CppExpression(
            f"tcspc::recycling_bucket_source<{t}, {b}, {c}>::create({arg})"
        )
