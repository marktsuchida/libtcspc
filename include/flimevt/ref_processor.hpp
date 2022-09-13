/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>

namespace flimevt {

/**
 * \brief Processor that forwards to a processor reference.
 *
 * This is an adapter to allow the use of non-movable processors, or when you
 * do not want to move a downstream processor into its upstream.
 *
 * \tparam D downstream processor type
 */
template <typename D> class ref_processor {
    D &downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * The caller is reponsible for ensuring that downstream is not moved or
     * destroyed during the lifetime of this ref_processor.
     *
     * \param downstream downstream processor
     */
    explicit ref_processor(D &downstream) : downstream(downstream) {}

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace flimevt
