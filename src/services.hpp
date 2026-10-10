#pragma once

#include "net.hpp"
#include "settings.hpp"

class Services {
public:
    Services(HttpTransport& transport, const Settings& settings)
        : transport_(transport), settings_(settings) {}

    bool getMapping(Mapping& out, ApiError& error);
    bool getSpool(uint32_t id, SpoolInfo& out, ApiError& error);

private:
    HttpTransport& transport_;
    const Settings& settings_;
};
