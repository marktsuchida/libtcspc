/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace tcspc {

/**
 * \brief Exception type used to indicate processor-initiated non-error end of
 * processing.
 *
 * \ingroup exceptions
 *
 * End of processing can be initiated either by the data source (which should
 * then `flush()` the processors) or by any processor. When a processor
 * initiates a non-error end of processing, it does so by first `flush()`ing
 * the downstream processors and then throwing `end_of_processing`. The data
 * source should catch this exception and subsequently must not send events or
 * flush the processors.
 */
class end_of_processing final : public std::exception {
    std::string msg;

  public:
    /**
     * \brief Construct with status message.
     *
     * \param message the message, which should describe the reason for the end
     * of processing
     */
    explicit end_of_processing(std::string message)
        : msg(std::move(message)) {}

    /** \brief Implements std::exception interface. */
    [[nodiscard]] auto what() const noexcept -> char const * override {
        return msg.c_str();
    }
};

/**
 * \brief Exception type thrown to pumping thread when buffer source was
 * discontinued without reaching the point of flushing.
 *
 * \ingroup exceptions
 *
 * \see `tcspc::buffer_access::pump()`
 */
class source_halted final : public std::exception {
  public:
    /** \brief Implements std::exception interface. */
    [[nodiscard]] auto what() const noexcept -> char const * override {
        return "source halted without flushing";
    }
};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * \ingroup errors
 *
 * This error is raised when the `tcspc::error_on_overflow_t` policy was
 * requested and there was an overflow. It is also raised when
 * `tcspc::reset_on_overflow_t` was requested but a reset would result in an
 * infinite loop: in the case of `tcspc::histogram()` when maximum-per-bin set
 * to 0, or `tcspc::histogram_elementwise_accumulate()` when a single batch
 * contains enough increments to overflow a bin.
 */
class histogram_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace tcspc
