/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace tcspc {

/**
 * \brief Short names for fixed-width integer types.
 *
 * \ingroup integers
 *
 * The types in this namespace are also available directly under the `tcspc`
 * namespace.
 */
namespace int_types {

/** \brief Short name for uint8_t. */
using u8 = std::uint8_t;

/** \brief Short name for uint16_t. */
using u16 = std::uint16_t;

/** \brief Short name for uint32_t. */
using u32 = std::uint32_t;

/** \brief Short name for uint64_t. */
using u64 = std::uint64_t;

/** \brief Short name for int8_t. */
using i8 = std::int8_t;

/** \brief Short name for int16_t. */
using i16 = std::int16_t;

/** \brief Short name for int32_t. */
using i32 = std::int32_t;

/** \brief Short name for int64_t. */
using i64 = std::int64_t;

} // namespace int_types

using namespace int_types;

} // namespace tcspc
