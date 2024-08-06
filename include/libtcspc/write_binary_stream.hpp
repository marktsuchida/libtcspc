/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "errors.hpp"
#include "introspect.hpp"
#include "span.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

// When editing this file, maintain partial symmetry with
// read_binary_stream.hpp.

namespace tcspc {

namespace internal {

class null_output_stream {
    std::uint64_t bytes_written = 0;

  public:
    static auto is_error() noexcept -> bool { return false; }
    [[nodiscard]] auto tell() const noexcept -> std::optional<std::uint64_t> {
        return bytes_written;
    }
    void write(span<std::byte const> buffer) noexcept {
        bytes_written += buffer.size();
    }
};

// We turn off ostream exceptions in the constructor.
// NOLINTBEGIN(bugprone-exception-escape)
template <typename OStream> class ostream_output_stream {
    static_assert(std::is_base_of_v<std::ostream, OStream>);
    OStream stream;

  public:
    explicit ostream_output_stream(OStream stream)
        : stream(std::move(stream)) {
        this->stream.exceptions(std::ios::goodbit);
    }

    auto is_error() noexcept -> bool { return stream.fail(); }

    [[nodiscard]] auto tell() noexcept -> std::optional<std::uint64_t> {
        if (stream.fail())
            return std::nullopt; // Do not affect flags.
        std::int64_t const pos = stream.tellp();
        if (pos >= 0)
            return std::uint64_t(pos);
        stream.clear();
        return std::nullopt;
    }

    void write(span<std::byte const> buffer) noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        stream.write(reinterpret_cast<char const *>(buffer.data()),
                     static_cast<std::streamsize>(buffer.size()));
    }
};
// NOLINTEND(bugprone-exception-escape)

class cfile_output_stream {
    std::FILE *fp;
    bool should_close;

  public:
    explicit cfile_output_stream(std::FILE *stream, bool close_on_destruction)
        : fp(stream), should_close(close_on_destruction && fp != nullptr) {}

    cfile_output_stream(cfile_output_stream const &) = delete;
    auto operator=(cfile_output_stream const &) = delete;

    cfile_output_stream(cfile_output_stream &&other) noexcept
        : fp(std::exchange(other.fp, nullptr)),
          should_close(std::exchange(other.should_close, false)) {}

    auto
    operator=(cfile_output_stream &&rhs) noexcept -> cfile_output_stream & {
        if (should_close)
            (void)std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
        fp = std::exchange(rhs.fp, nullptr);
        should_close = std::exchange(rhs.should_close, false);
        return *this;
    }

    ~cfile_output_stream() {
        if (should_close)
            (void)std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
    }

    auto is_error() noexcept -> bool {
        return fp == nullptr || std::ferror(fp) != 0;
    }

    [[nodiscard]] auto tell() noexcept -> std::optional<std::uint64_t> {
        if (fp == nullptr)
            return std::nullopt;
        std::int64_t pos =
#ifdef _WIN32
            ::_ftelli64(fp);
#else
            std::ftell(fp);
#endif
        if (pos >= 0)
            return std::uint64_t(pos);
        return std::nullopt;
    }

    void write(span<std::byte const> buffer) noexcept {
        // Errors are checked separately by is_error(); ignore here.
        if (fp == nullptr)
            return;
        (void)std::fwrite(buffer.data(), 1, buffer.size(), fp);
    }
};

// For benchmarking only
inline auto
unbuffered_binary_ofstream_output_stream(std::string const &filename,
                                         arg::truncate<bool> truncate,
                                         arg::append<bool> append) {
    std::ofstream stream;

    // Set to unbuffered.
    stream.rdbuf()->pubsetbuf(nullptr, 0);

    stream.open(filename,
                std::ios::binary |
                    (truncate.value ? std::ios::trunc : std::ios::openmode{}) |
                    (append.value ? std::ios::ate : std::ios::openmode{}));
    if (stream.fail())
        throw input_output_error("failed to open output file: " + filename);
    return internal::ostream_output_stream(std::move(stream));
}

// For benchmarking only
inline auto binary_ofstream_output_stream(std::string const &filename,
                                          arg::truncate<bool> truncate,
                                          arg::append<bool> append) {
    std::ofstream stream;
    stream.open(filename,
                std::ios::binary |
                    (truncate.value ? std::ios::trunc : std::ios::openmode{}) |
                    (append.value ? std::ios::ate : std::ios::openmode{}));
    if (stream.fail())
        throw input_output_error("failed to open output file: " + filename);
    return internal::ostream_output_stream(std::move(stream));
}

inline auto unbuffered_binary_cfile_output_stream(std::string const &filename,
                                                  arg::truncate<bool> truncate,
                                                  arg::append<bool> append) {
    char const *mode = [&] {
        if (truncate.value)
            return "wb";
        if (append.value)
            return "ab";
        return "wbx";
    }();
#ifdef _WIN32 // Avoid requiring _CRT_SECURE_NO_WARNINGS.
    std::FILE *fp{};
    (void)fopen_s(&fp, filename.c_str(), mode);
#else
    errno = 0; // ISO C does not require fopen to set errno on error.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), mode);
#endif
    if (fp == nullptr) {
        if (errno != 0)
            throw std::system_error(errno, std::generic_category());
        throw input_output_error("failed to open output file: " + filename);
    }
    if (std::setvbuf(fp, nullptr, _IONBF, 0) != 0)
        throw input_output_error(
            "failed to disable buffering for output file: " + filename);
    return internal::cfile_output_stream(fp, true);
}

// For benchmarking only
inline auto binary_cfile_output_stream(std::string const &filename,
                                       arg::truncate<bool> truncate,
                                       arg::append<bool> append) {
    char const *mode = [&] {
        if (truncate.value)
            return "wb";
        if (append.value)
            return "ab";
        return "wbx";
    }();
#ifdef _WIN32 // Avoid requiring _CRT_SECURE_NO_WARNINGS.
    std::FILE *fp{};
    (void)fopen_s(&fp, filename.c_str(), mode);
#else
    errno = 0; // ISO C does not require fopen to set errno on error.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), mode);
#endif
    if (fp == nullptr) {
        if (errno != 0)
            throw std::system_error(errno, std::generic_category());
        throw input_output_error("failed to open output file: " + filename);
    }
    return internal::cfile_output_stream(fp, true);
}

} // namespace internal

/**
 * \brief Create an output stream that discards all written bytes.
 *
 * \ingroup streams-output
 *
 * \return output stream
 */
inline auto null_output_stream() { return internal::null_output_stream(); }

/**
 * \brief Create a binary output stream for the given file.
 *
 * \ingroup streams-output
 *
 * If both \p truncate and \p append are true, behave as if only \p truncate is
 * true. If neither are true, the file must not exist or the stream will be in
 * an error state. If the file cannot be opened, the stream will be in an error
 * state.
 *
 * \param filename the filename
 *
 * \param truncate if true, overwrite existing file
 *
 * \param append if true, file will be appended to if it exists
 *
 * \return output stream
 */
inline auto
binary_file_output_stream(std::string const &filename,
                          arg::truncate<bool> truncate = arg::truncate{false},
                          arg::append<bool> append = arg::append{false}) {
    return internal::unbuffered_binary_cfile_output_stream(filename, truncate,
                                                           append);
}

/**
 * \brief Create an output stream from an `std::ostream` instance.
 *
 * \ingroup streams-output
 *
 * The ostream is moved into the returned output stream and destroyed together,
 * so you cannot use this with an ostream that you do not own (such as
 * `std::cout`). For that, see `tcspc::borrowed_cfile_output_stream` (which
 * works with `stdout`).
 *
 * \param stream an ostream (derived from `std::ostream`)
 *
 * \return output stream
 */
template <typename OStream>
inline auto ostream_output_stream(OStream &&stream) {
    static_assert(std::is_base_of_v<std::ostream, OStream>);
    return internal::ostream_output_stream(std::forward<OStream>(stream));
}

/**
 * \brief Create an output stream from a C file pointer, taking ownership.
 *
 * \ingroup streams-output
 *
 * The stream will use the C stdio functions, such as `std::fwrite()`. The
 * file pointer is closed when the stream is destroyed.
 *
 * The file pointer \p fp should have been opened in binary mode.
 *
 * If \p fp is null, the stream will always be in an error state.
 *
 * \see `tcspc::borrowed_cfile_output_stream`
 *
 * \param fp a file pointer
 *
 * \return output stream
 */
inline auto owning_cfile_output_stream(std::FILE *fp) {
    return internal::cfile_output_stream(fp, true);
}

/**
 * \brief Create an output stream from a non-owned C file pointer.
 *
 * \ingroup streams-output
 *
 * The stream will use the C stdio functions, such as `std::fwrite()`. The
 * file pointer is not closed when the stream is destroyed. The call is
 * responsible for ensuring that the file pointer will remain valid throughout
 * the lifetime of the returned output stream.
 *
 * The file pointer \p fp should have been opened in binary mode. (If using
 * `stdout`, use `std::freopen()` with a full filename on POSIX or `_setmode()`
 * with `_O_BINARY` on Windows (via `_fileno()`).)
 *
 * If \p fp is null, the stream will always be in an error state.
 *
 * \see `tcspc::owning_cfile_output_stream`
 *
 * \param fp a file pointer
 *
 * \return output stream
 */
inline auto borrowed_cfile_output_stream(std::FILE *fp) {
    return internal::cfile_output_stream(fp, false);
}

namespace internal {

template <typename OutputStream> class write_binary_stream {
    OutputStream strm;
    std::shared_ptr<bucket_source<std::byte>> bsource;
    std::size_t write_granularity;

    std::uint64_t total_bytes_written = 0;

    // If not empty, buffer to use next, containing a partial event:
    bucket<std::byte> buffer;
    std::size_t bytes_buffered = 0;

    void handle_span(span<std::byte const> event_span) {
        auto first_block_size = write_granularity;
        if (total_bytes_written == 0) {
            // Align second and subsequent writes to write_granularity if
            // current offset is available. This may or may not improve
            // write performance (when the write_granularity is a multiple
            // of the page size or block size), but shouldn't hurt.
            std::optional<std::uint64_t> pos = strm.tell();
            if (pos.has_value()) {
                first_block_size =
                    write_granularity - *pos % write_granularity;
            }
        }

        if (bytes_buffered > 0 || first_block_size < write_granularity) {
            auto const bytes_available =
                std::min(bytes_buffered + event_span.size(), first_block_size);
            if (buffer.empty())
                buffer = bsource->bucket_of_size(write_granularity);
            auto const dest_span =
                buffer.first(bytes_available).subspan(bytes_buffered);
            auto const src_span = event_span.first(
                std::min(event_span.size(), dest_span.size()));
            std::copy(src_span.begin(), src_span.end(), dest_span.begin());
            if (bytes_available == first_block_size) {
                strm.write(buffer.first(bytes_available));
                buffer = {};
                bytes_buffered = 0;
                if (strm.is_error())
                    throw input_output_error("failed to write output");
                total_bytes_written += bytes_available;
            } else {
                bytes_buffered = bytes_available;
            }
            event_span = event_span.subspan(src_span.size());
        }

        auto const direct_write_size =
            event_span.size() / write_granularity * write_granularity;
        if (direct_write_size > 0) {
            strm.write(event_span.first(direct_write_size));
            if (strm.is_error())
                throw input_output_error("failed to write output");
            total_bytes_written += direct_write_size;
            event_span = event_span.subspan(direct_write_size);
        }

        if (not event_span.empty()) {
            buffer = bsource->bucket_of_size(write_granularity);
            bytes_buffered = event_span.size();
            std::copy(event_span.begin(), event_span.end(), buffer.begin());
        }
    }

  public:
    explicit write_binary_stream(
        OutputStream stream,
        std::shared_ptr<bucket_source<std::byte>> buffer_provider,
        arg::granularity<std::size_t> granularity)
        : strm(std::move(stream)), bsource(std::move(buffer_provider)),
          write_granularity(granularity.value) {
        if (not bsource)
            throw std::invalid_argument(
                "write_binary_stream buffer_provider must not be null");
        if (write_granularity <= 0)
            throw std::invalid_argument(
                "write_binary_stream granularity must be positive");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "write_binary_stream");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    template <typename Span,
              typename = std::void_t<
                  decltype(span<std::byte const>(std::declval<Span>()))>>
    void handle(Span const &event) {
        handle_span(span<std::byte const>(event));
    }

    void flush() {
        if (bytes_buffered > 0) {
            strm.write(span(buffer).first(bytes_buffered));
            buffer = {};
            if (strm.is_error())
                throw input_output_error("failed to write output");
        }
    }
};

} // namespace internal

/**
 * \brief Create a sink that writes bytes to a binary stream, such as a file.
 *
 * \ingroup processors-io
 *
 * The stream is either libtcspc's output stream abstraction (see \ref
 * streams-output) or an iostreams `std::ostream`. In the latter case, it is
 * wrapped using `tcspc::ostream_output_stream()`. (Use of iostreams is not
 * recommended due to usually poor performance.)
 *
 * The processor receives data in the form of `tcspc::bucket<std::byte>` or
 * another type that can be explicitly converted to `tcspc::span<std::byte
 * const>` (see `tcspc::view_as_bytes()`). The bytes are written sequentially
 * and contiguously to the stream.
 *
 * For efficiency, data is written in batches whose size is a multiple of \p
 * granularity (except possibly at the beginning and end of the stream).
 *
 * The \p granularity can be tuned for best performance. If too small, writes
 * may incur more overhead per byte written; if too large, CPU caches may be
 * polluted (if the event size and write granularity is such that buffering is
 * necessary). It is best to try different powers of 2 and measure.
 *
 * If there is an error (either in this processor or upstream), an incomplete
 * file may be left (if the output stream was a regular file). Application
 * code, if it so desires, should delete this file after closing it (by
 * destroying the processor, if the file lifetime is tied to the output
 * stream).
 *
 * \see `tcspc::read_binary_stream()`
 *
 * \tparam OutputStream output stream type
 *
 * \param stream the output stream (see \ref streams-output)
 *
 * \param buffer_provider bucket source providing write buffers; must be able
 * to circulate at least 1 bucket without blocking; may not be used if all
 * events can be written directly
 *
 * \param granularity minimum size, in bytes, to write; all writes (except
 * possible the first and last ones, for alignment) will be a multiple of this
 * value
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bucket<std::byte>`, `tcspc::bucket<std::byte const>` or other
 *   contiguous container or span of `std::byte const`: write to the output
 *   stream; throw `tcspc::input_output_error` on stream write error
 * - Flush: write any buffered bytes to the stream; throw
 *   `tcspc::input_output_error` on stream write error
 */
template <typename OutputStream>
auto write_binary_stream(
    OutputStream &&stream,
    std::shared_ptr<bucket_source<std::byte>> buffer_provider,
    arg::granularity<std::size_t> granularity) {
    // Support direct passing of C++ iostreams stream.
    if constexpr (std::is_base_of_v<std::ostream, OutputStream>) {
        auto wrapped =
            ostream_output_stream(std::forward<OutputStream>(stream));
        return internal::write_binary_stream<decltype(wrapped)>(
            std::move(wrapped), std::move(buffer_provider), granularity);
    } else {
        return internal::write_binary_stream<OutputStream>(
            std::forward<OutputStream>(stream), std::move(buffer_provider),
            granularity);
    }
}

} // namespace tcspc
