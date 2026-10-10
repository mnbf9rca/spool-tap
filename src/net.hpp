#pragma once

#include <ctime>
#include <string>

enum class ApiError { None, Wifi, Time, Dns, Tls, Timeout, Http, TooLarge, InvalidUrl };

struct HttpResponse {
    int status = 0;
    std::string body;
};

bool clockValid(time_t now);
ApiError readinessError(bool wifi_connected, time_t now);
const char* apiErrorLabel(ApiError error);

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual bool request(const char* method, const char* url, const std::string& body,
                         HttpResponse& out, ApiError& error) = 0;
};

class Net : public HttpTransport {
public:
    void begin();
    bool ready(ApiError& error) const;
    bool request(const char* method, const char* url, const std::string& body,
                 HttpResponse& out, ApiError& error) override;
};
