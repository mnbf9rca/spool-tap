#pragma once

#include "ui.hpp"

class ReaderPort {
public:
    virtual ~ReaderPort() = default;
    virtual bool poll(TagRead& tag, TagHints& hints) = 0;
};

class ServicesPort {
public:
    virtual ~ServicesPort() = default;
    virtual bool getMapping(Mapping& out, ApiError& error) = 0;
    virtual bool getSpool(uint32_t id, SpoolInfo& out, ApiError& error) = 0;
    virtual bool scan(const TagRead& tag, ScanMatch& out, ApiError& error) = 0;
    virtual bool mapToolhead(uint32_t spool_id, Mapping& after, ApiError& error) = 0;
};

class UiPort {
public:
    virtual ~UiPort() = default;
    virtual UiAction pollAction() = 0;
    virtual Ui& ui() = 0;
    virtual std::string ipAddress() const = 0;
};

class ClockPort {
public:
    virtual ~ClockPort() = default;
    virtual uint32_t millis() const = 0;
};
