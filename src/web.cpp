#include "web.hpp"

#ifndef SPOOL_TAP_UNIT_TEST
#include <build_secrets.hpp>
#endif

namespace {
constexpr char kUsername[] = "spool-tap";
constexpr char kRealm[] = "Spool Tap";
}

void Web::begin() {
    server_.on("/logs", HTTP_GET, [this] { showLogs(); });
    server_.begin();
    started_ = true;
}

void Web::tick() {
    if (started_) server_.handleClient();
}

void Web::showLogs() {
#ifndef SPOOL_TAP_UNIT_TEST
    const char* password = BuildSecrets::ota_password;
#else
    const char* password = "";
#endif
    // WebServer::authenticate also accepts Basic, so require a Digest header explicitly.
    if (!server_.header("Authorization").startsWith("Digest ") ||
        !server_.authenticate(kUsername, password)) {
        server_.requestAuthentication(DIGEST_AUTH, kRealm);
        return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.send(200, "text/html; charset=utf-8", logs_.renderHtml().c_str());
}
