/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "autocopy_span.hpp"
#include "buffer.hpp"
#include "span.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

// When editing this file, maintain partial symmetry with
// read_binary_stream.hpp.

namespace tcspc {

/**
 * \addtogroup streams
 *
 * \par Requirements for output streams
 * An output stream must be a movable (usually noncopyable) object with the
 * following member functions:
 * - <tt>auto is_error() noexcept -> bool;</tt>\n
 *   Return true if the stream is not available or the previous write operation
 *   resulted in an error. Not influenced by failure of \c tell().
 * - <tt>auto tell() noexcept -> std::optional<std::uint64_t>;</tt>\n
 *   Return the current stream position if supported by the stream, or \c
 *   std::nullopt.
 * - <tt>void write(tcspc::span<std::byte const> buffer) noexcept;</tt>\n
 *   Write the given bytes to the stream.
 */

namespace internal {

template <typename OStream> class ostream_output_stream {
    static_assert(std::is_base_of_v<std::ostream, OStream>);
    OStream stream;

  public:
    explicit ostream_output_stream(OStream &&stream)
        : stream(std::move(stream)) {}

    auto is_error() noexcept -> bool { return stream.fail(); }

    auto tell() noexcept -> std::optional<std::uint64_t> {
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

    auto operator=(cfile_output_stream &&rhs) noexcept
        -> cfile_output_stream & {
        if (should_close)
            std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
        fp = std::exchange(rhs.fp, nullptr);
        should_close = std::exchange(rhs.should_close, false);
        return *this;
    }

    ~cfile_output_stream() {
        if (should_close)
            std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
    }

    auto is_error() noexcept -> bool {
        return fp == nullptr || std::ferror(fp) != 0;
    }

    auto tell() noexcept -> std::optional<std::uint64_t> {
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
        if (fp == nullptr)
            return; // Error detected by calling is_error().
        std::fwrite(buffer.data(), 1, buffer.size(), fp);
    }
};

// For benchmarking only
inline auto unbuffered_binary_ofstream_output_stream(
    std::string const &filename, bool truncate = false, bool append = false) {
    std::ofstream stream;

    // Set to unbuffered.
    stream.rdbuf()->pubsetbuf(nullptr, 0);

    stream.open(filename, std::ios::binary | (truncate ? std::ios::trunc : 0) |
                              (append ? std::ios::ate : 0));
    return internal::ostream_output_stream(std::move(stream));
}

// For benchmarking only
inline auto binary_ofstream_output_stream(std::string const &filename,
                                          bool truncate = false,
                                          bool append = false) {
    std::ofstream stream;
    stream.open(filename, std::ios::binary | (truncate ? std::ios::trunc : 0) |
                              (append ? std::ios::ate : 0));
    return internal::ostream_output_stream(std::move(stream));
}

inline auto unbuffered_binary_cfile_output_stream(std::string const &filename,
                                                  bool truncate = false,
                                                  bool append = false) {
    char const *mode = truncate ? "wb" : (append ? "ab" : "wbx");
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), mode);
    if (fp != nullptr)
        std::setbuf(fp, nullptr);
    return internal::cfile_output_stream(fp, true);
}

// For benchmarking only
inline auto binary_cfile_output_stream(std::string const &filename,
                                       bool truncate = false,
                                       bool append = false) {
    char const *mode = truncate ? "wb" : (append ? "ab" : "wbx");
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), mode);
    return internal::cfile_output_stream(fp, true);
}

} // namespace internal

/**
 * \brief Create a binary output stream for the given file.
 *
 * \ingroup streams
 *
 * If both \e truncate and \e append are true, behave as if only \e truncate is
 * true. If neither are true, the file must not exist or the stream will be in
 * an error state. If the file cannot be opened, the stream will be in an error
 * state.
 *
 * \see write_binary_stream
 *
 * \param filename the filename
 *
 * \param truncate if true, overwrite existing file
 *
 * \param append if true, file will be appended to if it exists
 *
 * \return output stream
 */
inline auto binary_file_output_stream(std::string const &filename,
                                      bool truncate = false,
                                      bool append = false) {
    return internal::unbuffered_binary_cfile_output_stream(filename, truncate,
                                                           append);
}

/**
 * \brief Create an abstract output stream from an \c std::ostream instance.
 *
 * \ingroup streams
 *
 * The ostream is moved into the returned output stream and destroyed together,
 * so you cannot use this with an ostream that you do not own (such as \c
 * std::cout). For that, see \ref borrowed_cfile_output_stream (which works
 * with \c stdout).
 *
 * \see write_binary_stream
 *
 * \param stream an ostream (derived from \c std::ostream)
 *
 * \return output stream
 */
template <typename OStream>
inline auto ostream_output_stream(OStream &&stream) {
    static_assert(std::is_base_of_v<std::ostream, OStream>);
    return internal::ostream_output_stream(std::forward<OStream>(stream));
}

/**
 * \brief Create an abstract output stream from a C file pointer, taking
 * ownership.
 *
 * \ingroup streams
 *
 * The stream will use the C stdio functions, such as \c std::fwrite(). The
 * file pointer is closed when the stream is destroyed.
 *
 * The file pointer \e fp should have been opened in binary mode.
 *
 * If \e fp is null, the stream will always be in an error state.
 *
 * \see borrowed_cfile_output_stream
 * \see write_binary_stream
 *
 * \param fp a file pointer
 *
 * \return output stream
 */
inline auto owning_cfile_output_stream(std::FILE *fp) {
    return internal::cfile_output_stream(fp, true);
}

/**
 * \brief Create an abstract output stream from a non-owned C file pointer.
 *
 * \ingroup streams
 *
 * The stream will use the C stdio functions, such as \c std::fwrite(). The
 * file pointer is not closed when the stream is destroyed. The call is
 * responsible for ensuring that the file pointer will remain valid throughout
 * the lifetime of the returned output stream.
 *
 * The file pointer \e fp should have been opened in binary mode. (If using \c
 * stdout, use \c std::freopen() with a full filename on POSIX or \c _setmode()
 * with \c _O_BINARY on Windows (via \c _fileno()).)
 *
 * If \e fp is null, the stream will always be in an error state.
 *
 * \see owning_cfile_output_stream
 * \see write_binary_stream
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
    std::shared_ptr<object_pool<std::vector<std::byte>>> bufpool;
    std::size_t write_granularity;

    std::uint64_t total_bytes_written = 0;
    // If not null, buffer to use next, containing a partial event:
    std::shared_ptr<std::vector<std::byte>> buf;

  public:
    explicit write_binary_stream(
        OutputStream &&stream,
        std::shared_ptr<object_pool<std::vector<std::byte>>> buffer_pool,
        std::size_t write_granularity_bytes)
        : strm(std::move(stream)), bufpool(std::move(buffer_pool)),
          write_granularity(write_granularity_bytes) {
        assert(bufpool);
        assert(write_granularity > 0);
    }

    void handle(autocopy_span<std::byte> const &event) {
        handle_span(event.as_span());
    }

    void handle(autocopy_span<std::byte const> const &event) {
        handle_span(event.as_span());
    }

    void flush() {
        if (buf) {
            strm.write(span(*buf));
            buf.reset();
            if (strm.is_error())
                throw std::runtime_error("failed to write output");
        }
    }

  private:
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

        if ((buf || first_block_size < write_granularity) &&
            not event_span.empty()) {
            auto const leftover_bytes = buf ? buf->size() : 0;
            auto const buffer_size =
                std::min(leftover_bytes + event_span.size(), first_block_size);
            if (not buf)
                buf = bufpool->check_out();
            buf->reserve(write_granularity);
            buf->resize(buffer_size);
            auto const dest_span = span(*buf).subspan(leftover_bytes);
            auto const src_span = event_span.subspan(
                0, std::min(event_span.size(), dest_span.size()));
            std::copy(src_span.begin(), src_span.end(), dest_span.begin());
            if (buffer_size == first_block_size) {
                strm.write(span(*buf));
                buf.reset();
                if (strm.is_error())
                    throw std::runtime_error("failed to write output");
                total_bytes_written += buffer_size;
            }
            event_span = event_span.subspan(src_span.size());
        }

        auto const direct_write_size =
            event_span.size() / write_granularity * write_granularity;
        if (direct_write_size > 0) {
            strm.write(event_span.subspan(0, direct_write_size));
            if (strm.is_error())
                throw std::runtime_error("failed to write output");
            total_bytes_written += direct_write_size;
            event_span = event_span.subspan(direct_write_size);
        }

        if (not event_span.empty()) {
            buf = bufpool->check_out();
            buf->reserve(write_granularity);
            buf->resize(event_span.size());
            std::copy(event_span.begin(), event_span.end(), buf->begin());
        }
    }
};

} // namespace internal

/**
 * \brief Create a sink that writes bytes to a binary stream, such as a file.
 *
 * \ingroup processors-basic
 *
 * The stream is either libtcspc's output stream abstraction (see \ref streams)
 * or an iostreams \c std::ostream. In the latter case, it is wrapped using
 * \ref ostream_output_stream. (Use of iostreams is not recommended due to
 * often poor performance.)
 *
 * The processor receives data in the form of <tt>autocopy_span<std::byte
 * const></tt> (see \ref view_as_bytes). The bytes are written sequentially and
 * contiguously to the stream.
 *
 * For efficiency, data is written in batches of at least \e
 * write_granularity_bytes (except possibly at the beginning and end of the
 * stream).
 *
 * The \e write_granularity_bytes can be tuned for best performance. If too
 * small, writes may incur more overhead per byte written; if too large, CPU
 * caches may be polluted (if the event size and write granularity is such that
 * buffering is necessary). It is best to try different powers of 2 and
 * measure.
 *
 * \see read_binary_stream
 *
 * \tparam OutputStream output stream type
 *
 * \param stream the output stream (see \ref streams)
 *
 * \param buffer_pool object pool providing write buffers; must be able to
 * circulate at least 1 buffer to avoid deadlock; may not be used if all events
 * can be written directly
 *
 * \param write_granularity_bytes minimum size, in bytes, to write; all writes
 * (except possible the first and last ones) will be a multiple of this value
 *
 * \return write-binary-stream processor
 */
template <typename OutputStream>
auto write_binary_stream(
    OutputStream &&stream,
    std::shared_ptr<object_pool<std::vector<std::byte>>> buffer_pool,
    std::size_t write_granularity_bytes) {
    // Support direct passing of C++ iostreams stream.
    if constexpr (std::is_base_of_v<std::ostream, OutputStream>) {
        auto wrapped =
            ostream_output_stream(std::forward<OutputStream>(stream));
        return internal::write_binary_stream<decltype(wrapped)>(
            std::move(wrapped), std::move(buffer_pool),
            write_granularity_bytes);
    } else {
        return internal::write_binary_stream<OutputStream>(
            std::forward<OutputStream>(stream), std::move(buffer_pool),
            write_granularity_bytes);
    }
}

} // namespace tcspc
