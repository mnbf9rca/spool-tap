#include "services.hpp"

#include <cJSON.h>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json parseJson(const std::string& body) {
    if (body.empty() || body.size() > 32768 || body.find('\0') != std::string::npos)
        return Json(nullptr, cJSON_Delete);
    return Json(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, true), cJSON_Delete);
}

bool unsignedId(const cJSON* item, uint32_t& id) {
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 0 ||
        item->valuedouble > std::numeric_limits<uint32_t>::max() ||
        std::floor(item->valuedouble) != item->valuedouble) return false;
    id = static_cast<uint32_t>(item->valuedouble);
    return true;
}

bool parseMapping(const std::string& body, const SettingsInput& settings, Mapping& mapping) {
    Json root = parseJson(body);
    if (!root || !cJSON_IsObject(root.get())) return false;
    const cJSON* printers = cJSON_GetObjectItemCaseSensitive(root.get(), "printers");
    const cJSON* mappings = cJSON_GetObjectItemCaseSensitive(root.get(), "toolhead_mappings");
    if (!cJSON_IsObject(printers) || !cJSON_IsObject(mappings)) return false;

    const char* printer_id = nullptr;
    for (const cJSON* printer = printers->child; printer; printer = printer->next) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(printer, "name");
        if (!printer->string || !*printer->string || !cJSON_IsObject(printer) || !cJSON_IsString(name))
            return false;
        if (settings.printer_name == name->valuestring) {
            if (printer_id) return false;
            printer_id = printer->string;
        }
    }
    if (!printer_id) return false;

    const cJSON* printer_mappings = cJSON_GetObjectItemCaseSensitive(mappings, printer_id);
    if (!printer_mappings) { mapping.spool_id = 0; mapping.stale = false; return true; }
    if (!cJSON_IsObject(printer_mappings)) return false;
    const std::string toolhead = std::to_string(settings.toolhead_id);
    const cJSON* entry = cJSON_GetObjectItemCaseSensitive(printer_mappings, toolhead.c_str());
    if (!entry) { mapping.spool_id = 0; mapping.stale = false; return true; }
    if (!cJSON_IsObject(entry) || !unsignedId(cJSON_GetObjectItemCaseSensitive(entry, "spool_id"), mapping.spool_id))
        return false;
    mapping.stale = false;
    return true;
}

bool parseSpool(const cJSON* root, uint32_t wanted_id, SpoolInfo& spool) {
    if (!cJSON_IsObject(root)) return false;
    const cJSON* filament = cJSON_GetObjectItemCaseSensitive(root, "filament");
    const cJSON* archived = cJSON_GetObjectItemCaseSensitive(root, "archived");
    const cJSON* weight = cJSON_GetObjectItemCaseSensitive(root, "remaining_weight");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(filament, "name");
    const cJSON* material = cJSON_GetObjectItemCaseSensitive(filament, "material");
    const cJSON* color = cJSON_GetObjectItemCaseSensitive(filament, "color_hex");
    const cJSON* vendor = cJSON_GetObjectItemCaseSensitive(filament, "vendor");
    const cJSON* vendor_name = cJSON_IsObject(vendor) ? cJSON_GetObjectItemCaseSensitive(vendor, "name") : nullptr;
    const bool name_ok = !name || cJSON_IsString(name) || cJSON_IsNull(name);
    const bool material_ok = !material || cJSON_IsString(material) || cJSON_IsNull(material);
    const bool color_ok = cJSON_IsString(color) || cJSON_IsNull(color);
    const bool vendor_ok = cJSON_IsNull(vendor) || (cJSON_IsObject(vendor) && cJSON_IsString(vendor_name));
    if (!cJSON_IsObject(filament) || !cJSON_IsBool(archived) || !name_ok ||
        !material_ok || !color_ok || !vendor_ok ||
        !unsignedId(id, spool.id) || spool.id != wanted_id) return false;
    if (weight && !cJSON_IsNull(weight)) {
        if (!cJSON_IsNumber(weight) || !std::isfinite(weight->valuedouble) || weight->valuedouble < 0)
            return false;
        spool.remaining_g = weight->valuedouble;
    }
    spool.archived = cJSON_IsTrue(archived);
    spool.vendor = cJSON_IsString(vendor_name) ? vendor_name->valuestring : "";
    spool.name = cJSON_IsString(name) ? name->valuestring : "";
    spool.material = cJSON_IsString(material) ? material->valuestring : "";
    spool.color_hex = cJSON_IsString(color) ? color->valuestring : "";
    return true;
}

bool parseSpool(const std::string& body, uint32_t wanted_id, SpoolInfo& spool) {
    Json root = parseJson(body);
    return parseSpool(root.get(), wanted_id, spool);
}
} // namespace

bool Services::getMapping(Mapping& out, ApiError& error) {
    HttpResponse response;
    const std::string url = settings_.current().filabridge_url + "/api/status";
    if (!transport_.request("GET", url.c_str(), "", response, error)) return false;
    if (response.status != 200) { error = ApiError::Http; return false; }
    Mapping candidate;
    if (!parseMapping(response.body, settings_.current(), candidate)) {
        error = ApiError::Malformed;
        return false;
    }
    out = candidate;
    error = ApiError::None;
    return true;
}

bool Services::getSpool(uint32_t id, SpoolInfo& out, ApiError& error) {
    if (id == 0) { error = ApiError::MissingSpool; return false; }
    HttpResponse response;
    const std::string url = settings_.current().spoolman_url + "/api/v1/spool/" + std::to_string(id);
    if (!transport_.request("GET", url.c_str(), "", response, error)) return false;
    if (response.status == 404) { error = ApiError::MissingSpool; return false; }
    if (response.status != 200) { error = ApiError::Http; return false; }
    SpoolInfo candidate;
    if (!parseSpool(response.body, id, candidate)) {
        error = ApiError::Malformed;
        return false;
    }
    if (candidate.archived) { error = ApiError::ArchivedSpool; return false; }
    out = std::move(candidate);
    error = ApiError::None;
    return true;
}

bool Services::scan(const TagRead& tag, ScanMatch& out, ApiError& error) {
    std::string uid = tag.uid;
    if (uid.empty() || uid.size() > 64 || uid.size() % 2) { error = ApiError::InvalidTag; return false; }
    for (char& ch : uid) {
        if (ch >= 'a' && ch <= 'f') ch -= 'a' - 'A';
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) {
            error = ApiError::InvalidTag;
            return false;
        }
    }

    Json request(cJSON_CreateObject(), cJSON_Delete);
    const std::string name = settings_.current().printer_name + " spool tap";
    const std::string& format = tag.format.empty() ? tag.type : tag.format;
    if (!request || !cJSON_AddStringToObject(request.get(), "uid", uid.c_str()) ||
        !cJSON_AddStringToObject(request.get(), "reader_id", settings_.current().reader_id.c_str()) ||
        !cJSON_AddStringToObject(request.get(), "name", name.c_str()) ||
        !cJSON_AddStringToObject(request.get(), "format", format.c_str())) {
        error = ApiError::Http;
        return false;
    }
    std::unique_ptr<char, decltype(&cJSON_free)> encoded(cJSON_PrintUnformatted(request.get()), cJSON_free);
    if (!encoded) { error = ApiError::Http; return false; }
    HttpResponse response;
    const std::string url = settings_.current().spoolman_url + "/api/v1/tag/scan";
    if (!transport_.request("POST", url.c_str(), encoded.get(), response, error)) return false;
    if (response.status == 400) { error = ApiError::InvalidTag; return false; }
    if (response.status != 200) { error = ApiError::Http; return false; }

    Json root = parseJson(response.body);
    const cJSON* echoed_uid = cJSON_GetObjectItemCaseSensitive(root.get(), "uid");
    const cJSON* reader_id = cJSON_GetObjectItemCaseSensitive(root.get(), "reader_id");
    const cJSON* spool_id = cJSON_GetObjectItemCaseSensitive(root.get(), "matched_spool_id");
    const cJSON* filament_id = cJSON_GetObjectItemCaseSensitive(root.get(), "matched_filament_id");
    const cJSON* spool_data = cJSON_GetObjectItemCaseSensitive(root.get(), "spool");
    const cJSON* filament_data = cJSON_GetObjectItemCaseSensitive(root.get(), "filament");
    ScanMatch candidate;
    bool valid = cJSON_IsObject(root.get()) && cJSON_IsString(echoed_uid) &&
                 uid == echoed_uid->valuestring && cJSON_IsString(reader_id) &&
                 settings_.current().reader_id == reader_id->valuestring &&
                 (cJSON_IsNull(spool_id) || cJSON_IsNumber(spool_id)) &&
                 (cJSON_IsNull(filament_id) || cJSON_IsNumber(filament_id)) &&
                 !(cJSON_IsNumber(spool_id) && cJSON_IsNumber(filament_id)) &&
                 (cJSON_IsNumber(spool_id) || !spool_data || cJSON_IsNull(spool_data)) &&
                 (cJSON_IsNumber(filament_id) || !filament_data || cJSON_IsNull(filament_data));
    if (valid && cJSON_IsNumber(spool_id)) {
        valid = unsignedId(spool_id, candidate.spool_id) && candidate.spool_id > 0 &&
                parseSpool(spool_data, candidate.spool_id, candidate.spool);
        if (valid && candidate.spool.archived) { error = ApiError::ArchivedSpool; return false; }
        candidate.kind = ScanKind::Spool;
    } else if (valid && cJSON_IsNumber(filament_id)) {
        valid = unsignedId(filament_id, candidate.filament_id) && candidate.filament_id > 0;
        candidate.kind = ScanKind::FilamentOnly;
    }
    if (!valid) { error = ApiError::Malformed; return false; }
    out = std::move(candidate);
    error = ApiError::None;
    return true;
}

bool Services::mapToolhead(uint32_t spool_id, Mapping& after, ApiError& error) {
    Json request(cJSON_CreateObject(), cJSON_Delete);
    if (!request ||
        !cJSON_AddStringToObject(request.get(), "printer_name", settings_.current().printer_name.c_str()) ||
        !cJSON_AddNumberToObject(request.get(), "toolhead_id", settings_.current().toolhead_id) ||
        !cJSON_AddNumberToObject(request.get(), "spool_id", spool_id)) {
        error = ApiError::Http;
        return false;
    }
    std::unique_ptr<char, decltype(&cJSON_free)> encoded(cJSON_PrintUnformatted(request.get()), cJSON_free);
    if (!encoded) { error = ApiError::Http; return false; }

    HttpResponse response;
    ApiError post_error = ApiError::None;
    const std::string url = settings_.current().filabridge_url + "/api/map_toolhead";
    const bool posted = transport_.request("POST", url.c_str(), encoded.get(), response, post_error);
    if (!posted && post_error == ApiError::None) post_error = ApiError::Http;
    if (posted) {
        if (response.status == 409) post_error = ApiError::Conflict;
        else if (response.status != 200) post_error = ApiError::Http;
        else {
            Json reply = parseJson(response.body);
            const cJSON* message = cJSON_GetObjectItemCaseSensitive(reply.get(), "message");
            if (!cJSON_IsObject(reply.get()) || !cJSON_IsString(message))
                post_error = ApiError::Malformed;
        }
    }

    if (between_requests_) between_requests_();
    Mapping observed;
    ApiError read_error = ApiError::None;
    if (!getMapping(observed, read_error)) { error = read_error; return false; }
    after = observed;
    if (!posted && post_error == ApiError::Timeout && observed.spool_id == spool_id) {
        error = ApiError::None;
        return true;
    }
    if (post_error != ApiError::None) { error = post_error; return false; }
    if (observed.spool_id != spool_id) { error = ApiError::Malformed; return false; }
    error = ApiError::None;
    return true;
}
