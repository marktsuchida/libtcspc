# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "BucketSource",
    "NewDeleteBucketSource",
    "RecyclingBucketSource",
]

from collections.abc import Sequence

from typing_extensions import override

from ._codegen import CodeGenerationContext
from ._cpp_utils import CppExpression, CppTypeName, size_type
from ._events import EventType
from ._param import Param, Parameterized


class BucketSource(Parameterized):
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        raise NotImplementedError()


class NewDeleteBucketSource(BucketSource):
    def __init__(self, object_type: EventType) -> None:
        self._object_type = object_type

    @override
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        t = self._object_type.cpp_type_name()
        return CppExpression(f"tcspc::new_delete_bucket_source<{t}>::create()")


class RecyclingBucketSource(BucketSource):
    def __init__(
        self,
        object_type: EventType,
        *,
        blocking: bool = False,
        clear_recycled: bool = False,
        max_bucket_count: int | Param[int] | None = None,
    ) -> None:
        if isinstance(max_bucket_count, int) and max_bucket_count < 0:
            raise ValueError("max_bucket_count must not be negative")
        if (
            isinstance(max_bucket_count, Param)
            and max_bucket_count.default_value is not None
            and max_bucket_count.default_value < 0
        ):
            raise ValueError(
                "default value for max_bucket_count must not be negative"
            )

        self._object_type = object_type
        self._blocking = blocking
        self._clear = clear_recycled
        self._max_count = max_bucket_count

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        if isinstance(self._max_count, Param):
            return ((self._max_count, size_type),)
        return ()

    @override
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        tmpl_args = ", ".join(
            (
                self._object_type.cpp_type_name(),
                "true" if self._blocking else "false",
                "true" if self._clear else "false",
            )
        )
        max_count = (
            ""
            if self._max_count is None
            else gencontext.size_t_expression(self._max_count)
        )
        return CppExpression(
            f"tcspc::recycling_bucket_source<{tmpl_args}>::create({max_count})"
        )
