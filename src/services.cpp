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

bool parseSpool(const std::string& body, uint32_t wanted_id, SpoolInfo& spool) {
    Json root = parseJson(body);
    if (!root || !cJSON_IsObject(root.get())) return false;
    const cJSON* filament = cJSON_GetObjectItemCaseSensitive(root.get(), "filament");
    const cJSON* archived = cJSON_GetObjectItemCaseSensitive(root.get(), "archived");
    const cJSON* weight = cJSON_GetObjectItemCaseSensitive(root.get(), "remaining_weight");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root.get(), "id");
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
