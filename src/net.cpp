#include "net.hpp"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/dns.h>

#include <atomic>
#include <cstring>

#include "trust_roots.hpp"
#ifndef SPOOL_TAP_UNIT_TEST
#include <build_secrets.hpp>
#endif

namespace {
constexpr time_t kEarliestValidTime = 1735689600; // 2025-01-01 UTC
constexpr uint32_t kTimeoutMs = 3000;
constexpr size_t kMaxResponseBytes = 32768;

struct DnsWait {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ip_addr_t address{};
    std::atomic<int> users{2};
};

void releaseDns(DnsWait* wait) {
    if (wait->users.fetch_sub(1) == 1) {
        vSemaphoreDelete(wait->done);
        delete wait;
    }
}

void dnsFound(const char*, const ip_addr_t* address, void* arg) {
    auto* wait = static_cast<DnsWait*>(arg);
    if (address) wait->address = *address;
    xSemaphoreGive(wait->done);
    releaseDns(wait);
}

bool resolveHost(const char* host, IPAddress& ip, ApiError& error) {
    auto* wait = new DnsWait;
    if (!wait || !wait->done) {
        if (wait) { if (wait->done) vSemaphoreDelete(wait->done); delete wait; }
        error = ApiError::Dns;
        return false;
    }
    ip_addr_t address{};
    const err_t result = dns_gethostbyname(host, &address, dnsFound, wait);
    if (result == ERR_INPROGRESS) {
        if (xSemaphoreTake(wait->done, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) {
            releaseDns(wait);
            error = ApiError::Timeout;
            return false;
        }
        address = wait->address;
        releaseDns(wait);
    } else {
        // An immediate result never invokes the callback.
        wait->users.store(1);
        releaseDns(wait);
    }
    if (result != ERR_OK && result != ERR_INPROGRESS) {
        error = ApiError::Dns;
        return false;
    }
    if (!IP_IS_V4(&address) || address.u_addr.ip4.addr == 0) {
        error = ApiError::Dns;
        return false;
    }
    ip = IPAddress(address.u_addr.ip4.addr);
    return true;
}

class ResolvedClient : public WiFiClientSecure {
public:
    explicit ResolvedClient(IPAddress ip) : ip_(ip) {
        setCACert(kIsrgRoots);
        setHandshakeTimeout(3);
    }

    int connect(const char* host, uint16_t port, int32_t timeout) override {
        _timeout = timeout;
        return WiFiClientSecure::connect(ip_, port, host, kIsrgRoots, nullptr, nullptr);
    }

private:
    IPAddress ip_;
};

bool hostnameFromUrl(const char* url, std::string& host) {
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    const char* start = url + 8;
    const char* end = start;
    while (*end && *end != ':' && *end != '/') {
        if (!((*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z') ||
              (*end >= '0' && *end <= '9') || *end == '-' || *end == '.')) return false;
        ++end;
    }
    if (end == start || static_cast<size_t>(end - start) > 253) return false;
    host.assign(start, end);
    if (*end == ':') {
        ++end;
        if (*end < '0' || *end > '9') return false;
        unsigned port = 0;
        while (*end >= '0' && *end <= '9') {
            port = port * 10 + *end++ - '0';
            if (port > 65535) return false;
        }
        if (port == 0) return false;
    }
    return *end == '/' || *end == '\0';
}

bool readBody(HTTPClient& http, std::string& body, ApiError& error) {
    const int expected = http.getSize();
    if (expected > static_cast<int>(kMaxResponseBytes)) {
        error = ApiError::TooLarge;
        return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) { error = ApiError::Http; return false; }
    const uint32_t started = millis();
    char buffer[512];
    while (expected < 0 ? stream->connected() || stream->available() :
                          body.size() < static_cast<size_t>(expected)) {
        if (millis() - started >= kTimeoutMs) { error = ApiError::Timeout; return false; }
        const int available = stream->available();
        if (available <= 0) { delay(1); continue; }
        const size_t space = kMaxResponseBytes - body.size();
        if (space == 0) { error = ApiError::TooLarge; return false; }
        size_t count = static_cast<size_t>(available);
        if (count > sizeof(buffer)) count = sizeof(buffer);
        if (count > space) count = space;
        if (expected >= 0 && count > static_cast<size_t>(expected) - body.size())
            count = static_cast<size_t>(expected) - body.size();
        const int read = stream->read(reinterpret_cast<uint8_t*>(buffer), count);
        if (read > 0) body.append(buffer, read);
    }
    return true;
}
} // namespace

bool clockValid(time_t now) { return now >= kEarliestValidTime; }

ApiError readinessError(bool wifi_connected, time_t now) {
    if (!wifi_connected) return ApiError::Wifi;
    if (!clockValid(now)) return ApiError::Time;
    return ApiError::None;
}

const char* apiErrorLabel(ApiError error) {
    switch (error) {
    case ApiError::None: return "Ready";
    case ApiError::Wifi: return "Wi-Fi unavailable";
    case ApiError::Time: return "Time not set";
    case ApiError::Dns: return "DNS failed";
    case ApiError::Tls: return "TLS certificate error";
    case ApiError::Timeout: return "Request timed out";
    case ApiError::TooLarge: return "Response too large";
    case ApiError::InvalidUrl: return "Invalid HTTPS URL";
    case ApiError::Http: return "HTTP request failed";
    case ApiError::Malformed: return "Invalid service response";
    case ApiError::MissingSpool: return "Spool not found";
    case ApiError::ArchivedSpool: return "Spool archived";
    case ApiError::Conflict: return "Spool already in use";
    }
    return "HTTP request failed";
}

void Net::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
#ifndef SPOOL_TAP_UNIT_TEST
    WiFi.begin(BuildSecrets::wifi_ssid, BuildSecrets::wifi_password);
#endif
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
}

bool Net::ready(ApiError& error) const {
    error = readinessError(WiFi.status() == WL_CONNECTED, time(nullptr));
    return error == ApiError::None;
}

bool Net::request(const char* method, const char* url, const std::string& body,
                  HttpResponse& out, ApiError& error) {
    out = {};
    if (!ready(error)) return false;
    std::string host;
    if (!hostnameFromUrl(url, host)) { error = ApiError::InvalidUrl; return false; }
    IPAddress ip;
    if (!resolveHost(host.c_str(), ip, error)) return false;
    ResolvedClient client(ip);
    HTTPClient http;
    http.setConnectTimeout(kTimeoutMs);
    http.setTimeout(kTimeoutMs);
    http.useHTTP10(true);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) { error = ApiError::InvalidUrl; return false; }
    const char* headers[] = {"Transfer-Encoding"};
    http.collectHeaders(headers, 1);
    if (!body.empty()) http.addHeader("Content-Type", "application/json");
    const uint32_t started = millis();
    const int status = http.sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
    if (status <= 0) {
        char detail[96]{};
        const int secure_error = client.lastError(detail, sizeof(detail));
        error = status == HTTPC_ERROR_READ_TIMEOUT ||
                (secure_error == -1 && millis() - started >= kTimeoutMs) ? ApiError::Timeout :
                secure_error != 0 && secure_error != -1 ? ApiError::Tls : ApiError::Http;
        http.end();
        return false;
    }
    out.status = status;
    // shortcut: HTTP/1.0 responses must be unchunked; add a bounded decoder if a service changes.
    if (http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
        error = ApiError::Http;
        http.end();
        return false;
    }
    const bool read_ok = readBody(http, out.body, error);
    http.end();
    if (!read_ok) return false;
    error = ApiError::None;
    return true;
}
