# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .. import _events
from .._bucket_sources import (
    BucketSource,
    PyBucketSource,
)
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
)
from .._events import EventType, WarningEvent
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _bucket_source_or_default,
    _cast_int_expr,
    _remove_events_from_set,
    _with_event_added,
)

_OVERFLOW_POLICIES = {
    "error": "tcspc::histogram_policy::error_on_overflow",
    "stop": "tcspc::histogram_policy::stop_on_overflow",
    "saturate": "tcspc::histogram_policy::saturate_on_overflow",
    "reset": "tcspc::histogram_policy::reset_on_overflow",
}


def _histogram_policy_expression(
    overflow: str,
    *,
    emit_concluding: bool = False,
    reset_after_scan: bool = False,
    clear_every_scan: bool = False,
    no_clear_new_bucket: bool = False,
) -> str:
    if overflow not in _OVERFLOW_POLICIES:
        raise ValueError(
            f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
        )
    parts = [_OVERFLOW_POLICIES[overflow]]
    if emit_concluding:
        parts.append("tcspc::histogram_policy::emit_concluding_events")
    if reset_after_scan:
        parts.append("tcspc::histogram_policy::reset_after_scan")
    if clear_every_scan:
        parts.append("tcspc::histogram_policy::clear_every_scan")
    if no_clear_new_bucket:
        parts.append("tcspc::histogram_policy::no_clear_new_bucket")
    return "(" + " | ".join(parts) + ")"


@final
class Histogram(_RelayNode):
    """Processor that accumulates a histogram from bin increment events.

    Each `BinIncrementEvent` increments the corresponding bin; a `HistogramEvent`
    carrying a view of the current histogram is emitted on each increment. A
    round of accumulation ends on a ``reset_event_type``; if concluding events
    are enabled, a `ConcludingHistogramEvent` is emitted on each reset.

    Parameters
    ----------
    num_bins : int or Param[int]
        Number of bins in the histogram.
    max_per_bin : int or Param[int]
        Maximum count per bin before the overflow policy applies.
    reset_event_type : EventType or None
        Event type that resets (starts a new round). ``None`` (the default)
        means no reset event.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets holding the histogram. If ``None``, a default
        `RecyclingBucketSource` is used.
    overflow : str
        Overflow behavior: ``"error"`` (default), ``"stop"``, ``"saturate"``,
        or ``"reset"``.
    emit_concluding : bool
        If ``True``, emit a `ConcludingHistogramEvent` on each reset. Default
        ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type`` and ``bin_type``. Defaults
        to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BinIncrementEvent`: increment the corresponding bin and emit a
      `HistogramEvent` carrying a view of the current histogram.
    - ``reset_event_type`` (if given): end the current round and start a new
      one; if ``emit_concluding`` is set, a `ConcludingHistogramEvent` is
      emitted first.
    - All other event types: pass through unchanged.
    - On bin overflow: handled per ``overflow`` (when ``"saturate"``, a
      `WarningEvent` is emitted).
    - End of input: pass through; the current histogram is not emitted as a
      concluding event.

    The histogram-carrying output events can be sent directly to a Python
    sink, where they are delivered as `EventInstance` values whose
    ``data_bucket`` attribute reads as a read-only NumPy array. Alternatively,
    insert `ExtractBucket` to obtain just the histogram as a bare NumPy array.

    See Also
    --------
    :cpp:`tcspc::histogram`
        The underlying C++ factory function.
    :py:obj:`ExtractBucket`
        Extract the histogram bucket as a bare NumPy array.
    """

    def __init__(
        self,
        num_bins: int | Param[int],
        max_per_bin: int | Param[int],
        reset_event_type: EventType | None = None,
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        overflow: str = "error",
        emit_concluding: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        if overflow not in _OVERFLOW_POLICIES:
            raise ValueError(
                f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
            )
        self._num_bins = num_bins
        self._max_per_bin = max_per_bin
        self._reset_event_type = reset_event_type
        self._overflow = overflow
        self._emit_concluding = emit_concluding
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._bin_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_type"
        )
        self._bucket_source = _bucket_source_or_default(
            self._bin_element, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._num_bins, Param):
            params.append((self._num_bins, _size_type))
        if isinstance(self._max_per_bin, Param):
            params.append((self._max_per_bin, _CppTypeName(f"{nt}::bin_type")))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bucket_source._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        bi = _events.BinIncrementEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (bi,))
        out = _with_event_added(
            out, _events.HistogramEvent(self._numeric_traits)
        )
        if self._emit_concluding:
            out = _with_event_added(
                out, _events.ConcludingHistogramEvent(self._numeric_traits)
            )
        if self._overflow == "saturate":
            out = _with_event_added(out, WarningEvent())
        return out

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        bt = f"{nt}::bin_type"
        policy = _histogram_policy_expression(
            self._overflow, emit_concluding=self._emit_concluding
        )
        reset = (
            self._reset_event_type._cpp_type_name()
            if self._reset_event_type is not None
            else "tcspc::never_event"
        )
        num_bins = gencontext.size_t_expression(self._num_bins)
        max_per_bin = _cast_int_expr(gencontext, self._max_per_bin, bt)
        return _CppExpression(
            f"""\
            tcspc::histogram<{policy}, {reset}, {nt}>(
                tcspc::arg::num_bins<std::size_t>{{{num_bins}}},
                tcspc::arg::max_per_bin<{bt}>{{{max_per_bin}}},
                {self._bucket_source._cpp_expression(gencontext)},
                {downstream}
            )"""
        )


@final
class ScanHistograms(_RelayNode):
    """Processor that accumulates an array of histograms over a scan.

    Each `BinIncrementClusterEvent` updates the next element of the histogram
    array; a `HistogramArrayProgressEvent` is emitted per cluster, and a
    `HistogramArrayEvent` upon completion of each scan. A round ends on a
    ``reset_event_type``; if concluding events are enabled, a
    `ConcludingHistogramArrayEvent` is emitted on each reset.

    Parameters
    ----------
    num_elements : int or Param[int]
        Number of histograms (elements) in the array.
    num_bins : int or Param[int]
        Number of bins per element.
    max_per_bin : int or Param[int]
        Maximum count per bin before the overflow policy applies.
    reset_event_type : EventType or None
        Event type that resets (starts a new round). ``None`` (the default)
        means no reset event.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets holding the histogram array. If ``None``, a default
        `RecyclingBucketSource` is used.
    overflow : str
        Overflow behavior: ``"error"`` (default), ``"stop"``, ``"saturate"``,
        or ``"reset"``.
    emit_concluding : bool
        If ``True``, emit a `ConcludingHistogramArrayEvent` on each reset.
        Default ``False``.
    reset_after_scan : bool
        If ``True``, reset after each completed scan. Default ``False``.
    clear_every_scan : bool
        If ``True``, clear the array at the start of each scan. Default
        ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type`` and ``bin_type``. Defaults
        to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BinIncrementClusterEvent`: update the next element of the histogram
      array and emit a `HistogramArrayProgressEvent`; emit a
      `HistogramArrayEvent` when a scan completes.
    - ``reset_event_type`` (if given): end the current round and start a new
      one; if ``emit_concluding`` is set, a `ConcludingHistogramArrayEvent`
      is emitted first.
    - All other event types: pass through unchanged.
    - On bin overflow: handled per ``overflow`` (when ``"saturate"``, a
      `WarningEvent` is emitted).
    - End of input: pass through; an incomplete scan is not emitted as a
      concluding event.

    The histogram-carrying output events can be sent directly to a Python
    sink, where they are delivered as `EventInstance` values whose
    ``data_bucket`` attribute reads as a read-only NumPy array. Alternatively,
    insert `ExtractBucket` to obtain just the histogram array as a bare NumPy
    array.

    See Also
    --------
    :cpp:`tcspc::scan_histograms`
        The underlying C++ factory function.
    :py:obj:`ExtractBucket`
        Extract a histogram-array bucket as a bare NumPy array.
    """

    def __init__(
        self,
        num_elements: int | Param[int],
        num_bins: int | Param[int],
        max_per_bin: int | Param[int],
        reset_event_type: EventType | None = None,
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        overflow: str = "error",
        emit_concluding: bool = False,
        reset_after_scan: bool = False,
        clear_every_scan: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        if overflow not in _OVERFLOW_POLICIES:
            raise ValueError(
                f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
            )
        self._num_elements = num_elements
        self._num_bins = num_bins
        self._max_per_bin = max_per_bin
        self._reset_event_type = reset_event_type
        self._overflow = overflow
        self._emit_concluding = emit_concluding
        self._reset_after_scan = reset_after_scan
        self._clear_every_scan = clear_every_scan
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._bin_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_type"
        )
        self._bucket_source = _bucket_source_or_default(
            self._bin_element, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._num_elements, Param):
            params.append((self._num_elements, _size_type))
        if isinstance(self._num_bins, Param):
            params.append((self._num_bins, _size_type))
        if isinstance(self._max_per_bin, Param):
            params.append((self._max_per_bin, _CppTypeName(f"{nt}::bin_type")))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bucket_source._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (cluster,))
        out = _with_event_added(
            out, _events.HistogramArrayProgressEvent(self._numeric_traits)
        )
        out = _with_event_added(
            out, _events.HistogramArrayEvent(self._numeric_traits)
        )
        if self._emit_concluding:
            out = _with_event_added(
                out,
                _events.ConcludingHistogramArrayEvent(self._numeric_traits),
            )
        if self._overflow == "saturate":
            out = _with_event_added(out, WarningEvent())
        return out

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        bt = f"{nt}::bin_type"
        policy = _histogram_policy_expression(
            self._overflow,
            emit_concluding=self._emit_concluding,
            reset_after_scan=self._reset_after_scan,
            clear_every_scan=self._clear_every_scan,
        )
        reset = (
            self._reset_event_type._cpp_type_name()
            if self._reset_event_type is not None
            else "tcspc::never_event"
        )
        num_elements = gencontext.size_t_expression(self._num_elements)
        num_bins = gencontext.size_t_expression(self._num_bins)
        max_per_bin = _cast_int_expr(gencontext, self._max_per_bin, bt)
        return _CppExpression(
            f"""\
            tcspc::scan_histograms<{policy}, {reset}, {nt}>(
                tcspc::arg::num_elements<std::size_t>{{{num_elements}}},
                tcspc::arg::num_bins<std::size_t>{{{num_bins}}},
                tcspc::arg::max_per_bin<{bt}>{{{max_per_bin}}},
                {self._bucket_source._cpp_expression(gencontext)},
                {downstream}
            )"""
        )
