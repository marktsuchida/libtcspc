/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "buffer.hpp"
#include "span.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace tcspc {

namespace internal {

template <typename IStream> class istream_input_stream {
    static_assert(std::is_base_of_v<std::istream, IStream>);
    IStream stream;

  public:
    explicit istream_input_stream(IStream &&stream)
        : stream(std::move(stream)) {}

    auto is_error() noexcept -> bool {
        auto const flags = stream.rdstate();
        return ((flags & std::ios::failbit) || (flags & std::ios::badbit)) &&
               not(flags & std::ios::eofbit);
    }

    auto is_eof() noexcept -> bool { return stream.eof(); }

    auto is_good() noexcept -> bool { return stream.good(); }

    void clear() noexcept { stream.clear(); }

    auto tell() noexcept -> std::optional<std::uint64_t> {
        std::int64_t const pos = stream.tellg();
        if (pos < 0)
            return std::nullopt;
        return std::uint64_t(pos);
    }

    auto skip(std::uint64_t bytes) noexcept -> bool {
        if (stream.fail() ||
            bytes > std::numeric_limits<std::streamoff>::max())
            return false;
        stream.seekg(std::streamoff(bytes), std::ios::beg);
        return stream.good();
    }

    auto read(span<std::byte> buffer) noexcept -> std::uint64_t {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        stream.read(reinterpret_cast<char *>(buffer.data()),
                    static_cast<std::streamsize>(buffer.size()));
        return stream.gcount();
    }
};

class cfile_input_stream {
    std::FILE *fp;
    bool should_close;

  public:
    explicit cfile_input_stream(std::FILE *stream, bool close_on_destruction)
        : fp(stream), should_close(close_on_destruction && fp != nullptr) {}

    cfile_input_stream(cfile_input_stream const &) = delete;
    auto operator=(cfile_input_stream const &) = delete;

    cfile_input_stream(cfile_input_stream &&other) noexcept
        : fp(std::exchange(other.fp, nullptr)),
          should_close(std::exchange(other.should_close, false)) {}

    auto operator=(cfile_input_stream &&rhs) noexcept -> cfile_input_stream & {
        if (should_close)
            std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
        fp = std::exchange(rhs.fp, nullptr);
        should_close = std::exchange(rhs.should_close, false);
        return *this;
    }

    ~cfile_input_stream() {
        if (should_close)
            std::fclose(fp); // NOLINT(cppcoreguidelines-owning-memory)
    }

    auto is_error() noexcept -> bool {
        return fp == nullptr || std::ferror(fp) != 0;
    }

    auto is_eof() noexcept -> bool {
        return fp != nullptr && std::feof(fp) != 0;
    }

    auto is_good() noexcept -> bool {
        return fp != nullptr && std::ferror(fp) == 0 && std::feof(fp) == 0;
    }

    void clear() noexcept {
        if (fp != nullptr)
            std::clearerr(fp);
    }

    auto tell() noexcept -> std::optional<std::uint64_t> {
        if (fp == nullptr)
            return std::nullopt;
        std::int64_t pos = std::ftell(fp);
        if (pos >= 0)
            return std::uint64_t(pos);
#ifdef _WIN32
        pos = ::_ftelli64(fp);
        if (pos >= 0)
            return std::uint64_t(pos);
#endif
        return std::nullopt;
    }

    auto skip(std::uint64_t bytes) noexcept -> bool {
        if (fp == nullptr)
            return false;
        if (bytes > std::numeric_limits<long>::max()) {
#ifdef _WIN32
            if (bytes <= std::numeric_limits<__int64>::max())
                return ::_fseeki64(fp, __int64(bytes), SEEK_CUR) == 0;
#else
            return false;
#endif
        }
        return std::fseek(fp, long(bytes), SEEK_CUR) == 0;
    }

    auto read(span<std::byte> buffer) noexcept -> std::uint64_t {
        if (fp == nullptr)
            return 0;
        return std::fread(buffer.data(), 1, buffer.size(), fp);
    }
};

template <typename InputStream>
inline void skip_stream_bytes(InputStream &stream,
                              std::uint64_t bytes) noexcept {
    if (stream.is_good()) {
        if (not stream.skip(bytes)) {
            stream.clear();
            // Try instead reading and discarding up to 'start', to support
            // non-seekable streams (e.g., pipes).
            std::uint64_t bytes_discarded = 0;
            static constexpr std::streamsize bufsize = 65536;
            std::vector<std::byte> buf(bufsize);
            span<std::byte> const bufspan(buf);
            while (bytes_discarded < bytes) {
                auto read_size =
                    std::min<std::uint64_t>(bufsize, bytes - bytes_discarded);
                bytes_discarded += stream.read(bufspan.subspan(0, read_size));
                if (not stream.is_good())
                    break;
            }
        }
    }
}

// For files, we prefer cfile over ofstream (see binary_file_input_stream), but
// here are ifstream-based implementations for benchmarking.

inline auto
unbuffered_binary_ifstream_input_stream(std::string const &filename,
                                        std::uint64_t start = 0) {
    std::ifstream stream;

    // The standard says that the following makes the stream "unbuffered", but
    // its definition of unbuffered specifies nothing about input streams. At
    // least with libc++, this is a huge pessimization:
    stream.rdbuf()->pubsetbuf(nullptr, 0);

    stream.open(filename, std::ios::binary);
    auto ret = internal::istream_input_stream(std::move(stream));
    skip_stream_bytes(ret, start);
    return ret;
}

inline auto binary_ifstream_input_stream(std::string const &filename,
                                         std::uint64_t start = 0) {
    std::ifstream stream;
    stream.open(filename, std::ios::binary);
    auto ret = internal::istream_input_stream(std::move(stream));
    skip_stream_bytes(ret, start);
    return ret;
}

inline auto unbuffered_binary_cfile_input_stream(std::string const &filename,
                                                 std::uint64_t start = 0) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), "rb");
    if (fp != nullptr) {
        // Unlike with ifstream, setting to unbuffered does reduce overhead.
        std::setbuf(fp, nullptr);
    }
    auto ret = internal::cfile_input_stream(fp, true);
    skip_stream_bytes(ret, start);
    return ret;
}

inline auto binary_cfile_input_stream(std::string const &filename,
                                      std::uint64_t start = 0) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), "rb");
    auto ret = internal::cfile_input_stream(fp, true);
    skip_stream_bytes(ret, start);
    return ret;
}

} // namespace internal

/**
 * \brief Create a binary input stream for the given file.
 *
 * \ingroup input-streams
 *
 * \see read_binary_stream
 *
 * \param filename the filename
 *
 * \param start offset within the file to start reading from
 *
 * \return input stream
 */
inline auto binary_file_input_stream(std::string const &filename,
                                     std::uint64_t start = 0) {
    // Prefer cfile over ifstream for performance; for cfile, unbuffered
    // performs better (given our own buffering). See benchmark.
    return internal::unbuffered_binary_cfile_input_stream(filename, start);
}

/**
 * \brief Create an abstract input stream from an \c std::istream instance.
 *
 * \ingroup input-streams
 *
 * The istream is moved into the returned input stream and destroyed together,
 * so you cannot use this with an istream that you do not own (such as \c
 * std::cin). For that, see \ref borrowed_cfile_input_stream (which works with
 * \c stdin).
 *
 * \see read_binary_stream
 *
 * \param stream an istream (derived from \c std::istream)
 *
 * \return input stream
 */
template <typename IStream>
inline auto istream_input_stream(IStream &&stream) {
    static_assert(std::is_base_of_v<std::istream, IStream>);
    return internal::istream_input_stream(std::move(stream));
}

/**
 * \brief Create an abstract input stream from a C file pointer, taking
 * ownership.
 *
 * \ingroup input-streams
 *
 * The stream will use the C stdio functions, such as \c std::fread(). The file
 * pointer is closed when the stream is destroyed.
 *
 * The file pointer \e fp should have been opened in binary mode.
 *
 * If \e fp is null, the stream will always be in an error state (even after
 * clearing).
 *
 * \see borrowed_cfile_input_stream
 * \see read_binary_stream
 *
 * \param fp a file pointer
 *
 * \return input stream
 */
inline auto owning_cfile_input_stream(std::FILE *fp) {
    return internal::cfile_input_stream(fp, true);
}

/**
 * \brief Create an abstract input stream from a non-owned C file pointer.
 *
 * \ingroup input-streams
 *
 * The stream will use the C stdio functions, such as \c std::fread(). The file
 * pointer is not closed when the stream is destroyed. The caller is
 * responsible for ensuring that the file pointer will remain valid throughout
 * the lifetime of the returned input stream.
 *
 * The file pointer \e fp should have been opened in binary mode. (If using
 * \c stdin, use \c std::freopen() with a null filename on POSIX or \c
 * _setmode() with \c _O_BINARY on Windows.)
 *
 * If \e fp is null, the stream will always be in an error state (even after
 * clearing).
 *
 * \see owning_cfile_input_stream
 * \see read_binary_stream
 *
 * \param fp a file pointer
 *
 * \return input stream
 */
inline auto borrowed_cfile_input_stream(std::FILE *fp) {
    return internal::cfile_input_stream(fp, false);
}

namespace internal {

template <typename InputStream, typename Event, typename EventVector,
          typename Downstream>
class read_binary_stream {
    static_assert(
        std::is_trivial_v<Event>,
        "Event type must be trivial to work with read_binary_stream");

    InputStream stream;
    std::uint64_t length;

    std::uint64_t total_bytes_read = 0;
    std::array<std::byte, sizeof(Event)>
        remainder; // Store partially read Event.
    std::size_t remainder_nbytes = 0;

    std::shared_ptr<object_pool<EventVector>> bufpool;
    std::size_t read_size;

    Downstream downstream;

  public:
    explicit read_binary_stream(
        InputStream &&stream, std::uint64_t max_length,
        std::shared_ptr<object_pool<EventVector>> buffer_pool,
        std::size_t read_size_bytes, Downstream &&downstream)
        : stream(std::move(stream)), length(max_length),
          bufpool(std::move(buffer_pool)), read_size(read_size_bytes),
          downstream(std::move(downstream)) {
        assert(bufpool);
        assert(read_size > 0);
    }

    void pump_events() noexcept {
        auto this_read_size = read_size;
        if (stream.is_good()) {
            // Align second and subsequent reads to read_size if current
            // offset is available. This may or may not improve read
            // performance (when the read_size is a multiple of the page
            // size or block size), but can't hurt.
            std::optional<std::uint64_t> pos = stream.tell();
            if (pos.has_value())
                this_read_size -= *pos % read_size;
        }

        while (total_bytes_read < length && stream.is_good()) {
            this_read_size = std::min<std::uint64_t>(
                this_read_size, length - total_bytes_read); // > 0
            auto const bufsize_bytes = remainder_nbytes + this_read_size;
            auto const bufsize_elements =
                (bufsize_bytes - 1) / sizeof(Event) + 1;
            std::shared_ptr<EventVector> buf;
            try {
                buf = bufpool->check_out();
                buf->resize(bufsize_elements);
            } catch (std::exception const &e) {
                return downstream.handle_end(std::make_exception_ptr(e));
            }
            // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
            span<std::byte> const buffer_span{
                reinterpret_cast<std::byte *>(buf->data()), bufsize_bytes};
            // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

            std::copy(remainder.begin(),
                      std::next(remainder.begin(), remainder_nbytes),
                      buffer_span.begin());
            auto const read_span = buffer_span.subspan(remainder_nbytes);
            assert(read_span.size() == this_read_size);

            auto const bytes_read = stream.read(read_span);
            total_bytes_read += bytes_read;
            auto const data_span =
                buffer_span.subspan(0, remainder_nbytes + bytes_read);

            auto const this_batch_size = data_span.size() / sizeof(Event);
            remainder_nbytes = data_span.size() % sizeof(Event);
            auto const batch_span =
                data_span.subspan(0, data_span.size() - remainder_nbytes);
            auto const remainder_span = data_span.subspan(batch_span.size());
            std::copy(remainder_span.begin(), remainder_span.end(),
                      remainder.begin());
            if (this_batch_size > 0) {
                buf->resize(this_batch_size);
                downstream.handle_event(buf);
            }

            this_read_size = read_size;
        }

        std::exception_ptr error;
        if (stream.is_error()) {
            error = std::make_exception_ptr(
                std::runtime_error("failed to read input"));
        } else if (remainder_nbytes > 0) {
            error = std::make_exception_ptr(std::runtime_error(
                "bytes fewer than event size remain at end of input"));
        }
        downstream.handle_end(std::move(error));
    }
};

} // namespace internal

/**
 * \brief Create a source that reads batches of events from a binary stream,
 * such as a file.
 *
 * \ingroup processors-basic
 *
 * The stream is either libtcspc's stream abstraction (see \ref input-streams)
 * or an iostreams stream. In the latter case, it is wrapped using \ref
 * istream_input_stream.
 *
 * The stream must contain a contiguous array of events (of type \c Event,
 * which must be a trivial type). Events are read from the stream in batches
 * and placed into buffers (of type \c EventContainer) supplied by an \ref
 * object_pool. The events sent to the downstream processor are of the type \c
 * std::shared_ptr<EventVector>.
 *
 * The \e read_size can be tuned for best performance. If too small, reads will
 * incur more overhead per bytes read; if too large, CPU caches may be
 * polluted. Small batch sizes may also pessimize downstream processing. It is
 * best to try different powers of 2 and measure, but 32768 bytes is likely a
 * good starting point.
 *
 * \tparam Event the event type
 *
 * \tparam EventVector vector-like container of Event used for buffering (must
 * provide \c data() and \c resize())
 *
 * \tparam InputStream input stream type
 *
 * \tparam Downstream downstream processor type
 *
 * \param stream the input stream (e.g., an \c std::ifstream opened in binary
 * mode)
 *
 * \param max_length maximum number of bytes to read from stream (should be a
 * multiple of \c sizeof(Event), or \c std::numeric_limit<std::size_t>::max()
 * to read to the end of the stream)
 *
 * \param buffer_pool object pool providing event buffers
 *
 * \param read_size_bytes size, in bytes, to read in each iteration; batches
 * will be approximately this size
 *
 * \param downstream downstream processor
 *
 * \return read-binary-stream source having \c pump_events member function
 */
template <typename Event, typename EventVector, typename InputStream,
          typename Downstream>
auto read_binary_stream(InputStream &&stream, std::uint64_t max_length,
                        std::shared_ptr<object_pool<EventVector>> buffer_pool,
                        std::size_t read_size_bytes, Downstream &&downstream) {
    // Support direct passing of C++ iostreams stream.
    if constexpr (std::is_base_of_v<std::istream, InputStream>) {
        auto wrapped =
            internal::istream_input_stream<InputStream>(std::move(stream));
        return internal::read_binary_stream<decltype(wrapped), Event,
                                            EventVector, Downstream>(
            std::move(wrapped), max_length, std::move(buffer_pool),
            read_size_bytes, std::forward<Downstream>(downstream));
    } else {
        return internal::read_binary_stream<InputStream, Event, EventVector,
                                            Downstream>(
            std::forward<InputStream>(stream), max_length,
            std::move(buffer_pool), read_size_bytes,
            std::forward<Downstream>(downstream));
    }
}

} // namespace tcspc
