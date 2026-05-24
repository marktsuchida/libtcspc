# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection
from typing import final

from typing_extensions import override

from .. import _events
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
)
from .._events import EventType, WarningEvent
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from ._common import (
    _check_events_subset_of,
)


def _pqt2_decode_output(
    numeric_traits: NumericTraits,
) -> tuple[EventType, ...]:
    return (
        _events.DetectionEvent(numeric_traits),
        _events.MarkerEvent(numeric_traits),
        _events.TimeReachedEvent(numeric_traits),
        WarningEvent(),
    )


def _pqt3_decode_output(
    numeric_traits: NumericTraits,
) -> tuple[EventType, ...]:
    return (
        _events.MarkerEvent(numeric_traits),
        _events.TimeCorrelatedDetectionEvent(numeric_traits),
        _events.TimeReachedEvent(numeric_traits),
        WarningEvent(),
    )


# Becker & Hickl


@final
class DecodeBHSPC(_RelayNode):
    """Processor that decodes Becker & Hickl SPC FIFO records into libtcspc TCSPC events.

    Decoder for SPC-130, 830, 140, 930, 150, 130EM, 150N (NX, NXX),
    130EMN, 160 (X, PCIE), 180N (NX, NXX), and 130IN (INX, INXX). For
    SPC-160 and SPC-180N, the fast intensity counter is not decoded; the
    processor can still be used for these models if the counter value is
    not of interest.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPCEvent`: decoded; for each input record, zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `DataLostEvent` are emitted. A `WarningEvent` is emitted when
      an invalid record is encountered.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (_events.BHSPCEvent(),), self.__class__.__name__
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.MarkerEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_bh_spc<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPC600_256ch(_RelayNode):
    """Processor that decodes BH SPC-600/630 32-bit FIFO records (256-channel mode).

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPC600_256chEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `DataLostEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc600_256ch`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPC600_256chEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_bh_spc600_256ch<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPC600_4096ch(_RelayNode):
    """Processor that decodes BH SPC-600/630 48-bit FIFO records (4096-channel mode).

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPC600_4096chEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `DataLostEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc600_4096ch`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPC600_4096chEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_bh_spc600_4096ch<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPCWithIntensityCounter(_RelayNode):
    """Processor that decodes BH SPC FIFO records including fast intensity counter.

    For SPC-160 and SPC-180N devices. Like `DecodeBHSPC`, but the
    marker-0 records are also decoded into `BulkCountsEvent`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, ``difftime``,
        and ``count`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPCEvent`: decoded; emits zero or more of `TimeReachedEvent`,
      `TimeCorrelatedDetectionEvent`, `BulkCountsEvent`, `MarkerEvent`,
      `DataLostEvent`, and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc_with_intensity_counter`
        The underlying C++ factory function.
    :py:obj:`DecodeBHSPC`
        Decode standard Becker & Hickl SPC FIFO records.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPCEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.BulkCountsEvent(self._numeric_traits),
            _events.DataLostEvent(self._numeric_traits),
            _events.MarkerEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_bh_spc_with_intensity_counter<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


# PicoQuant


@final
class DecodePQT2Generic(_RelayNode):
    """Processor that decodes PicoQuant T2 (Generic) FIFO records.

    Used with HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp 330.
    Sync edges are reported as `DetectionEvent` on channel ``-1``.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2GenericEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_generic`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2GenericEvent(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_generic<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT2HydraHarpV1(_RelayNode):
    """Processor that decodes PicoQuant HydraHarp V1 T2 FIFO records.

    Sync edges are reported as `DetectionEvent` on channel ``-1``.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2HydraHarpV1Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_hydraharpv1`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2HydraHarpV1Event(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_hydraharpv1<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT2PicoHarp300(_RelayNode):
    """Processor that decodes PicoQuant PicoHarp 300 T2 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2PicoHarp300Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_picoharp300`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2PicoHarp300Event(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_picoharp300<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3Generic(_RelayNode):
    """Processor that decodes PicoQuant T3 (Generic) FIFO records.

    Used with HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp 330.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3GenericEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_generic`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3GenericEvent(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_generic<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3HydraHarpV1(_RelayNode):
    """Processor that decodes PicoQuant HydraHarp V1 T3 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3HydraHarpV1Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_hydraharpv1`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3HydraHarpV1Event(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_hydraharpv1<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3PicoHarp300(_RelayNode):
    """Processor that decodes PicoQuant PicoHarp 300 T3 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3PicoHarp300Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_picoharp300`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3PicoHarp300Event(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_picoharp300<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


# Swabian


@final
class DecodeSwabianTags(_RelayNode):
    """Processor that decodes 16-byte Swabian Time Tagger tags.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``count`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `SwabianTagEvent`: decoded; emits one of `DetectionEvent`,
      `BeginLostIntervalEvent`, `EndLostIntervalEvent`,
      `LostCountsEvent`, or `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_swabian_tags`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.SwabianTagEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.BeginLostIntervalEvent(self._numeric_traits),
            _events.DetectionEvent(self._numeric_traits),
            _events.EndLostIntervalEvent(self._numeric_traits),
            _events.LostCountsEvent(self._numeric_traits),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_swabian_tags<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )
