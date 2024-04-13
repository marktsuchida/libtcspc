/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "common.hpp"
#include "introspect.hpp"
#include "span.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

// When editing this file, maintain partial symmetry with
// write_binary_stream.hpp.

namespace tcspc {

/**
 * \addtogroup streams
 *
 * \par Requirements for input streams
 * An input stream must be a movable (usually noncopyable) object with the
 * following member functions:
 * - <tt>auto is_error() noexcept -> bool;</tt>\n
 *   Return true if the stream is not available or the previous read operation
 *   resulted in an error (\e not including reaching EOF). Not influenced by
 *   failure of \c tell() or \c skip().
 * - <tt>auto is_eof() noexcept -> bool;</tt>\n
 *   Return true if the previous read operation tried to read beyond the end of
 *   the stream (or if the stream is not available). Not influenced by failure
 *   of \c tell() or \c skip().
 * - <tt>auto is_good() noexcept -> bool;</tt>\n
 *   Return true if neiter \c is_error() nor \c is_eof() is true.
 * - <tt>auto tell() noexcept -> std::optional<std::uint64_t>;</tt>\n
 *   Return the current stream position if supported by the stream, or \c
 *   std::nullopt.
 * - <tt>auto skip(std::uint64_t bytes) noexcept -> bool;</tt>\n
 *   Seek, relative to the current offset, forward by \e bytes. Return true if
 *   successful.
 * - <tt>auto read(tcspc::span<std::byte> buffer) noexcept ->
 *   std::uint64_t;</tt>\n
 *   Read into the given buffer, up to the buffer size. Return the number of
 *   bytes read.
 */

namespace internal {

struct null_input_stream {
    static auto is_error() noexcept -> bool { return false; }
    static auto is_eof() noexcept -> bool { return true; }
    static auto is_good() noexcept -> bool { return false; }
    static auto tell() noexcept -> std::optional<std::uint64_t> { return 0; }
    static auto skip(std::uint64_t bytes) noexcept -> bool {
        return bytes == 0;
    }
    static auto read([[maybe_unused]] span<std::byte> buffer) noexcept
        -> std::uint64_t {
        return 0;
    }
};

// We turn off istream exceptions in the constructor.
// NOLINTBEGIN(bugprone-exception-escape)
template <typename IStream> class istream_input_stream {
    static_assert(std::is_base_of_v<std::istream, IStream>);
    IStream stream;

  public:
    explicit istream_input_stream(IStream stream) : stream(std::move(stream)) {
        this->stream.exceptions(std::ios::goodbit);
    }

    auto is_error() noexcept -> bool {
        auto const flags = stream.rdstate();
        return ((flags & std::ios::failbit) || (flags & std::ios::badbit)) &&
               not(flags & std::ios::eofbit);
    }

    auto is_eof() noexcept -> bool { return stream.eof(); }

    auto is_good() noexcept -> bool { return stream.good(); }

    auto tell() noexcept -> std::optional<std::uint64_t> {
        if (stream.fail())
            return std::nullopt; // Do not affect flags.
        std::int64_t const pos = stream.tellg();
        if (pos >= 0)
            return std::uint64_t(pos);
        stream.clear();
        return std::nullopt;
    }

    auto skip(std::uint64_t bytes) noexcept -> bool {
        if (stream.fail() ||
            bytes > std::uint64_t(std::numeric_limits<std::streamoff>::max()))
            return false;
        stream.seekg(std::streamoff(bytes), std::ios::cur);
        auto const ret = stream.good();
        stream.clear();
        return ret;
    }

    auto read(span<std::byte> buffer) noexcept -> std::uint64_t {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        stream.read(reinterpret_cast<char *>(buffer.data()),
                    static_cast<std::streamsize>(buffer.size()));
        return static_cast<std::uint64_t>(stream.gcount());
    }
};
// NOLINTEND(bugprone-exception-escape)

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

    auto skip(std::uint64_t bytes) noexcept -> bool {
        if (fp == nullptr)
            return false;
#ifdef _WIN32
        if (bytes <= std::uint64_t(std::numeric_limits<__int64>::max()))
            return ::_fseeki64(fp, __int64(bytes), SEEK_CUR) == 0;
#else
        if (bytes <= std::numeric_limits<long>::max())
            return std::fseek(fp, long(bytes), SEEK_CUR) == 0;
#endif
        return false;
    }

    auto read(span<std::byte> buffer) noexcept -> std::uint64_t {
        if (fp == nullptr)
            return 0;
        return std::fread(buffer.data(), 1, buffer.size(), fp);
    }
};

template <typename InputStream>
inline void skip_stream_bytes(InputStream &stream, std::uint64_t bytes) {
    if (not stream.skip(bytes)) {
        // Try instead reading and discarding up to 'start', to support
        // non-seekable streams (e.g., pipes).
        std::uint64_t bytes_discarded = 0;
        // For now, use the read size that was found fastest when reading
        // /dev/zero on an Apple M1 Pro laptop. Could be tuned.
        static constexpr std::streamsize bufsize = 32768;
        std::vector<std::byte> buf(bufsize);
        span<std::byte> const bufspan(buf);
        while (bytes_discarded < bytes) {
            auto read_size =
                std::min<std::uint64_t>(bufsize, bytes - bytes_discarded);
            bytes_discarded += stream.read(bufspan.first(read_size));
            if (not stream.is_good())
                break;
        }
    }
}

// For benchmarking only
inline auto
unbuffered_binary_ifstream_input_stream(std::string const &filename,
                                        std::uint64_t start = 0) {
    std::ifstream stream;

    // The standard says that the following makes the stream "unbuffered", but
    // its definition of unbuffered specifies nothing about input streams. At
    // least with libc++, this is a huge pessimization:
    stream.rdbuf()->pubsetbuf(nullptr, 0);

    stream.open(filename, std::ios::binary);
    if (stream.fail())
        throw std::runtime_error("failed to open input file: " + filename);
    auto ret = internal::istream_input_stream(std::move(stream));
    skip_stream_bytes(ret, start);
    return ret;
}

// For benchmarking only
inline auto binary_ifstream_input_stream(std::string const &filename,
                                         std::uint64_t start = 0) {
    std::ifstream stream;
    stream.open(filename, std::ios::binary);
    if (stream.fail())
        throw std::runtime_error("failed to open input file: " + filename);
    auto ret = internal::istream_input_stream(std::move(stream));
    skip_stream_bytes(ret, start);
    return ret;
}

inline auto unbuffered_binary_cfile_input_stream(std::string const &filename,
                                                 std::uint64_t start = 0) {
#ifdef _WIN32 // Avoid requiring _CRT_SECURE_NO_WARNINGS.
    std::FILE *fp{};
    (void)fopen_s(&fp, filename.c_str(), "rb");
#else
    errno = 0; // ISO C does not require fopen to set errno on error.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), "rb");
#endif
    if (fp == nullptr) {
        if (errno != 0)
            throw std::system_error(errno, std::generic_category());
        throw std::runtime_error("failed to open input file: " + filename);
    }
    std::setvbuf(fp, nullptr, _IONBF, 0);
    auto ret = internal::cfile_input_stream(fp, true);
    skip_stream_bytes(ret, start);
    return ret;
}

// For benchmarking only
inline auto binary_cfile_input_stream(std::string const &filename,
                                      std::uint64_t start = 0) {
#ifdef _WIN32 // Avoid requiring _CRT_SECURE_NO_WARNINGS.
    std::FILE *fp{};
    (void)fopen_s(&fp, filename.c_str(), "rb");
#else
    errno = 0; // ISO C does not require fopen to set errno on error.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *fp = std::fopen(filename.c_str(), "rb");
#endif
    if (fp == nullptr) {
        if (errno != 0)
            throw std::system_error(errno, std::generic_category());
        throw std::runtime_error("failed to open input file: " + filename);
    }
    auto ret = internal::cfile_input_stream(fp, true);
    skip_stream_bytes(ret, start);
    return ret;
}

} // namespace internal

/**
 * \brief Create an input stream that contains no bytes.
 *
 * \ingroup streams
 *
 * \see read_binary_stream
 *
 * \return input stream
 */
inline auto null_input_stream() { return internal::null_input_stream(); }

/**
 * \brief Create a binary input stream for the given file.
 *
 * \ingroup streams
 *
 * If the file cannot be opened, or is smaller than \e start bytes, the stream
 * will be in an error state.
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
 * \ingroup streams
 *
 * The istream is moved into the returned input stream and destroyed together,
 * so you cannot use this with an istream that you do not own (such as \c
 * std::cin). For that, see \ref borrowed_cfile_input_stream (which works with
 * \c stdin).
 *
 * Due to poor performance, use of \c istream_input_stream is not recommended
 * unless you must interface with an existing \c std::istream.
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
    return internal::istream_input_stream(std::forward<IStream>(stream));
}

/**
 * \brief Create an abstract input stream from a C file pointer, taking
 * ownership.
 *
 * \ingroup streams
 *
 * The stream will use the C stdio functions, such as \c std::fread(). The file
 * pointer is closed when the stream is destroyed.
 *
 * The file pointer \e fp should have been opened in binary mode.
 *
 * If \e fp is null, the stream will always be in an error state.
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
 * \ingroup streams
 *
 * The stream will use the C stdio functions, such as \c std::fread(). The file
 * pointer is not closed when the stream is destroyed. The caller is
 * responsible for ensuring that the file pointer will remain valid throughout
 * the lifetime of the returned input stream.
 *
 * The file pointer \e fp should have been opened in binary mode. (If using
 * \c stdin, use \c std::freopen() with a null filename on POSIX or \c
 * _setmode() with \c _O_BINARY on Windows (via \c _fileno()).)
 *
 * If \e fp is null, the stream will always be in an error state.
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

template <typename InputStream, typename Event, typename Downstream>
class read_binary_stream {
    static_assert(
        std::is_trivial_v<Event>,
        "Event type must be trivial to work with read_binary_stream");

    InputStream stream;
    std::uint64_t length;

    std::size_t read_granularity;
    std::shared_ptr<bucket_source<Event>> bsource;

    Downstream downstream;

    LIBTCSPC_NOINLINE auto first_read_size() -> std::uint64_t {
        auto ret = read_granularity;
        if (stream.is_good()) {
            // Align second and subsequent reads to read_granularity if current
            // offset is available. This may or may not improve read
            // performance (when the read_granularity is a multiple of the page
            // size or block size), but shouldn't hurt.
            std::optional<std::uint64_t> pos = stream.tell();
            if (pos.has_value())
                ret -= *pos % read_granularity;
        }
        return ret;
    }

    // Read some multiple (max: max_units) of the read granularity that fits in
    // dest, subject to first-read size and max length.
    auto read_units(span<std::byte> dest, std::size_t max_units,
                    std::uint64_t &total_bytes_read) -> std::uint64_t {
        auto bytes_to_read =
            std::min<std::uint64_t>(dest.size(), length - total_bytes_read);
        if (total_bytes_read == 0) {
            bytes_to_read =
                std::min<std::uint64_t>(bytes_to_read, first_read_size());
        }
        if (bytes_to_read > read_granularity) {
            bytes_to_read = read_granularity *
                            std::min<std::uint64_t>(
                                max_units, bytes_to_read / read_granularity);
        }
        auto const bytes_read = stream.read(dest.first(bytes_to_read));
        total_bytes_read += bytes_read;
        return bytes_read;
    }

  public:
    explicit read_binary_stream(
        InputStream stream, std::uint64_t max_length,
        std::shared_ptr<bucket_source<Event>> bucket_source,
        std::size_t read_granularity_bytes, Downstream downstream)
        : stream(std::move(stream)), length(max_length),
          read_granularity(read_granularity_bytes),
          bsource(std::move(bucket_source)),
          downstream(std::move(downstream)) {
        if (not bsource)
            throw std::invalid_argument(
                "read_binary_stream bucket_source must not be null");
        if (read_granularity <= 0)
            throw std::invalid_argument(
                "read_binary_stream read_granularity_bytes must be positive");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "read_binary_stream");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_source(this);
        return g;
    }

    void flush() {
        auto const bucket_size =
            sizeof(Event) >= read_granularity
                ? 1
                : (read_granularity - 1) / sizeof(Event) + 1;
        auto const bucket_size_bytes = bucket_size * sizeof(Event);

        std::uint64_t total_bytes_read = 0;
        bucket<Event> bkt;
        std::size_t remainder_nbytes = 0; // Always < sizeof(Event)

        while (total_bytes_read < length && stream.is_good()) {
            auto const bytes_left_in_bucket =
                bucket_size_bytes - remainder_nbytes;
            if (bytes_left_in_bucket >= read_granularity) {
                if (bkt.empty())
                    bkt = bsource->bucket_of_size(bucket_size);
                auto const bytes_read = read_units(
                    as_writable_bytes(span(bkt)).subspan(remainder_nbytes),
                    std::numeric_limits<std::size_t>::max(), total_bytes_read);
                auto const available_nbytes = remainder_nbytes + bytes_read;
                auto const this_batch_size = available_nbytes / sizeof(Event);
                remainder_nbytes = available_nbytes % sizeof(Event);
                if (this_batch_size == 0)
                    continue; // Leave incomplete event in current bucket.
                bucket<Event> bkt2;
                if (remainder_nbytes > 0) {
                    bkt2 = bsource->bucket_of_size(bucket_size);
                    auto const remainder_span = as_bytes(span(bkt)).subspan(
                        available_nbytes - remainder_nbytes);
                    std::copy(remainder_span.begin(), remainder_span.end(),
                              as_writable_bytes(span(bkt2)).begin());
                }
                bkt.shrink(0, this_batch_size);
                downstream.handle(std::move(bkt));
                bkt = std::move(bkt2);
            } else { // Top off single event.
                bucket<Event> bkt2;
                bkt2 = bsource->bucket_of_size(bucket_size);
                auto const bytes_read = read_units(
                    as_writable_bytes(span(bkt2)), 1, total_bytes_read);
                if (bytes_read < bytes_left_in_bucket)
                    break;
                auto const top_off_span =
                    as_bytes(span(bkt2)).first(bytes_left_in_bucket);
                auto const remainder_span = as_bytes(span(bkt2))
                                                .first(bytes_read)
                                                .subspan(bytes_left_in_bucket);
                std::copy(top_off_span.begin(), top_off_span.end(),
                          as_writable_bytes(span(bkt))
                              .last(bytes_left_in_bucket)
                              .begin());
                std::copy(remainder_span.begin(), remainder_span.end(),
                          as_writable_bytes(span(bkt2)).begin());
                downstream.handle(std::move(bkt));
                bkt = std::move(bkt2);
                remainder_nbytes = remainder_span.size();
            }
        }

        if (stream.is_error())
            throw std::runtime_error("failed to read input");
        if (remainder_nbytes > 0) {
            downstream.handle(warning_event{
                "bytes fewer than record size remain at end of input"});
        }
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a source that reads batches of events from a binary stream,
 * such as a file.
 *
 * \ingroup processors-basic
 *
 * The stream is either libtcspc's input stream abstraction (see \ref streams)
 * or an iostreams \c std::istream. In the latter case, it is wrapped using
 * \ref istream_input_stream. (Use of iostreams is not recommended due to often
 * poor performance.)
 *
 * The stream must contain a contiguous array of events (of type \c Event,
 * which must be a trivial type). Events are read from the stream in batches
 * and placed into buckets (of type \c Event) supplied by a \ref bucket_source.
 *
 * Each time the stream is read, events that have been completely read are sent
 * downstream in a bucket. The size of each read is controlled by \e
 * read_granularity_bytes and the size of \c Event. When the former is not
 * smaller, it is used as the read size. When the size of \c Event is larger,
 * multiples of \e read_granularity_bytes may be read at once for efficiency.
 * The first read may be adjusted to a smaller size to align subsequent read
 * offsets to the read granularity. The last read may be adjusted to a smaller
 * size to avoid reading past \e max_length.
 *
 * The \e read_granularity_bytes can be tuned for best performance. If too
 * small, reads may incur more overhead per byte read; if too large, CPU caches
 * may be polluted. Small batch sizes may also pessimize downstream processing.
 * It is best to try different powers of 2 and measure.
 *
 * \see write_binary_stream
 *
 * \tparam Event the event type
 *
 * \tparam InputStream input stream type
 *
 * \tparam Downstream downstream processor type
 *
 * \param stream the input stream (see \ref streams)
 *
 * \param max_length maximum number of bytes to read from stream (should be a
 * multiple of \c sizeof(Event), or \c std::numeric_limit<std::size_t>::max()
 * to read to the end of the stream)
 *
 * \param bucket_source bucket source providing event buffers; must be able to
 * circulate at least 2 buckets without blocking
 *
 * \param read_granularity_bytes minimum size, in bytes, to read in each
 * iteration; a multiple of this value may be used if \c Event is larger
 *
 * \param downstream downstream processor
 *
 * \return read-binary-stream source processor
 */
template <typename Event, typename InputStream, typename Downstream>
auto read_binary_stream(InputStream &&stream, std::uint64_t max_length,
                        std::shared_ptr<bucket_source<Event>> bucket_source,
                        std::size_t read_granularity_bytes,
                        Downstream &&downstream) {
    // Support direct passing of C++ iostreams stream.
    if constexpr (std::is_base_of_v<std::istream, InputStream>) {
        auto wrapped = istream_input_stream(std::forward<InputStream>(stream));
        return internal::read_binary_stream<decltype(wrapped), Event,
                                            Downstream>(
            std::move(wrapped), max_length, std::move(bucket_source),
            read_granularity_bytes, std::forward<Downstream>(downstream));
    } else {
        return internal::read_binary_stream<InputStream, Event, Downstream>(
            std::forward<InputStream>(stream), max_length,
            std::move(bucket_source), read_granularity_bytes,
            std::forward<Downstream>(downstream));
    }
}

} // namespace tcspc
