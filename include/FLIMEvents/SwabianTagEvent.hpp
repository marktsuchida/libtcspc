#pragma once

#include "ReadBytes.hpp"
#include "TCSPCEvents.hpp"

#include <cstdint>
#include <exception>

namespace flimevt {

// The raw tag stream format (struct Tag) is documented in Swabian's Time
// Tagger C++ API Manual (part of their software download). See the 16-byte
// 'Tag' struct.

/**
 * \brief Binary record interpretation for 16-byte Swabian 'Tag'.
 *
 * This has the same size and memory layout as the 'Tag' struct in the Swabian
 * Time Tagger C++ API.
 */
struct SwabianTagEvent {
    unsigned char bytes[16];

    enum class Type : std::uint8_t {
        TimeTag = 0,
        Error = 1,
        OverflowBegin = 2,
        OverflowEnd = 3,
        MissedEvents = 4,
    };

    Type GetType() const noexcept { return Type(bytes[0]); }

    // bytes[1] is reserved, to be written zero.

    std::uint16_t GetMissedEventCount() const noexcept {
        return internal::ReadU16LE(&bytes[2]);
    }

    std::int32_t GetChannel() const noexcept {
        return internal::ReadI32LE(&bytes[4]);
    }

    std::int64_t GetTime() const noexcept {
        return internal::ReadI64LE(&bytes[8]);
    }
};

template <typename D> class DecodeSwabianTags {
    bool hadError = false;
    D downstream;

  public:
    explicit DecodeSwabianTags(D &&downstream)
        : downstream(std::move(downstream)) {}

    void HandleEvent(SwabianTagEvent const &event) noexcept {
        if (hadError)
            return;

        using Type = SwabianTagEvent::Type;
        switch (event.GetType()) {
        case Type::TimeTag: {
            TimeTaggedCountEvent e;
            e.macrotime = event.GetTime();
            e.channel = event.GetChannel();
            downstream.HandleEvent(e);
            break;
        }
        case Type::Error:
            downstream.HandleEnd(std::make_exception_ptr(
                std::runtime_error("Error tag in input")));
            hadError = true;
            break;
        case Type::OverflowBegin: {
            BeginLostIntervalEvent e;
            e.macrotime = event.GetTime();
            downstream.HandleEvent(e);
            break;
        }
        case Type::OverflowEnd: {
            EndLostIntervalEvent e;
            e.macrotime = event.GetTime();
            downstream.HandleEvent(e);
            break;
        }
        case Type::MissedEvents: {
            UntaggedCountsEvent e;
            e.macrotime = event.GetTime();
            e.count = event.GetMissedEventCount();
            break;
        }
        default:
            downstream.HandleEnd(std::make_exception_ptr(
                std::runtime_error("Unknown Swabian event type")));
            hadError = true;
            break;
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
