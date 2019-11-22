#pragma once

#include "DecodedEvent.hpp"

#include <cstdlib>
#include <memory>


class DeviceEventDecoder {
    std::shared_ptr<DecodedEventProcessor> downstream;

protected:
    void SendTimestamp(DecodedEvent const& event) {
        if (downstream) {
            downstream->HandleTimestamp(event);
        }
    }

    void SendValidPhoton(ValidPhotonEvent const& event) {
        if (downstream) {
            downstream->HandleValidPhoton(event);
        }
    }

    void SendInvalidPhoton(InvalidPhotonEvent const& event) {
        if (downstream) {
            downstream->HandleInvalidPhoton(event);
        }
    }

    void SendMarker(MarkerEvent const& event) {
        if (downstream) {
            downstream->HandleMarker(event);
        }
    }

    void SendDataLost(DataLostEvent const& event) {
        if (downstream) {
            downstream->HandleDataLost(event);
        }
    }

    void SendError(std::string const& message) {
        if (downstream) {
            downstream->HandleError(message);
            downstream.reset();
        }
    }

    void SendFinish() {
        if (downstream) {
            downstream->HandleFinish();
            downstream.reset();
        }
    }

public:
    virtual ~DeviceEventDecoder() = default;

    void SetDownstream(std::shared_ptr<DecodedEventProcessor> downstream) {
        this->downstream = downstream;
    }

    void HandleDeviceEvents(char const* events, std::size_t count) {
        auto size = GetEventSize();
        for (std::size_t i = 0; i < count; ++i) {
            HandleDeviceEvent(events + i * size);
        }
    }

    template <typename T>
    void HandleDeviceEvents(T const* events, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            HandleDeviceEvent(reinterpret_cast<char const*>(&events[i]));
        }
    }

    virtual std::size_t GetEventSize() const noexcept = 0;
    virtual void HandleDeviceEvent(char const* event) = 0;
    virtual void HandleFinish() = 0;
};
