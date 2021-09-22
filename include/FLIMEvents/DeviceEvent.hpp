#pragma once

#include "DecodedEvent.hpp"

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

class DeviceEventProcessor {
  public:
    virtual ~DeviceEventProcessor() = default;

    virtual std::size_t GetEventSize() const noexcept = 0;
    virtual void HandleDeviceEvent(char const *event) = 0;
    virtual void HandleError(std::exception_ptr exception) = 0;
    virtual void HandleFinish() = 0;

    void HandleDeviceEvents(char const *events, std::size_t count) {
        auto size = GetEventSize();
        for (std::size_t i = 0; i < count; ++i) {
            HandleDeviceEvent(events + i * size);
        }
    }
};

// A DeviceEventProcessor that sends decoded events downstream
class DeviceEventDecoder : public DeviceEventProcessor {
    std::shared_ptr<DecodedEventProcessor> downstream;

  protected:
    template <typename E> void EmitEvent(E const &event) {
        if (downstream) {
            downstream->HandleEvent(event);
        }
    }

    void EmitError(std::exception_ptr exception) {
        if (downstream) {
            downstream->HandleError(exception);
            downstream.reset();
        }
    }

    void EmitFinish() {
        if (downstream) {
            downstream->HandleFinish();
            downstream.reset();
        }
    }

  public:
    explicit DeviceEventDecoder(
        std::shared_ptr<DecodedEventProcessor> downstream)
        : downstream(downstream) {}

    void HandleError(std::exception_ptr exception) override {
        EmitError(exception);
    }

    void HandleFinish() override { EmitFinish(); }
};
