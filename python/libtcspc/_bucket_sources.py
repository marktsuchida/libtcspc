# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence

from typing_extensions import override

from ._cpp_utils import CppExpression, CppTypeName
from ._events import EventType
from ._node import CodeGenerationContext
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
        max_bucket_count: int | Param[int] = -1,
    ) -> None:
        self._object_type = object_type
        self._blocking = blocking
        self._clear = clear_recycled
        self._max_count = max_bucket_count

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        if isinstance(self._max_count, Param):
            return ((self._max_count, CppTypeName("std::size_t")),)
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
        if isinstance(self._max_count, Param):
            max_count = f"{gencontext.params_varname}.{self._max_count.name}"
        else:
            max_count = f"{self._max_count}uLL" if self._max_count >= 0 else ""
        return CppExpression(
            f"tcspc::recycling_bucket_source<{tmpl_args}>::create({max_count})"
        )
