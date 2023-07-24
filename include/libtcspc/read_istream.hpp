/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "buffer.hpp"
#include "span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace tcspc {

namespace internal {

// Design note: read_istream holds a std::istream via std::unique_ptr to
// allow polymorphism (the overhead of the indirection is likely negligible).
// We could also imagine scenarios where user code wants to provide a reference
// to a stream that is shared with other user tasks. This is not possible due
// to the unique ownership. However, the intended use case for
// read_istream is upstream of a buffer, pumping events on a dedicated
// reading thread. Since streams are in general not thread safe, sharing the
// istream with some other task almost never makes sense.

inline auto unbuffered_binary_ifstream_at_offset(std::string const &filename,
                                                 std::uint64_t start)
    -> std::ifstream {
    std::ifstream stream;
    stream.rdbuf()->pubsetbuf(nullptr, 0); // Disable buffering.
    stream.open(filename, std::ios::binary);
    if (start > 0 && stream.good()) {
        // Unlikely but guard against 32-bit std::streamoff.
        if (start > std::numeric_limits<std::streamoff>::max())
            stream.setstate(std::ios::failbit);
        else
            stream.seekg(static_cast<std::streamoff>(start), std::ios::beg);
        if (stream.fail()) {
            stream.clear();
            // Try instead reading and discarding up to 'start', to
            // support non-seekable files (e.g., pipes).
            std::uint64_t bytes_discarded = 0;
            static constexpr std::streamsize bufsize = 65536;
            std::vector<char> buf(bufsize);
            while (bytes_discarded < start) {
                auto read_size =
                    std::min<std::uint64_t>(bufsize, start - bytes_discarded);
                stream.read(buf.data(),
                            static_cast<std::streamsize>(read_size));
                if (not stream.good()) {
                    stream.close();
                    break;
                }
                bytes_discarded += stream.gcount();
            }
        }
    }
    return stream;
}

template <typename Event, typename EventVector, typename Downstream>
class read_istream {
    static_assert(std::is_trivial_v<Event>,
                  "Event type must be trivial to work with read_istream");

    std::unique_ptr<std::istream> stream;
    std::uint64_t length;

    std::uint64_t total_bytes_read = 0;
    std::array<char, sizeof(Event)> remainder; // Store partially read Event.
    std::size_t remainder_nbytes = 0;

    std::shared_ptr<object_pool<EventVector>> buffer_pool;
    std::size_t read_size;

    Downstream downstream;

  public:
    template <typename IStream, typename = std::enable_if_t<
                                    std::is_base_of_v<std::istream, IStream>>>
    explicit read_istream(
        IStream &&stream, std::uint64_t max_length,
        std::shared_ptr<object_pool<EventVector>> buffer_pool,
        std::size_t read_size_bytes, Downstream &&downstream)
        : stream(std::make_unique<IStream>(std::forward<IStream>(stream))),
          length(max_length), buffer_pool(std::move(buffer_pool)),
          read_size(read_size_bytes), downstream(std::move(downstream)) {
        assert(read_size > 0);
        assert(read_size <= std::numeric_limits<std::streamsize>::max());
    }

    void pump_events() noexcept {
        auto this_read_size = read_size;
        if (stream->good()) {
            // Align second and subsequent reads to read_size if current offset
            // is available. This may or may not improve read performance (when
            // the read_size is a multiple of the page size or block size), but
            // can't hurt.
            std::streamoff const offset = stream->tellg();
            if (offset >= 0)
                this_read_size -= offset % read_size;
        }

        while (total_bytes_read < length && stream->good()) {
            this_read_size = std::min<std::uint64_t>(
                this_read_size, length - total_bytes_read); // > 0
            auto const bufsize_bytes = remainder_nbytes + this_read_size;
            auto const bufsize_elements =
                (bufsize_bytes - 1) / sizeof(Event) + 1;
            std::shared_ptr<EventVector> buf;
            try {
                buf = buffer_pool->check_out();
                buf->resize(bufsize_elements);
            } catch (std::exception const &e) {
                return downstream.handle_end(std::make_exception_ptr(e));
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            span<char> const buffer_span{reinterpret_cast<char *>(buf->data()),
                                         bufsize_bytes};

            std::copy(remainder.begin(),
                      std::next(remainder.begin(), remainder_nbytes),
                      buffer_span.begin());
            auto const read_span = buffer_span.subspan(remainder_nbytes);
            assert(read_span.size() == this_read_size);

            stream->read(read_span.data(),
                         static_cast<std::streamsize>(read_span.size()));
            auto const bytes_read = static_cast<std::size_t>(stream->gcount());
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
        if (stream->fail() && not stream->eof())
            error = std::make_exception_ptr(
                std::runtime_error("failed to read input"));
        else if (remainder_nbytes > 0)
            error = std::make_exception_ptr(std::runtime_error(
                "bytes fewer than event size remain at end of input"));
        downstream.handle_end(std::move(error));
    }
};

} // namespace internal

/**
 * \brief Create a source that reads batches of events from a binary \c
 * std::istream.
 *
 * \ingroup processors-basic
 *
 * The stream must contain a contiguous array of events (of type \c Event,
 * which must be a trivial type). Events are read from the stream in batches
 * and placed into buffers (of type \c EventContainer) provided by an \ref
 * object_pool. The events sent to the downstream processor are these buffers
 * (via \c std::shared_ptr).
 *
 * If the stream is a file stream, it should be opened in binary mode.
 *
 * \see read_file
 *
 * \tparam Event the event type
 *
 * \tparam EventVector vector-like container of Event used for buffering (must
 * provide \c data() and \c resize())
 *
 * \tparam IStream input stream type (must be derived from \c std::istream)
 *
 * \tparam Downstream downstream processor type
 *
 * \param stream the input stream (e.g., an \c std::ifstream opened in binary
 * mode)
 *
 * \param max_length maximum number of bytes to read from stream (should be a
 * multiple of \c sizeof(Event), or \c std::numeric_limit<std::size_t>::max())
 *
 * \param buffer_pool object pool providing event buffers
 *
 * \param read_size_bytes size, in bytes, to read for each iteration; batches
 * will be approximately this size
 *
 * \param downstream downstream processor
 *
 * \return read-istream source having \c pump_events member function
 */
template <
    typename Event, typename EventVector, typename IStream,
    typename Downstream,
    typename = std::enable_if_t<std::is_base_of_v<std::istream, IStream>>>
auto read_istream(IStream &&stream, std::uint64_t max_length,
                  std::shared_ptr<object_pool<EventVector>> buffer_pool,
                  std::size_t read_size_bytes, Downstream &&downstream) {
    return internal::read_istream<Event, EventVector, Downstream>(
        std::forward<IStream>(stream), max_length, std::move(buffer_pool),
        read_size_bytes, std::forward<Downstream>(downstream));
}

/**
 * \brief Create a source that reads batches of events from a binary file.
 *
 * \ingroup processors-basic
 *
 * This is a convenience wrapper around \ref read_istream. The \c std::istream
 * is constructed by opening the given file in binary mode and seeking to the
 * start offset.
 *
 * Otherwise, the behavior is the same as \ref read_istream.
 *
 * \see read_istream
 *
 * \tparam Event the event type
 *
 * \tparam EventVector vector-like container of Event used for buffering (must
 * provide \c data() and \c resize())
 *
 * \tparam Downstream downstream processor type
 *
 * \param filename name of file to read from
 *
 * \param start start offset, in bytes, from which to read
 *
 * \param max_length maximum number of bytes to read (should be a multiple of
 * \c sizeof(Event))
 *
 * \param buffer_pool object pool providing event buffers
 *
 * \param read_size_bytes size, in bytes, to read for each iteration; batches
 * will be approximately this size
 *
 * \param downstream downstream processor
 *
 * \return read-istream source having \c pump_events member function
 */
template <typename Event, typename EventVector, typename Downstream>
auto read_file(std::string const &filename, std::uint64_t start,
               std::uint64_t max_length,
               std::shared_ptr<object_pool<EventVector>> buffer_pool,
               std::size_t read_size_bytes, Downstream &&downstream) {
    return internal::read_istream<Event, EventVector, Downstream>(
        internal::unbuffered_binary_ifstream_at_offset(filename, start),
        max_length, std::move(buffer_pool), read_size_bytes,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
