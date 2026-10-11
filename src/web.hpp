#pragma once

#include <WebServer.h>

#include "logs.hpp"

class Web {
public:
    explicit Web(Logs& logs) : logs_(logs) {}
    void begin();
    void tick();

private:
    void showLogs();

    Logs& logs_;
    WebServer server_{80};
    bool started_ = false;
};
