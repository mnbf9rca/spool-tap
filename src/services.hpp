#pragma once

#include "net.hpp"
#include "settings.hpp"

class Services {
public:
    Services(HttpTransport& transport, const Settings& settings)
        : transport_(transport), settings_(settings) {}

    bool getMapping(Mapping& out, ApiError& error);
    bool getSpool(uint32_t id, SpoolInfo& out, ApiError& error);
    bool scan(const TagRead& tag, ScanMatch& out, ApiError& error);
    bool mapToolhead(uint32_t spool_id, Mapping& after, ApiError& error);
    void setBetweenRequests(void (*callback)()) { between_requests_ = callback; }

private:
    HttpTransport& transport_;
    const Settings& settings_;
    void (*between_requests_)() = nullptr;
};
