#pragma once

#include "DecodedEvent.hpp"

#include <exception>

template <typename E> class DeviceEventProcessor {
  public:
    virtual ~DeviceEventProcessor() = default;

    virtual void HandleEvent(E const &event) = 0;
    virtual void HandleError(std::exception_ptr exception) = 0;
    virtual void HandleFinish() = 0;
};
