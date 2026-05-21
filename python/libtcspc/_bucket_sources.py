# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppTypeName, _size_type
from ._events import EventType
from ._param import Param, _Parameterized


class BucketSource(_Parameterized):
    """Base class for sources of `bucket` storage used by stream-processing graphs.

    Subclasses implement different allocation strategies (fresh
    allocation per request, pooled recycling, etc.) for the storage
    that backs the buckets emitted by processors such as
    `ReadBinaryStream`, `Batch`, and `Acquire`.

    See Also
    --------
    :cpp:`tcspc::bucket_source`
        The underlying C++ bucket source interface.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


class NewDeleteBucketSource(BucketSource):
    """Bucket source that allocates fresh storage for each bucket using ``new[]``/``delete[]``.

    Storage from destroyed buckets is not reused. Suitable for simple
    workloads where allocation cost is not a concern.

    Parameters
    ----------
    object_type : EventType
        Element type of the buckets produced by this source.

    Notes
    -----
    Thread-safe.

    See Also
    --------
    :cpp:`tcspc::new_delete_bucket_source`
        The underlying C++ bucket source.
    :py:obj:`RecyclingBucketSource`
        Bucket source that reuses storage.
    """

    def __init__(self, object_type: EventType) -> None:
        self._object_type = object_type

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        t = self._object_type._cpp_type_name()
        return _CppExpression(
            f"tcspc::new_delete_bucket_source<{t}>::create()"
        )


class RecyclingBucketSource(BucketSource):
    """Bucket source that reuses storage from destroyed buckets.

    Storage allocated for a bucket is returned to an internal pool when
    the bucket is destroyed and may be reused for a subsequent request,
    avoiding repeated allocation.

    Parameters
    ----------
    object_type : EventType
        Element type of the buckets produced by this source.
    blocking : bool, keyword-only
        Behavior when ``max_bucket_count`` outstanding buckets exist
        and a new one is requested. ``True`` blocks until a bucket is
        recycled; ``False`` (the default) raises an overflow error.
    clear_recycled : bool, keyword-only
        If ``True``, recycled storage is cleared before reuse. Default
        ``False``.
    max_bucket_count : int or Param[int] or None, keyword-only
        Maximum number of outstanding buckets. ``None`` (the default)
        is unlimited. Must be non-negative.

    Raises
    ------
    ValueError
        If ``max_bucket_count`` is a negative integer, or a `Param`
        whose ``default_value`` is negative.

    Notes
    -----
    Thread-safe. When ``max_bucket_count`` is set, at least two
    buckets must be allowed to circulate to avoid deadlock.

    See Also
    --------
    :cpp:`tcspc::recycling_bucket_source`
        The underlying C++ bucket source.
    :py:obj:`NewDeleteBucketSource`
        Bucket source without recycling.
    """

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
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_count, Param):
            return ((self._max_count, _size_type),)
        return ()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        tmpl_args = ", ".join(
            (
                self._object_type._cpp_type_name(),
                "true" if self._blocking else "false",
                "true" if self._clear else "false",
            )
        )
        max_count = (
            ""
            if self._max_count is None
            else gencontext.size_t_expression(self._max_count)
        )
        return _CppExpression(
            f"tcspc::recycling_bucket_source<{tmpl_args}>::create({max_count})"
        )
