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
 * \brief Error thrown upon an arithmetic overflow or underflow.
 *
 * \ingroup exceptions
 */
class arithmetic_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
    arithmetic_overflow_error() : std::runtime_error("arithmetic overflow") {}
};

/**
 * \brief Error thrown when buffer capacity has been exhausted.
 *
 * \ingroup errors
 */
class buffer_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error thrown when the data being processed does not meet
 * expectations.
 *
 * \ingroup errors
 */
class data_validation_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error thrown when a histogram bin overflows.
 *
 * \ingroup errors
 *
 * This error is thrown when the `tcspc::error_on_overflow_t` policy was
 * requested and there was an overflow. It is also thrown when
 * `tcspc::reset_on_overflow_t` was requested but a reset would result in an
 * infinite loop: in the case of `tcspc::histogram()` when maximum-per-bin set
 * to 0, or `tcspc::histogram_elementwise_accumulate()` when a single batch
 * contains enough increments to overflow a bin.
 */
class histogram_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error thrown when a file or stream could not be accessed.
 *
 * \ingroup errors
 *
 * This error strictly represents input/output errors, usually coming from the
 * operating system, such as inaibility to open a file or inability to read or
 * write bytes.
 *
 * It is not used for errors in the data contained in a file or stream.
 *
 * \note Some file/stream errors are reported as `std::system_error` when the
 * error code is available.
 */
class input_output_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error thrown when a fit to a model did not meet the desired criteria.
 *
 * \ingroup errors
 *
 * \see `tcspc::fit_periodic_sequences`
 */
class model_fit_error : public data_validation_error {
  public:
    using data_validation_error::data_validation_error;
};

/**
 * \brief Error thrown when requested to do so for testing purposes.
 *
 * \ingroup errors
 *
 * \see `tcspc::capture_output`, `tcspc::capture_output_checker`
 */
class test_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace tcspc
