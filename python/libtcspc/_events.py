# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import contextlib
import contextvars
import dataclasses
import hashlib
import re
from abc import ABC, abstractmethod
from collections.abc import Iterable, Iterator, Mapping, Sequence
from typing import TypeAlias, final

import numpy as np
import numpy.typing as npt
from typing_extensions import override

from . import _cpp_utils
from ._cpp_utils import (
    _const,
    _CppClassScopeDefs,
    _CppExpression,
    _CppFunctionScopeDefs,
    _CppIdentifier,
    _CppNamespaceScopeDefs,
    _CppTypeName,
    _identifier_from_string,
    _nominal,
    _quote_string,
    _TypeIdentity,
)
from ._numeric_traits import NumericTraits

_referenced_events: contextvars.ContextVar[dict[str, str] | None] = (
    contextvars.ContextVar("_referenced_events", default=None)
)


@contextlib.contextmanager
def _collecting_referenced_events() -> Iterator[dict[str, str]]:
    registry: dict[str, str] = {}
    token = _referenced_events.set(registry)
    try:
        yield registry
    finally:
        _referenced_events.reset(token)


# Process-wide registry of every custom event struct definition ever produced.
# Keyed by the content-addressed struct name, so duplicates collapse harmlessly
# (like `_known_traits_definitions`). Used to resolve custom event names in
# `_same_type_by_compile` test compiles.
_known_event_definitions: dict[str, str] = {}

_CPP_IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_EVENT_NAME_PATTERN = re.compile(r"\bce_[A-Za-z0-9_]*_[0-9a-f]{16}\b")


def _event_definitions_referenced_by(typenames: Iterable[str]) -> list[str]:
    seen: dict[str, str] = {}
    for typename in typenames:
        for name in _EVENT_NAME_PATTERN.findall(typename):
            if name in _known_event_definitions and name not in seen:
                seen[name] = _known_event_definitions[name]
    return list(seen.values())


def _field_py_kind(cpp_type: _CppTypeName) -> type:
    if cpp_type in ("double", "float"):
        return float
    if cpp_type == "std::string":
        return str
    return int  # NumericTraits member types and std::size_t are integral


_FieldValue: TypeAlias = (
    bytes | int | float | str | np.ndarray | tuple["EventInstance", ...]
)


@dataclasses.dataclass(frozen=True)
class _ScalarField:
    name: str
    cpp_type: _CppTypeName

    def py_kind(self) -> type:  # int, float, or str
        return _field_py_kind(self.cpp_type)

    def coerce(
        self,
        value: int | float | str | npt.ArrayLike | Sequence["EventInstance"],
        owner: str,
    ) -> _FieldValue:
        kind = self.py_kind()
        if kind is int:
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(
                    f"{owner}: field {self.name!r} "
                    f"must be an int, not {type(value).__name__}"
                )
            return int(value)
        if kind is float:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise TypeError(
                    f"{owner}: field {self.name!r} "
                    f"must be a float, not {type(value).__name__}"
                )
            return float(value)
        if not isinstance(value, str):
            raise TypeError(
                f"{owner}: field {self.name!r} "
                f"must be a str, not {type(value).__name__}"
            )
        return value

    def wrapper_binding(self, event_cpp: _CppTypeName) -> str:
        return f'\n    .def_rw("{self.name}", &{event_cpp}::{self.name})'

    def output_init_expr(self) -> str:
        return f"event.{self.name}"

    def cpp_initializer(self, value: _FieldValue) -> str:
        kind = self.py_kind()
        if kind is str:
            assert isinstance(value, str)
            return f".{self.name} = {self.cpp_type}{{{_quote_string(value)}}}"
        if kind is float:
            return f".{self.name} = static_cast<{self.cpp_type}>({value!r})"
        assert isinstance(value, int)
        return f".{self.name} = static_cast<{self.cpp_type}>({value})"


@dataclasses.dataclass(frozen=True)
class _BucketField:
    name: str
    element_cpp_type: _CppTypeName
    dtype: np.dtype

    def coerce(
        self,
        value: int | float | str | npt.ArrayLike | Sequence["EventInstance"],
        owner: str,
    ) -> _FieldValue:
        if isinstance(value, np.ndarray):
            if value.dtype != self.dtype and not np.can_cast(
                value.dtype, self.dtype, casting="safe"
            ):
                raise TypeError(
                    f"{owner}: field {self.name!r} given dtype "
                    f"{value.dtype} cannot be safely cast to {self.dtype}"
                )
            arr = value.astype(self.dtype)  # Always copies.
        else:
            try:
                arr = np.array(value, dtype=self.dtype)
            except (TypeError, ValueError, OverflowError) as e:
                raise TypeError(
                    f"{owner}: field {self.name!r} must be convertible to "
                    f"a NumPy array of dtype {self.dtype} ({e})"
                ) from e
        if arr.ndim != 1:
            raise TypeError(
                f"{owner}: field {self.name!r} "
                f"must be 1-dimensional, got {arr.ndim} dimension(s)"
            )
        arr.setflags(write=False)
        return arr

    def wrapper_binding(self, event_cpp: _CppTypeName) -> str:
        # The getter is a read-only zero-copy view of the bucket held by the
        # wrapper (the wrapper Python object is the ndarray owner). The setter
        # copies into newly owned storage (value semantics). The dummy pointer
        # substituted for a default-constructed (empty) bucket's nullptr
        # data() is never dereferenced (the array has 0 elements).
        elem = self.element_cpp_type
        return f"""
    .def_prop_rw("{self.name}",
        [](nanobind::handle_t<{event_cpp}> self_py) {{
            auto &self = nanobind::cast<{event_cpp} &>(self_py);
            auto const *ptr = self.{self.name}.data();
            if (ptr == nullptr)
                ptr = reinterpret_cast<{elem} const *>(&self);
            return nanobind::ndarray<{elem} const, nanobind::numpy>(
                ptr, {{self.{self.name}.size()}}, self_py).cast();
        }},
        []({event_cpp} &self,
           nanobind::ndarray<{elem} const, nanobind::ndim<1>,
                             nanobind::c_contig,
                             nanobind::device::cpu> const &arr) {{
            auto bkt = tcspc::new_delete_bucket_source<{elem}>::create()
                           ->bucket_of_size(arr.size());
            std::copy_n(arr.data(), arr.size(), bkt.begin());
            self.{self.name} = std::move(bkt);
        }})"""

    def output_init_expr(self) -> str:
        return f"share_or_copy_bucket(event.{self.name})"

    def cpp_initializer(self, value: _FieldValue) -> str:
        # Unreachable: callers reject bucket-carrying event types up front
        # (see EventInstance._cpp_expression); defined so the union type
        # checks.
        raise TypeError(
            f"bucket field {self.name!r} cannot be embedded in compiled code"
        )


@dataclasses.dataclass(frozen=True)
class _ArrayField:
    name: str
    element_type: "EventType"
    count: int

    def coerce(
        self,
        value: int | float | str | npt.ArrayLike | Sequence["EventInstance"],
        owner: str,
    ) -> _FieldValue:
        try:
            items = list(value)  # type: ignore[arg-type]
        except TypeError:
            raise TypeError(
                f"{owner}: field {self.name!r} must be a sequence of "
                f"{self.count} EventInstance(s), "
                f"not {type(value).__name__}"
            ) from None
        if len(items) != self.count:
            raise TypeError(
                f"{owner}: field {self.name!r} must have exactly "
                f"{self.count} element(s), got {len(items)}"
            )
        result: list[EventInstance] = []
        for i, item in enumerate(items):
            if not isinstance(item, EventInstance):
                raise TypeError(
                    f"{owner}: field {self.name!r} element {i} must be an "
                    f"EventInstance, not {type(item).__name__}"
                )
            if item._event_type != self.element_type:
                raise TypeError(
                    f"{owner}: field {self.name!r} element {i} has event type "
                    f"{type(item._event_type).__name__}, expected "
                    f"{type(self.element_type).__name__}"
                )
            result.append(item)
        return tuple(result)

    def wrapper_binding(self, event_cpp: _CppTypeName) -> str:
        # `event_cpp` is the array-wrapper struct (see
        # ArrayEventType._cpp_wrapper_struct_def), which holds the std::array in
        # a member named `value`. Expose `elements` as a settable property: the
        # getter returns a list of copies of the element wrappers; the setter
        # assigns a sequence of exactly `count` element wrappers.
        elem = self.element_type._cpp_type_name()
        return f"""
    .def_prop_rw("{self.name}",
        []({event_cpp} const &self) {{
            nanobind::list lst;
            for (auto const &e : self.value)
                lst.append(nanobind::cast(e, nanobind::rv_policy::copy));
            return lst;
        }},
        []({event_cpp} &self, nanobind::sequence seq) {{
            std::size_t i = 0;
            for (auto h : seq) {{
                if (i >= self.value.size())
                    throw std::out_of_range(
                        "{self.name}: expected {self.count} element(s)");
                self.value[i++] = nanobind::cast<{elem}>(h);
            }}
            if (i != self.value.size())
                throw std::out_of_range(
                    "{self.name}: expected {self.count} element(s)");
        }})"""

    def output_init_expr(self) -> str:
        # Unreachable: array events have no bucket fields, so the simple
        # nanobind::cast(event) output path is used (see
        # EventType._cpp_output_handlers).
        raise TypeError(
            f"array field {self.name!r} does not use the bucket output path"
        )

    def cpp_initializer(self, value: _FieldValue) -> str:
        assert isinstance(value, tuple)
        return ", ".join(e._cpp_expression() for e in value)


@dataclasses.dataclass(frozen=True)
class _RawBytesField:
    name: str
    size: int  # exact record byte count, for eager validation

    def coerce(
        self,
        value: int | float | str | npt.ArrayLike | Sequence["EventInstance"],
        owner: str,
    ) -> _FieldValue:
        # Reject int/str explicitly: bytes(int) would silently fabricate
        # `int` zero bytes; bytes(str) is a TypeError but give a clear message.
        if isinstance(value, (int, str)):
            raise TypeError(
                f"{owner}: field {self.name!r} must be bytes-like of "
                f"{self.size} byte(s), not {type(value).__name__}"
            )
        try:
            b = bytes(value)  # type: ignore[arg-type]
        except (TypeError, ValueError) as e:
            raise TypeError(
                f"{owner}: field {self.name!r} must be bytes-like ({e})"
            ) from e
        if len(b) != self.size:
            raise TypeError(
                f"{owner}: field {self.name!r} must be exactly "
                f"{self.size} byte(s), got {len(b)}"
            )
        return b

    def wrapper_binding(self, event_cpp: _CppTypeName) -> str:
        # Getter returns a copy as Python bytes; setter validates length and
        # copies in. `self.bytes.size()` is the authoritative record size.
        return f"""
    .def_prop_rw("{self.name}",
        []({event_cpp} const &self) {{
            return nanobind::bytes(
                reinterpret_cast<char const *>(self.{self.name}.data()),
                self.{self.name}.size());
        }},
        []({event_cpp} &self, nanobind::bytes const &b) {{
            if (b.size() != self.{self.name}.size())
                throw std::invalid_argument(
                    "{self.name}: wrong number of bytes for this event type");
            std::copy_n(reinterpret_cast<std::byte const *>(b.c_str()),
                        self.{self.name}.size(), self.{self.name}.begin());
        }})"""

    def output_init_expr(self) -> str:
        # Unreachable: raw-bytes events have no bucket fields, so the simple
        # nanobind::cast(event) output path is used (mirror _ArrayField).
        raise TypeError(
            f"raw-bytes field {self.name!r} does not use the bucket output path"
        )

    def cpp_initializer(self, value: _FieldValue) -> str:
        assert isinstance(value, bytes)
        elems = ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in value)
        return f".{self.name} = {{{{{elems}}}}}"


_Field: TypeAlias = _ScalarField | _BucketField | _ArrayField | _RawBytesField


def _field_values_equal(v: _FieldValue, w: _FieldValue) -> bool:
    if isinstance(v, np.ndarray) and isinstance(w, np.ndarray):
        return bool(v.dtype == w.dtype and np.array_equal(v, w))
    if isinstance(v, np.ndarray) or isinstance(w, np.ndarray):
        return False
    if isinstance(v, tuple) and isinstance(w, tuple):
        return len(v) == len(w) and all(
            a == b for a, b in zip(v, w, strict=False)
        )
    if isinstance(v, tuple) or isinstance(w, tuple):
        return False
    return bool(v == w)


class EventType(ABC):
    """Opaque marker for the type of events carried on a graph edge.

    An `EventType` instance is a type tag, not a data carrier. Each concrete
    subclass corresponds to a C++ event type in ``namespace tcspc``; an
    instance describes a particular instantiation of that C++ type (for
    example, parameterised by `NumericTraits`) and is used by the codegen layer
    to determine the C++ types flowing on each graph edge.

    ``EventType`` itself is abstract; instantiate one of its concrete
    subclasses. To declare a simple marker event of your own, use
    `CustomEvent`.
    """

    @abstractmethod
    def _cpp_type_name(self) -> _CppTypeName: ...

    def _type_identity(self) -> _TypeIdentity:
        """Structural identity used for fast type comparison.

        The default is *opaque* (``head=None``): comparisons fall back to the
        compile-based ground truth. Subclasses that emit a canonically spelled
        non-alias class (template) override this to enable no-compile
        comparison.
        """
        return _TypeIdentity(self._cpp_type_name(), head=None, args=())

    def __repr__(self) -> str:
        return f"<{type(self).__name__}({self._cpp_type_name()})>"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, EventType) and _cpp_utils._same_type(
            self._type_identity(), other._type_identity()
        )

    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return _CppClassScopeDefs(f"""\
        void handle({self._cpp_type_name()} const &event) {{
            {downstream}.handle(event);
        }}
        """)

    def _cpp_input_handler_param_type(self) -> _CppTypeName:
        """C++ parameter type of the handle() generated by
        `_cpp_input_handler` (used to disambiguate the overload set when
        binding the processor's handle to Python)."""
        return self._cpp_type_name()

    def _supports_value(self) -> bool:
        return self._field_schema() is not None

    def _has_bucket_fields(self) -> bool:
        for f in self._field_schema() or ():
            if isinstance(f, _BucketField):
                return True
            if (
                isinstance(f, _ArrayField)
                and f.element_type._has_bucket_fields()
            ):
                return True
        return False

    def _value_dependency_event_types(self) -> list["EventType"]:
        """Event types whose value wrappers this type's wrapper depends on.

        An array event's wrapper exposes its elements as wrappers of the
        element type, so the element type must also be bound. The default is
        empty.
        """
        return []

    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        schema = self._field_schema()
        if schema is None:
            raise TypeError(
                f"event type {type(self).__name__} ({self._cpp_type_name()}) "
                "cannot be delivered to a Python sink."
            )
        cpp = self._cpp_type_name()
        if not any(isinstance(f, _BucketField) for f in schema):
            return _CppClassScopeDefs(f"""\
            void handle({cpp} const &event) {{
                nanobind::gil_scoped_acquire held;
                {pysink}->handle(nanobind::cast(event));
            }}
            """)
        # For bucket-carrying events, share Python-buffer-backed bucket
        # storage (zero-copy) or deep-copy, before acquiring the GIL; an
        # rvalue event moves into the wrapper wholesale, preserving storage.
        inits = "".join(
            f"\n                .{f.name} = {f.output_init_expr()},"
            for f in schema
        )
        return _CppClassScopeDefs(f"""\
        void handle({cpp} const &event) {{
            {cpp} copy{{{inits}
            }};
            nanobind::gil_scoped_acquire held;
            {pysink}->handle(nanobind::cast(std::move(copy)));
        }}

        void handle({cpp} &&event) {{
            nanobind::gil_scoped_acquire held;
            {pysink}->handle(nanobind::cast(std::move(event)));
        }}
        """)

    def _cpp_wrapper_struct_def(self) -> _CppNamespaceScopeDefs | None:
        """Namespace-scope C++ struct backing the Python value wrapper.

        The default is ``None``: the wrapper binds the event's C++ type
        directly. Event types whose C++ type cannot be bound as a nanobind
        ``class_`` (for example ``std::array``, which has an STL type caster)
        override this to provide a distinct backing struct.
        """
        return None

    def _cpp_wrapper_class_name(self) -> _CppIdentifier:
        return _CppIdentifier(
            f"event_wrapper__{_identifier_from_string(self._cpp_type_name())}"
        )

    def _cpp_wrapper_class_def(
        self, module_var: _CppIdentifier
    ) -> _CppFunctionScopeDefs:
        cpp = self._cpp_type_name()
        name = self._cpp_wrapper_class_name()
        defs = "".join(
            f.wrapper_binding(cpp) for f in self._field_schema() or ()
        )
        return _CppFunctionScopeDefs(
            f'nanobind::class_<{cpp}>({module_var}, "{name}", '
            f"nanobind::is_final())\n    .def(nanobind::init<>())"
            f"{defs};\n"
        )

    def _field_schema(self) -> list[_Field] | None:
        """Field descriptor schema for constructing event values.

        Event types that support value construction override this. The default
        returns ``None``, marking the type as non-constructible from Python.
        """
        return None

    def value(
        self,
        **fields: int
        | float
        | str
        | npt.ArrayLike
        | Sequence["EventInstance"],
    ) -> "EventInstance":
        """Construct a concrete event value of this type.

        Parameters
        ----------
        **fields : int or float or str or array-like
            The value of each field of the event (see the event type's
            ``Notes``). All fields must be given, as concrete ints, floats, or
            strings matching the type of each field. A bucket field takes a
            1-D array-like; it is stored as a read-only copy. A NumPy array is
            accepted if its dtype matches the field's dtype exactly or can be
            safely cast to it; other array-likes (such as lists of ints) are
            converted (and value-checked) via ``numpy.array``.

        Returns
        -------
        EventInstance
            The concrete event value, carrying this `EventType`.
        """
        return EventInstance(self, fields)


class EventInstance:
    """A concrete event value: an `EventType` plus its field values.

    Unlike an `EventType` (which is only a type tag), an `EventInstance`
    represents an actual event value with concrete field contents. Construct
    one via :py:meth:`EventType.value`, for example
    ``DetectionEvent(nt).value(abstime=42, channel=1)``.

    Used as the event inserted by `Prepend`/`Append`, as input to
    `ExecutionContext.handle`, and as the value delivered to a `PySink`. Field
    values are concrete ints, floats, strings, or (for bucket fields)
    read-only NumPy arrays; read them as attributes (for example,
    ``event.abstime``). Runtime-supplied event values (`Param`) are not yet
    supported.

    Instances of bucket-carrying event types (such as `HistogramEvent`)
    cannot be inserted with `Prepend`/`Append` (use
    :py:meth:`ExecutionContext.handle` to send them at run time) and are not
    hashable.

    See Also
    --------
    :py:meth:`EventType.value`
        The factory used to construct an `EventInstance`.
    """

    def __init__(
        self,
        event_type: EventType,
        fields: Mapping[
            str, int | float | str | npt.ArrayLike | Sequence["EventInstance"]
        ],
    ) -> None:
        schema = event_type._field_schema()
        if schema is None:
            raise TypeError(
                f"event type {type(event_type).__name__} "
                f"({event_type._cpp_type_name()}) does not support "
                "constructing event values"
            )
        names = [f.name for f in schema]
        missing = [n for n in names if n not in fields]
        extra = [k for k in fields if k not in names]
        if missing or extra:
            problems = []
            if missing:
                problems.append(f"missing field(s) {missing}")
            if extra:
                problems.append(f"unexpected field(s) {extra}")
            raise TypeError(
                f"{type(event_type).__name__}.value(): "
                f"{'; '.join(problems)} (expected {names})"
            )
        owner = f"{type(event_type).__name__}.value()"
        self._event_type = event_type
        self._fields = {
            f.name: f.coerce(fields[f.name], owner) for f in schema
        }

    @classmethod
    def _from_wrapper(
        cls, event_type: EventType, fields: dict[str, _FieldValue]
    ) -> "EventInstance":
        # Trusted: fields come from a generated wrapper (correct names and
        # types; arrays already read-only). Skips validation and copying to
        # preserve zero-copy delivery.
        inst = object.__new__(cls)
        inst._event_type = event_type
        inst._fields = fields
        return inst

    def __getattr__(self, name: str) -> _FieldValue:
        # __getattr__ is only called when normal lookup fails, so it never
        # shadows real attributes/methods. Guard against recursion when
        # _fields is not yet set (e.g. during unpickling or copy probing).
        try:
            fields = object.__getattribute__(self, "_fields")
        except AttributeError:
            raise AttributeError(name) from None
        try:
            return fields[name]
        except KeyError:
            raise AttributeError(
                f"{type(self).__name__!r} object (event type "
                f"{type(self._event_type).__name__}) has no field {name!r}"
            ) from None

    def __dir__(self) -> list[str]:
        return [*super().__dir__(), *self._fields]

    def _sole_array_field(self) -> "_ArrayField | None":
        schema = self._event_type._field_schema() or ()
        if len(schema) == 1 and isinstance(schema[0], _ArrayField):
            return schema[0]
        return None

    def __len__(self) -> int:
        f = self._sole_array_field()
        if f is None:
            raise TypeError(
                f"{type(self._event_type).__name__} value has no length"
            )
        value = self._fields[f.name]
        assert isinstance(value, tuple)
        return len(value)

    def __getitem__(self, index: int) -> "EventInstance":
        f = self._sole_array_field()
        if f is None:
            raise TypeError(
                f"{type(self._event_type).__name__} value is not subscriptable"
            )
        value = self._fields[f.name]
        assert isinstance(value, tuple)
        return value[index]

    def _cpp_expression(self) -> _CppExpression:
        if self._event_type._has_bucket_fields():
            raise TypeError(
                f"{type(self._event_type).__name__} value carries a bucket "
                "field, so it cannot be embedded in compiled code (and is "
                "therefore not supported by Prepend or Append); send it at "
                "run time using ExecutionContext.handle() instead"
            )
        ctype = self._event_type._cpp_type_name()
        parts = [
            f.cpp_initializer(self._fields[f.name])
            for f in self._event_type._field_schema() or ()
        ]
        return _CppExpression(f"{ctype}{{{', '.join(parts)}}}")

    def __eq__(self, other: object) -> bool:
        if (
            not isinstance(other, EventInstance)
            or self._event_type != other._event_type
            or self._fields.keys() != other._fields.keys()
        ):
            return False
        return all(
            _field_values_equal(v, other._fields[n])
            for n, v in self._fields.items()
        )

    def __hash__(self) -> int:
        if self._event_type._has_bucket_fields():
            raise TypeError(
                f"unhashable: {type(self._event_type).__name__} value with "
                "array (bucket) fields"
            )
        return hash(
            (self._event_type._cpp_type_name(), tuple(self._fields.items()))
        )

    def __repr__(self) -> str:
        # str(bytes) renders the raw-bytes field as b'...', which is intended.
        args = ", ".join(
            f"{n}={v}"  # type: ignore[str-bytes-safe]
            for n, v in self._fields.items()
        )
        return f"{type(self._event_type).__name__}(...).value({args})"


class _RawDeviceEventType(EventType):
    """Base for raw vendor device events: a single fixed-size byte record."""

    _record_byte_count: int  # set by each concrete subclass

    @override
    def _field_schema(self) -> list[_Field]:
        return [_RawBytesField("bytes", self._record_byte_count)]


class CustomEvent(EventType):
    """A user-defined event type.

    Use this to declare a simple event type of your own, for use as a timing
    marker or other signal in a processing graph (for example, pixel/line/frame
    clock edges or control signals such as histogram reset).

    Only two shapes are supported: an *empty* event (no fields), and a
    *timestamped* event carrying a single ``abstime`` field (selected with
    ``abstime=True``). The latter is compatible with the processors that drive
    or match on timing markers (for example, `Generate`, `CheckAlternating`,
    `ClusterBinIncrements`, `Route`, and `Match`).

    Parameters
    ----------
    name : str
        A name for the event. Must be a valid C++ identifier. It is what the
        struct's ``operator<<`` prints. Re-declaring the same ``name`` with a
        different shape or ``traits`` yields a *distinct* event type rather than
        an error; declaring it identically yields an equivalent type.
    abstime : bool, optional
        If ``True``, the event carries a single ``abstime`` field; if ``False``
        (the default), the event is empty.
    traits : NumericTraits or None, optional
        The numeric traits determining the type of the ``abstime`` field.
        Required if (and only if) ``abstime`` is ``True``.

    Notes
    -----
    The emitted C++ struct provides a defaulted ``operator==`` and an
    ``operator<<`` that prints the event name (and, for the timestamped form,
    the ``abstime`` value).

    Examples
    --------
    >>> reset = CustomEvent("reset_event")
    >>> nt = NumericTraits()
    >>> pixel_start = CustomEvent("pixel_start_event", abstime=True, traits=nt)
    >>> pixel_start.value(abstime=0)  # doctest: +SKIP
    """

    def __init__(
        self,
        name: str,
        *,
        abstime: bool = False,
        traits: NumericTraits | None = None,
    ) -> None:
        if not _CPP_IDENTIFIER_PATTERN.fullmatch(name):
            raise ValueError(
                f"CustomEvent name {name!r} is not a valid C++ identifier"
            )
        if abstime and traits is None:
            raise TypeError(
                "CustomEvent: traits is required when abstime=True"
            )
        if not abstime and traits is not None:
            raise TypeError(
                "CustomEvent: traits must not be given when abstime=False"
            )
        self._readable_name = name
        self._abstime = abstime
        self._traits = traits

        # Content-address the struct name so that distinct shapes/traits sharing
        # a readable name become distinct C++ types (and identical declarations
        # collapse). The owned `ce_` prefix mirrors NumericTraits' `nt_<hash>`
        # and lets the reference scanner anchor on both ends.
        traits_id = "" if traits is None else traits._cpp_type_name()
        canonical = f"{name}|abstime={abstime}|traits={traits_id}"
        digest = hashlib.sha256(canonical.encode()).hexdigest()[:16]
        struct_name = f"ce_{name}_{digest}"
        self._struct_name = _CppTypeName(struct_name)

        if abstime:
            self._definition = (
                f"struct {struct_name} {{\n"
                f"    {traits_id}::abstime_type abstime;\n"
                f"    friend auto operator==({struct_name} const &lhs, "
                f"{struct_name} const &rhs) -> bool = default;\n"
                f"    friend auto operator<<(std::ostream &s, "
                f"{struct_name} const &e) -> std::ostream & {{\n"
                f'        return s << "{name}{{abstime=" << e.abstime << "}}";\n'
                f"    }}\n"
                f"}};"
            )
        else:
            self._definition = (
                f"struct {struct_name} {{\n"
                f"    friend auto operator==({struct_name} const &, "
                f"{struct_name} const &) -> bool = default;\n"
                f"    friend auto operator<<(std::ostream &s, "
                f"{struct_name} const &) -> std::ostream & {{\n"
                f'        return s << "{name}";\n'
                f"    }}\n"
                f"}};"
            )

        _known_event_definitions.setdefault(struct_name, self._definition)

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        registry = _referenced_events.get()
        if registry is not None:
            registry[str(self._struct_name)] = self._definition
        if self._traits is not None:
            # Ensure the trait struct is collected too: the event's abstime
            # member type references it.
            self._traits._cpp_type_name()
        return self._struct_name

    @override
    def _type_identity(self) -> _TypeIdentity:
        # Content-addressed leaf struct: distinct names are distinct types.
        return _nominal(self._struct_name)

    def __repr__(self) -> str:
        return f"<CustomEvent({self._readable_name})>"

    @override
    def _field_schema(self) -> list[_Field]:
        if not self._abstime:
            return []
        assert self._traits is not None
        nt = self._traits._cpp_type_name()
        return [_ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type"))]


class BucketEvent(EventType):
    """Event carrying a contiguous array of elements of another event type.

    Emitted by sources and batching processors (for example,
    `ReadBinaryStream`, `Batch`, and `Acquire`) to transport bulk data
    through the graph with zero-copy semantics where possible.

    Parameters
    ----------
    element_type : EventType
        The event type of the elements stored in the bucket.

    Notes
    -----
    The corresponding C++ event is ``tcspc::bucket<T>``, a movable handle
    to a contiguous storage region. Moving a bucket transfers ownership
    of the storage; copying allocates and copies the data.

    This (writable) event type cannot be used to feed bulk data into a graph
    from Python, because sole ownership of a Python buffer cannot be
    guaranteed. Use the read-only `ConstBucketEvent` as a graph input instead.

    See Also
    --------
    :cpp:`tcspc::bucket`
        The underlying C++ bucket type.
    :py:obj:`ConstBucketEvent`
        The read-only counterpart (``tcspc::bucket<T const>``); use this to
        push data into a graph from Python.
    """

    def __init__(self, element_type: EventType) -> None:
        self._element_type = element_type

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bucket<{self._element_type._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::bucket", self._element_type._type_identity())

    @override
    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        elem_cpp_type = self._element_type._cpp_type_name()
        # We take ownership of the bucket if we can; otherwise we make a copy;
        # in all cases we emit a writable ndarray that owns the memory. Buckets
        # backed by a Python buffer (py_buffer_shared_storage) are emitted as a
        # zero-copy view co-owning the original Python object; shared (const)
        # views are emitted as a read-only zero-copy view.
        return _CppClassScopeDefs(f"""\
        void emit_span_copy(std::span<{elem_cpp_type} const> spn) {{
            using elem_type = {elem_cpp_type};
            auto *buf = new elem_type[spn.size()];
            std::copy(spn.begin(), spn.end(), buf);
            nanobind::gil_scoped_acquire held;
            auto deleter = nanobind::capsule(buf,
                [](void *p) noexcept {{ delete[] static_cast<elem_type *>(p); }});
            {pysink}->handle(nanobind::ndarray<elem_type, nanobind::numpy>(
                buf, {{spn.size()}}, deleter).cast());

        }}

        void handle(tcspc::bucket<{elem_cpp_type} const> const &event) {{
            using elem_type = {elem_cpp_type};
            if (event.check_storage_type<py_buffer_shared_storage>()) {{
                auto const n = event.size();
                auto const *ptr = event.data();
                auto const &storage =
                    event.storage<py_buffer_shared_storage>();
                nanobind::gil_scoped_acquire held;
                nanobind::object owner = storage.ref->obj;
                {pysink}->handle(
                    nanobind::ndarray<elem_type const, nanobind::numpy>(
                        ptr, {{n}}, owner).cast());
            }} else {{
                emit_span_copy(
                    std::span<elem_type const>(event.begin(), event.end()));
            }}
        }}

        void handle(tcspc::bucket<{elem_cpp_type}> const &event) {{
            emit_span_copy(
                std::span<{elem_cpp_type} const>(event.begin(), event.end()));
        }}

        void handle(tcspc::bucket<{elem_cpp_type}> &&event) {{
            using elem_type = {elem_cpp_type};
            if (event.check_storage_type<py_buffer_shared_storage>()) {{
                auto const n = event.size();
                auto *const ptr = event.data();
                auto storage =
                    event.extract_storage<py_buffer_shared_storage>();
                nanobind::gil_scoped_acquire held;
                nanobind::object owner = storage.ref->obj;
                {pysink}->handle(
                    nanobind::ndarray<elem_type, nanobind::numpy>(
                        ptr, {{n}}, owner).cast());
            }} else {{
                using bkt_type = tcspc::bucket<elem_type>;
                auto *bkt = new bkt_type(std::move(event));
                nanobind::gil_scoped_acquire held;
                auto deleter = nanobind::capsule(bkt,
                    [](void *p) noexcept {{ delete static_cast<bkt_type *>(p); }});
                {pysink}->handle(nanobind::ndarray<elem_type, nanobind::numpy>(
                    bkt->data(), {{bkt->size()}}, deleter).cast());
            }}
        }}
        """)

    def element_event_type(self) -> EventType:
        return self._element_type


class ConstBucketEvent(EventType):
    """Event carrying a read-only contiguous array of elements of another event type.

    Emitted by sources that deliver real-time, read-only views of partial
    buckets — for example, the ``live`` output of `AcquireFullBuckets`. The
    view co-owns its backing storage and must not be modified.

    Parameters
    ----------
    element_type : EventType
        The event type of the elements stored in the bucket.

    Notes
    -----
    The corresponding C++ event is ``tcspc::bucket<T const>``, a movable handle
    to a contiguous storage region that is read-only and may be shared with its
    owner. This is a distinct type from `BucketEvent` (``tcspc::bucket<T>``).

    This is the event type to use when feeding bulk data into a graph from
    Python (for example, as the input to `CopyToBuckets` or
    `CopyToFullBuckets`): a numpy array or other buffer-protocol object pushed
    into a graph input declared as `ConstBucketEvent` is wrapped zero-copy as a
    read-only bucket. Pushing data in as a `BucketEvent` (writable) is not
    supported, because sole ownership of a Python buffer cannot be guaranteed.

    See Also
    --------
    :cpp:`tcspc::bucket`
        The underlying C++ bucket type.
    :py:obj:`BucketEvent`
        The writable counterpart (``tcspc::bucket<T>``).
    """

    def __init__(self, element_type: EventType) -> None:
        self._element_type = element_type

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bucket<{self._element_type._cpp_type_name()} const>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::bucket", _const(self._element_type._type_identity())
        )

    @override
    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        elem_cpp_type = self._element_type._cpp_type_name()
        return _CppClassScopeDefs(f"""\
        void handle({self._cpp_input_handler_param_type()} const &event) {{
            auto const spn =
                std::span<{elem_cpp_type} const>(event.data(), event.size());
            {downstream}.handle(tcspc::ad_hoc_bucket(spn));
        }}
        """)

    @override
    def _cpp_input_handler_param_type(self) -> _CppTypeName:
        elem_cpp_type = self._element_type._cpp_type_name()
        return _CppTypeName(
            f"nanobind::ndarray<{elem_cpp_type} const, nanobind::device::cpu, nanobind::c_contig>"
        )

    @override
    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        elem_cpp_type = self._element_type._cpp_type_name()
        # A read-only (const) bucket is delivered as a read-only ndarray. A
        # bucket backed by a Python buffer (py_buffer_shared_storage) is emitted
        # as a zero-copy view co-owning the original Python object; otherwise a
        # read-only copy is made.
        return _CppClassScopeDefs(f"""\
        void emit_span_copy(std::span<{elem_cpp_type} const> spn) {{
            using elem_type = {elem_cpp_type};
            auto *buf = new elem_type[spn.size()];
            std::copy(spn.begin(), spn.end(), buf);
            nanobind::gil_scoped_acquire held;
            auto deleter = nanobind::capsule(buf,
                [](void *p) noexcept {{ delete[] static_cast<elem_type *>(p); }});
            {pysink}->handle(
                nanobind::ndarray<elem_type const, nanobind::numpy>(
                    buf, {{spn.size()}}, deleter).cast());
        }}

        void handle(tcspc::bucket<{elem_cpp_type} const> const &event) {{
            using elem_type = {elem_cpp_type};
            if (event.check_storage_type<py_buffer_shared_storage>()) {{
                auto const n = event.size();
                auto const *ptr = event.data();
                auto const &storage =
                    event.storage<py_buffer_shared_storage>();
                nanobind::gil_scoped_acquire held;
                nanobind::object owner = storage.ref->obj;
                {pysink}->handle(
                    nanobind::ndarray<elem_type const, nanobind::numpy>(
                        ptr, {{n}}, owner).cast());
            }} else {{
                emit_span_copy(
                    std::span<elem_type const>(event.begin(), event.end()));
            }}
        }}
        """)

    def element_event_type(self) -> EventType:
        return self._element_type


@final
class VariantEvent(EventType):
    """Event holding one of several event types, as a variant (tagged union).

    This is an advanced event type, used to carry events of more than one type
    through a stage that handles only a single type — most importantly to
    buffer a heterogeneous stream. `Multiplex` wraps the listed event types
    into a `VariantEvent`; `Demultiplex` unwraps them again.

    Parameters
    ----------
    *event_types : EventType
        The member event types of the variant. At least one is required. The
        order is *significant*: it is part of the type, and `Multiplex` and
        `Demultiplex` preserve it.

    Notes
    -----
    The corresponding C++ event is
    ``tcspc::variant_event<tcspc::type_list<...>>``.

    A `VariantEvent` is a type tag only: it cannot be constructed as a value
    (no :py:meth:`~EventType.value`) and cannot be delivered to a Python sink.
    To inspect the carried events from Python, restore the individual event
    types with `Demultiplex` first.

    See Also
    --------
    :cpp:`tcspc::variant_event`
        The underlying C++ event type.
    :py:obj:`Multiplex`
        Wrap the listed event types into a `VariantEvent`.
    :py:obj:`Demultiplex`
        Restore the individual event types from a `VariantEvent`.
    """

    def __init__(self, *event_types: EventType) -> None:
        if not event_types:
            raise ValueError("VariantEvent requires at least one event type")
        self._event_types = tuple(event_types)

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        inner = ", ".join(t._cpp_type_name() for t in self._event_types)
        return _CppTypeName(f"tcspc::variant_event<tcspc::type_list<{inner}>>")

    @override
    def _type_identity(self) -> _TypeIdentity:
        type_list_id = _nominal(
            "tcspc::type_list",
            *(t._type_identity() for t in self._event_types),
        )
        return _nominal("tcspc::variant_event", type_list_id)


class ArrayEventType(EventType):
    """Event holding a fixed-size array of elements of another event type.

    The corresponding C++ event is ``std::array<T, N>``, where ``T`` is the
    element type and ``N`` is the (compile-time) element count. Use this for
    events that group a fixed number of structured elements, such as a pair of
    detections (`DetectionPairEvent`), or for future N-fold correlation.

    Parameters
    ----------
    element_type : EventType
        The event type of the array elements. It must support value
        construction (see :py:meth:`EventType.value`).
    count : int
        The number of elements in the array. Must be at least 1.

    Notes
    -----
    A value of this type is constructed with the single field ``elements``,
    given as a sequence of exactly ``count`` `EventInstance` values of
    ``element_type``::

        ArrayEventType(DetectionEvent(nt), 2).value(elements=[start, stop])

    When delivered to a Python sink (or read back), ``elements`` is a tuple of
    `EventInstance` values of ``element_type``. For convenience, an
    `EventInstance` of this type also supports ``len(...)`` and indexing
    (``inst[i]``), which return the elements directly.

    Whether values can be embedded in compiled code (and used with
    `Prepend`/`Append`) and whether they are hashable follow from the element
    type: an array of scalar-only elements is embeddable and hashable; an array
    of bucket-carrying elements is neither.

    See Also
    --------
    :py:obj:`DetectionPairEvent`
        A specialization for a pair of detections.
    """

    def __init__(self, element_type: EventType, count: int) -> None:
        if count < 1:
            raise ValueError("ArrayEventType count must be at least 1")
        if not element_type._supports_value():
            raise ValueError(
                "ArrayEventType element_type must support value construction"
            )
        self._element = element_type
        self._count = count

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"std::array<{self._element._cpp_type_name()}, {self._count}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "std::array", self._element._type_identity(), str(self._count)
        )

    @override
    def _field_schema(self) -> list[_Field]:
        return [_ArrayField("elements", self._element, self._count)]

    @override
    def _value_dependency_event_types(self) -> list[EventType]:
        return [self._element]

    # An array event's C++ type is a std::array, which cannot be bound directly
    # as a nanobind class_: <nanobind/stl/array.h> (included unconditionally,
    # and needed elsewhere -- e.g. the sinks parameter) provides a
    # type_caster<std::array<...>> partial specialization that takes precedence
    # over any class_ registration. So nanobind::cast() of a std::array yields
    # a Python list (each element cast in turn), not an instance of our wrapper
    # class, which would defeat the type-based dispatch used to map a delivered
    # wrapper back to its EventType. We therefore bind a distinct backing
    # struct that simply holds the std::array in a member named `value`; the
    # wrapper class binds this struct, and the input/output handlers convert
    # between it and the bare std::array that flows through the graph.

    def _wrapper_struct_name(self) -> _CppTypeName:
        return _CppTypeName(
            "array_event_wrapper__"
            + _identifier_from_string(self._cpp_type_name())
        )

    @override
    def _cpp_wrapper_struct_def(self) -> _CppNamespaceScopeDefs:
        return _CppNamespaceScopeDefs(
            f"struct {self._wrapper_struct_name()} {{\n"
            f"    {self._cpp_type_name()} value;\n"
            f"}};"
        )

    @override
    def _cpp_wrapper_class_def(
        self, module_var: _CppIdentifier
    ) -> _CppFunctionScopeDefs:
        wrapper = self._wrapper_struct_name()
        name = self._cpp_wrapper_class_name()
        defs = "".join(
            f.wrapper_binding(wrapper) for f in self._field_schema()
        )
        return _CppFunctionScopeDefs(
            f'nanobind::class_<{wrapper}>({module_var}, "{name}", '
            f"nanobind::is_final())\n    .def(nanobind::init<>())"
            f"{defs};\n"
        )

    @override
    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        cpp = self._cpp_type_name()
        wrapper = self._wrapper_struct_name()
        return _CppClassScopeDefs(f"""\
        void handle({cpp} const &event) {{
            nanobind::gil_scoped_acquire held;
            {pysink}->handle(nanobind::cast({wrapper}{{event}}));
        }}
        """)

    @override
    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return _CppClassScopeDefs(f"""\
        void handle({self._wrapper_struct_name()} const &event) {{
            {downstream}.handle(event.value);
        }}
        """)

    @override
    def _cpp_input_handler_param_type(self) -> _CppTypeName:
        return self._wrapper_struct_name()


class _ByteEvent(EventType):
    """Internal placeholder for C++ ``std::byte``.

    Used as the element type of `BucketEvent` for byte-level processors
    (`ViewAsBytes`, `BatchFromBytes`, `UnbatchFromBytes`). Not exposed
    in the public API.
    """

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("std::byte")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("std::byte")


class _TraitsMemberEvent(EventType):
    """Internal placeholder for a `NumericTraits` member type.

    Used as the element type of `BucketEvent` for scalar bucket payloads
    whose element type is a numeric-traits member (for example
    ``bin_index_type`` or ``bin_type``). Not exposed in the public API.
    """

    def __init__(self, numeric_traits: NumericTraits, member: str) -> None:
        self._numeric_traits = numeric_traits
        self._member = member

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"{self._numeric_traits._cpp_type_name()}::{self._member}"
        )


# Note: C++ event wrappers are ordered alphabetically without regard to the C++
# header in which they are defined.


class BeginLostIntervalEvent(EventType):
    """Event marking the beginning of an interval in which counts were lost.

    The interval must be closed with a subsequent `EndLostIntervalEvent`.
    Unlike `DataLostEvent`, the ``abstime`` remains consistent before,
    during, and after the lost interval; detected but un-time-tagged
    counts during the interval can be reported as `LostCountsEvent`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``.

    See Also
    --------
    :cpp:`tcspc::begin_lost_interval_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::begin_lost_interval_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::begin_lost_interval_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [_ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type"))]


class BHSPC600_256chEvent(_RawDeviceEventType):
    """Raw 32-bit FIFO record from Becker & Hickl SPC-600/630 in 256-channel mode.

    Typically appears upstream of `DecodeBHSPC600_256ch`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes
    (``BHSPC600_256chEvent().value(bytes=b"....")``) and read them back as
    ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::bh_spc600_256ch_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::bh_spc600_256ch_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::bh_spc600_256ch_event")


class BHSPC600_4096chEvent(_RawDeviceEventType):
    """Raw 48-bit FIFO record from Becker & Hickl SPC-600/630 in 4096-channel mode.

    Typically appears upstream of `DecodeBHSPC600_4096ch`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 6> bytes`` holding the raw record. Construct an
    instance from raw bytes
    (``BHSPC600_4096chEvent().value(bytes=b"......")``) and read them back as
    ``inst.bytes`` (a 6-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::bh_spc600_4096ch_event`
        The underlying C++ event type.
    """

    _record_byte_count = 6

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::bh_spc600_4096ch_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::bh_spc600_4096ch_event")


class BHSPCEvent(_RawDeviceEventType):
    """Raw 32-bit FIFO record from Becker & Hickl SPC hardware.

    Represents one record as produced by the SPC FIFO mode (does not cover
    SPC-600/630 or TDC-family devices, which use different formats).
    Typically appears upstream of `DecodeBHSPC`, which interprets the
    record and emits the appropriate detection, marker, or
    bookkeeping events.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``BHSPCEvent().value(bytes=b"....")``) and read
    them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::bh_spc_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::bh_spc_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::bh_spc_event")


class BinIncrementClusterEvent(EventType):
    """Event carrying a cluster of histogram bin increments.

    Emitted by `ClusterBinIncrements` and `UnbatchBinIncrementClusters`,
    and consumed by `ScanHistograms` and `BatchBinIncrementClusters`. It
    carries a bucket of bin indices.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``bin_indices``, a
    ``tcspc::bucket`` of ``bin_index_type``.

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``bin_indices`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::bin_increment_cluster_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bin_increment_cluster_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::bin_increment_cluster_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _BucketField(
                "bin_indices",
                _CppTypeName(f"{nt}::bin_index_type"),
                self._numeric_traits._member_dtype("bin_index_type"),
            )
        ]


class BinIncrementEvent(EventType):
    """Event representing a single increment of a histogram bin.

    Emitted by `MapToBins` and consumed by `Histogram` and
    `ClusterBinIncrements`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``bin_index`` (of
    ``bin_index_type``).

    See Also
    --------
    :cpp:`tcspc::bin_increment_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bin_increment_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::bin_increment_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("bin_index", _CppTypeName(f"{nt}::bin_index_type"))
        ]


class BulkCountsEvent(EventType):
    """Event representing detection counts from a non-time-tagging counter.

    Used for devices that emit counter values at some interval (often
    based on an internal or external clock signal) rather than tagging
    individual detections.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime``, ``channel``, and
    ``count``.

    See Also
    --------
    :cpp:`tcspc::bulk_counts_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bulk_counts_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::bulk_counts_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("channel", _CppTypeName(f"{nt}::channel_type")),
            _ScalarField("count", _CppTypeName(f"{nt}::count_type")),
        ]


class ConcludingHistogramArrayEvent(EventType):
    """Event carrying the final accumulated histogram array of a round.

    Emitted by `ScanHistograms` (when concluding events are enabled) once
    per round, before each reset.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``data_bucket``, a
    ``tcspc::bucket`` of ``bin_type``.

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``data_bucket`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::concluding_histogram_array_event`
        The underlying C++ event type.
    :py:obj:`ExtractBucket`
        Extract the carried bucket as a bare NumPy array (without the event
        wrapper).
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::concluding_histogram_array_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::concluding_histogram_array_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _BucketField(
                "data_bucket",
                _CppTypeName(f"{nt}::bin_type"),
                self._numeric_traits._member_dtype("bin_type"),
            )
        ]

    def _data_bucket_element_event_type(self) -> EventType:
        return _TraitsMemberEvent(self._numeric_traits, "bin_type")


class ConcludingHistogramEvent(EventType):
    """Event carrying the final accumulated histogram of a round.

    Emitted by `Histogram` (when concluding events are enabled) upon each
    reset.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``data_bucket``, a
    ``tcspc::bucket`` of ``bin_type``.

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``data_bucket`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::concluding_histogram_event`
        The underlying C++ event type.
    :py:obj:`ExtractBucket`
        Extract the carried bucket as a bare NumPy array (without the event
        wrapper).
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::concluding_histogram_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::concluding_histogram_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _BucketField(
                "data_bucket",
                _CppTypeName(f"{nt}::bin_type"),
                self._numeric_traits._member_dtype("bin_type"),
            )
        ]

    def _data_bucket_element_event_type(self) -> EventType:
        return _TraitsMemberEvent(self._numeric_traits, "bin_type")


class DataLostEvent(EventType):
    """Event indicating that the data source detected a buffer overflow.

    Emitted when an upstream data source (FIFO, DMA, or similar) detected
    that one or more events were lost before they could be delivered.
    Subsequent events may therefore be missing and the ``abstime`` field
    may have skipped time.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``. The source
    should continue to produce overflow notifications for subsequent
    occurrences; cancelling the stream in response to data loss is the
    responsibility of a downstream processor.

    See Also
    --------
    :cpp:`tcspc::data_lost_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::data_lost_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::data_lost_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [_ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type"))]


class DatapointEvent(EventType):
    """Event representing a scalar datapoint mapped from another event.

    Emitted by `MapToDatapoints` and consumed by `MapToBins`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``value`` (of
    ``datapoint_type``).

    See Also
    --------
    :cpp:`tcspc::datapoint_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::datapoint_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::datapoint_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [_ScalarField("value", _CppTypeName(f"{nt}::datapoint_type"))]


class DetectionEvent(EventType):
    """Event representing a detected count without time correlation.

    Like `TimeCorrelatedDetectionEvent` but without a ``difftime`` field;
    used when the device does not report a microtime (or it has been
    discarded). The PicoQuant T2 decoders and `RemoveTimeCorrelation`
    emit this type.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime`` and ``channel``.

    See Also
    --------
    :cpp:`tcspc::detection_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::detection_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::detection_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("channel", _CppTypeName(f"{nt}::channel_type")),
        ]


class DetectionPairEvent(EventType):
    """Event representing a pair of detections (a start and a stop).

    Emitted by the `PairAll`, `PairOne`, `PairAllBetween`, and
    `PairOneBetween` processors, and consumed by the `TimeCorrelateAtStart`,
    `TimeCorrelateAtStop`, `TimeCorrelateAtMidpoint`, and
    `TimeCorrelateAtFraction` processors.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event is ``std::array<tcspc::detection_event, 2>``;
    element 0 is the start detection and element 1 is the stop detection.

    A value is constructed with the single field ``elements``, given as a
    sequence of exactly two `DetectionEvent` `EventInstance` values (the start
    and the stop)::

        DetectionPairEvent(nt).value(elements=[start, stop])

    When delivered to a Python sink (or read back), ``elements`` is a tuple of
    two `DetectionEvent` `EventInstance` values; an `EventInstance` of this type
    also supports ``len(...)`` and indexing. Values are embeddable in compiled
    code (usable with `Prepend`/`Append`) and hashable.

    See Also
    --------
    :py:obj:`DetectionEvent`
        The element type of the pair.
    :py:obj:`ArrayEventType`
        The generic fixed-size-array event type this specializes.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._array = ArrayEventType(DetectionEvent(self._numeric_traits), 2)

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return self._array._cpp_type_name()

    @override
    def _type_identity(self) -> _TypeIdentity:
        return self._array._type_identity()

    @override
    def _field_schema(self) -> list[_Field]:
        return self._array._field_schema()

    @override
    def _value_dependency_event_types(self) -> list[EventType]:
        return self._array._value_dependency_event_types()

    @override
    def _cpp_wrapper_struct_def(self) -> _CppNamespaceScopeDefs | None:
        return self._array._cpp_wrapper_struct_def()

    @override
    def _cpp_wrapper_class_def(
        self, module_var: _CppIdentifier
    ) -> _CppFunctionScopeDefs:
        return self._array._cpp_wrapper_class_def(module_var)

    @override
    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return self._array._cpp_output_handlers(pysink)

    @override
    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return self._array._cpp_input_handler(downstream)

    @override
    def _cpp_input_handler_param_type(self) -> _CppTypeName:
        return self._array._cpp_input_handler_param_type()


class EndLostIntervalEvent(EventType):
    """Event marking the end of an interval in which counts were lost.

    Closes an interval opened by `BeginLostIntervalEvent`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``.

    See Also
    --------
    :cpp:`tcspc::end_lost_interval_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::end_lost_interval_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::end_lost_interval_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [_ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type"))]


class HistogramArrayEvent(EventType):
    """Event carrying a histogram array at the end of a completed scan.

    Emitted by `ScanHistograms` upon completion of each scan.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``data_bucket``, a
    ``tcspc::bucket`` of ``bin_type``.

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``data_bucket`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::histogram_array_event`
        The underlying C++ event type.
    :py:obj:`ExtractBucket`
        Extract the carried bucket as a bare NumPy array (without the event
        wrapper).
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::histogram_array_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::histogram_array_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _BucketField(
                "data_bucket",
                _CppTypeName(f"{nt}::bin_type"),
                self._numeric_traits._member_dtype("bin_type"),
            )
        ]

    def _data_bucket_element_event_type(self) -> EventType:
        return _TraitsMemberEvent(self._numeric_traits, "bin_type")


class HistogramArrayProgressEvent(EventType):
    """Event reporting progress while accumulating a histogram array.

    Emitted by `ScanHistograms` on each bin-increment cluster, carrying a
    view of the entire histogram array updated so far.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the fields ``valid_size`` and
    ``data_bucket`` (a ``tcspc::bucket`` of ``bin_type``).

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``data_bucket`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::histogram_array_progress_event`
        The underlying C++ event type.
    :py:obj:`ExtractBucket`
        Extract the carried bucket as a bare NumPy array (without the event
        wrapper).
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::histogram_array_progress_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::histogram_array_progress_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        # Order must match the C++ member declaration order (the generated
        # output handler uses designated initializers).
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("valid_size", _CppTypeName("std::size_t")),
            _BucketField(
                "data_bucket",
                _CppTypeName(f"{nt}::bin_type"),
                self._numeric_traits._member_dtype("bin_type"),
            ),
        ]

    def _data_bucket_element_event_type(self) -> EventType:
        return _TraitsMemberEvent(self._numeric_traits, "bin_type")


class HistogramEvent(EventType):
    """Event carrying a snapshot of a histogram after a bin increment.

    Emitted by `Histogram` on each incoming bin increment, carrying a view
    of the current histogram.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``data_bucket``, a
    ``tcspc::bucket`` of ``bin_type``.

    Can be delivered directly to a Python sink as an `EventInstance` whose
    ``data_bucket`` attribute reads as a read-only NumPy array, and
    constructed via :py:meth:`~EventType.value` (the bucket field accepts a
    1-D array-like). Not supported by `Prepend`/`Append`; use
    :py:meth:`ExecutionContext.handle` to send a value at run time.

    See Also
    --------
    :cpp:`tcspc::histogram_event`
        The underlying C++ event type.
    :py:obj:`ExtractBucket`
        Extract the carried bucket as a bare NumPy array (without the event
        wrapper).
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::histogram_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::histogram_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _BucketField(
                "data_bucket",
                _CppTypeName(f"{nt}::bin_type"),
                self._numeric_traits._member_dtype("bin_type"),
            )
        ]

    def _data_bucket_element_event_type(self) -> EventType:
        return _TraitsMemberEvent(self._numeric_traits, "bin_type")


class LostCountsEvent(EventType):
    """Event indicating a number of counts that could not be time-tagged.

    Should only occur between a `BeginLostIntervalEvent` and the
    corresponding `EndLostIntervalEvent`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime``, ``channel``, and
    ``count``.

    See Also
    --------
    :cpp:`tcspc::lost_counts_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::lost_counts_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::lost_counts_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("channel", _CppTypeName(f"{nt}::channel_type")),
            _ScalarField("count", _CppTypeName(f"{nt}::count_type")),
        ]


class MarkerEvent(EventType):
    """Event representing a timing marker or external trigger.

    Used for frame, line, and pixel markers in scanning microscopy, and
    for other external trigger signals that the TCSPC hardware records
    alongside detections.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime`` and ``channel``.
    When the hardware reports multiple simultaneous markers, one event
    is emitted per channel sharing the same ``abstime``; the ordering
    among simultaneous markers is unspecified. The marker channel
    numbering may or may not share a namespace with detection channels,
    depending on the device.

    See Also
    --------
    :cpp:`tcspc::marker_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::marker_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::marker_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("channel", _CppTypeName(f"{nt}::channel_type")),
        ]


class PeriodicSequenceModelEvent(EventType):
    """Event describing a fitted periodic sequence of timing events.

    Emitted by `FitPeriodicSequences` and consumed and re-emitted by
    `RetimePeriodicSequences`; also consumed by
    `ExtrapolatePeriodicSequences` and `AddCountToPeriodicSequences`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the fields ``abstime``, ``delay``, and
    ``interval``.

    See Also
    --------
    :cpp:`tcspc::periodic_sequence_model_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::periodic_sequence_model_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::periodic_sequence_model_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("delay", _CppTypeName("double")),
            _ScalarField("interval", _CppTypeName("double")),
        ]


class PQT2GenericEvent(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant T2 (Generic) format.

    Format used by HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp
    330 devices. Typically appears upstream of `DecodePQT2Generic`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT2GenericEvent().value(bytes=b"....")``) and
    read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt2_generic_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt2_generic_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt2_generic_event")


class PQT2HydraHarpV1Event(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant HydraHarp V1 T2 format.

    Typically appears upstream of `DecodePQT2HydraHarpV1`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT2HydraHarpV1Event().value(bytes=b"....")``)
    and read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt2_hydraharpv1_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt2_hydraharpv1_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt2_hydraharpv1_event")


class PQT2PicoHarp300Event(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant PicoHarp 300 T2 format.

    Typically appears upstream of `DecodePQT2PicoHarp300`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT2PicoHarp300Event().value(bytes=b"....")``)
    and read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt2_picoharp300_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt2_picoharp300_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt2_picoharp300_event")


class PQT3GenericEvent(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant T3 (Generic) format.

    Format used by HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp
    330 devices. Typically appears upstream of `DecodePQT3Generic`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT3GenericEvent().value(bytes=b"....")``) and
    read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt3_generic_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt3_generic_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt3_generic_event")


class PQT3HydraHarpV1Event(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant HydraHarp V1 T3 format.

    Typically appears upstream of `DecodePQT3HydraHarpV1`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT3HydraHarpV1Event().value(bytes=b"....")``)
    and read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt3_hydraharpv1_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt3_hydraharpv1_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt3_hydraharpv1_event")


class PQT3PicoHarp300Event(_RawDeviceEventType):
    """Raw 32-bit FIFO record for PicoQuant PicoHarp 300 T3 format.

    Typically appears upstream of `DecodePQT3PicoHarp300`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record. Construct an
    instance from raw bytes (``PQT3PicoHarp300Event().value(bytes=b"....")``)
    and read them back as ``inst.bytes`` (a 4-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::pqt3_picoharp300_event`
        The underlying C++ event type.
    """

    _record_byte_count = 4

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::pqt3_picoharp300_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::pqt3_picoharp300_event")


class RealLinearTimingEvent(EventType):
    """Event describing a linear (repeating) sequence of timings.

    Emitted by `AddCountToPeriodicSequences`. Can be used as the trigger
    pattern for `Generate` with a dynamic linear timing generator.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the fields ``abstime``, ``delay``,
    ``interval``, and ``count``.

    See Also
    --------
    :cpp:`tcspc::real_linear_timing_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::real_linear_timing_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::real_linear_timing_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("delay", _CppTypeName("double")),
            _ScalarField("interval", _CppTypeName("double")),
            _ScalarField("count", _CppTypeName("std::size_t")),
        ]


class RealOneShotTimingEvent(EventType):
    """Event describing a single (one-shot) delayed timing.

    Emitted by `ExtrapolatePeriodicSequences`. Can be used as the trigger
    pattern for `Generate` with a dynamic one-shot timing generator.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the fields ``abstime`` and ``delay``.

    See Also
    --------
    :cpp:`tcspc::real_one_shot_timing_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::real_one_shot_timing_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::real_one_shot_timing_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("delay", _CppTypeName("double")),
        ]


class SwabianTagEvent(_RawDeviceEventType):
    """Raw 16-byte tag record from a Swabian Time Tagger.

    Has the same memory layout as the ``Tag`` struct in the Swabian
    Time Tagger C++ API and the format used by the Python ``CustomMeasurement``
    class. Typically appears upstream of `DecodeSwabianTags`.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 16> bytes`` holding the raw record. Construct an
    instance from raw bytes (a 16-byte ``SwabianTagEvent().value(bytes=...)``)
    and read them back as ``inst.bytes`` (a 16-byte ``bytes`` object).

    See Also
    --------
    :cpp:`tcspc::swabian_tag_event`
        The underlying C++ event type.
    """

    _record_byte_count = 16

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::swabian_tag_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::swabian_tag_event")


class TimeCorrelatedDetectionEvent(EventType):
    """The canonical TCSPC detection event.

    Represents a single detection (typically a photon) carrying both a
    macrotime (``abstime``) and a microtime (``difftime``, also known as
    nanotime), along with the detector channel. This is the central event
    type processed in applications such as FLIM and FCS.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime``, ``channel``, and
    ``difftime``.

    See Also
    --------
    :cpp:`tcspc::time_correlated_detection_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::time_correlated_detection_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::time_correlated_detection_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [
            _ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type")),
            _ScalarField("channel", _CppTypeName(f"{nt}::channel_type")),
            _ScalarField("difftime", _CppTypeName(f"{nt}::difftime_type")),
        ]


class TimeReachedEvent(EventType):
    """Keep-alive event indicating that the data source has reached a given time.

    Serves two purposes: (a) propagating time progression when there are
    no detections to send (including marking the end of a measurement),
    and (b) preventing long gaps in ``abstime`` that would otherwise
    stall time-sensitive processors.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `NumericTraits` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``. The emission
    frequency of these events can be tuned on the C++ side with
    ``tcspc::regulate_time_reached()``.

    See Also
    --------
    :cpp:`tcspc::time_reached_event`
        The underlying C++ event type.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::time_reached_event<{self._numeric_traits._cpp_type_name()}>"
        )

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal(
            "tcspc::time_reached_event",
            self._numeric_traits._type_identity(),
        )

    @override
    def _field_schema(self) -> list[_Field]:
        nt = self._numeric_traits._cpp_type_name()
        return [_ScalarField("abstime", _CppTypeName(f"{nt}::abstime_type"))]


class WarningEvent(EventType):
    """Event indicating a non-fatal, recoverable issue detected by a processor.

    Used to report problems that do not by themselves require stopping
    the stream, such as input format anomalies, dropped or out-of-order
    events, or other recoverable conditions.

    Notes
    -----
    The corresponding C++ event has the field ``message``. Producers of
    warnings should also pass any received warning events through, so
    multiple warning-emitting processors can be chained ahead of a
    single handler such as `Stop` or `StopWithError`.

    See Also
    --------
    :cpp:`tcspc::warning_event`
        The underlying C++ event type.
    :py:obj:`Stop`
        Convert warnings into a normal end-of-processing.
    :py:obj:`StopWithError`
        Convert warnings into a terminating error.
    """

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::warning_event")

    @override
    def _type_identity(self) -> _TypeIdentity:
        return _nominal("tcspc::warning_event")

    @override
    def _field_schema(self) -> list[_Field]:
        return [_ScalarField("message", _CppTypeName("std::string"))]
