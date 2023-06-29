/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>

namespace tcspc {

/**
 * \brief Processor that forwards to a processor reference.
 *
 * This is an adapter to allow the use of non-movable processors, or when you
 * do not want to move a downstream processor into its upstream.
 *
 * \tparam Downstream downstream processor type
 */
template <typename Downstream> class ref_processor {
    Downstream *downstream; // Not null.

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * The caller is reponsible for ensuring that downstream is not moved or
     * destroyed during the lifetime of this ref_processor.
     *
     * \param downstream downstream processor
     */
    explicit ref_processor(Downstream &downstream) : downstream(&downstream) {}

    /** \brief Processor interface */
    template <typename AnyEvent>
    void handle_event(AnyEvent const &event) noexcept {
        downstream->handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr const &error) noexcept {
        downstream->handle_end(error);
    }
};

} // namespace tcspc
