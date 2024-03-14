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
 * the downstream processors and then throwing `end_processing`. The data
 * source should catch this exception and subsequently must not send events or
 * flush the processors.
 */
class end_processing final : public std::exception {
    std::string msg;

  public:
    /**
     * \brief Construct with status message.
     *
     * \param message the message, which should describe the reason for the end
     * of processing
     */
    explicit end_processing(std::string message) : msg(std::move(message)) {}

    /** \brief std::exception interface. */
    [[nodiscard]] auto what() const noexcept -> char const * override {
        return msg.c_str();
    }
};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * \ingroup exceptions
 *
 * This error is raised when the error_on_overflow strategy is requested and
 * there was an overflow. It is also raised when reset_on_overflow is requested
 * but a reset would result in an infinite loop: in the case of histogram if
 * maximum per bin set to 0, or accumulate_histograms if a single batch
 * contains enough increments to overflow a bin.
 */
class histogram_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error raised when histogram array cycle is incomplete.
 *
 * \ingroup exceptions
 *
 * All but the last cycle before a reset or end-of-stream must be complete for
 * processors computing histogram arrays. This exception is thrown if a
 * new-cycle event is received before the current cycle has had the expected
 * number of batches.
 */
class incomplete_array_cycle_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace tcspc
