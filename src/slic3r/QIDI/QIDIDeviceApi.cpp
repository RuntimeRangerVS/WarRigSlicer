// WarRig Slicer: local-network device API (replaces QIDI's unpublished QIDIDeviceApi).
#include "QIDIDeviceApi.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>
#include <thread>
#include <mutex>
#include <map>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <algorithm>
#include "nlohmann/json.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

std::string QIDIDeviceApi::normalize_base_url(const std::string& url)
{
    std::string u = url;
    while (!u.empty() && (u.back() == '/' || u.back() == ' ')) u.pop_back();
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) u = "http://" + u;
    return u;
}

static bool http_get(const std::string& url, std::string& body, const std::string& api_key = {}, long timeout_s = 10)
{
    bool ok = false;
    try {
        auto http = Http::get(url);
        http.timeout_connect(5).timeout_max(timeout_s);
        if (!api_key.empty()) http.header("X-Api-Key", api_key);
        http.on_complete([&](std::string b, unsigned status) {
                body = std::move(b);
                ok   = status == 200;
            })
            .on_error([&](std::string b, std::string err, unsigned status) {
                BOOST_LOG_TRIVIAL(warning) << "WarRig: GET " << url << " failed: " << status << " " << err;
                ok = false;
            })
            .perform_sync();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "WarRig: GET " << url << " exception: " << e.what();
        ok = false;
    }
    return ok;
}

namespace {
struct WarRigDetectState { int state = -1; std::time_t checked = 0; bool busy = false; };
std::mutex                               g_warrig_mutex;
std::map<std::string, WarRigDetectState> g_warrig_detect;
}

int QIDIDeviceApi::warrig_detect(const std::string& base_url, std::function<void()> on_change)
{
    if (base_url.empty()) return -1;
    const std::string url = normalize_base_url(base_url);
    const std::time_t now = std::time(nullptr);
    {
        std::lock_guard<std::mutex> lock(g_warrig_mutex);
        WarRigDetectState& e = g_warrig_detect[url];
        // a positive answer sticks for the session; a negative one is re-checked after a minute
        if (e.busy || e.state == 1 || (e.state == 0 && now - e.checked < 60))
            return e.state;
        e.busy = true;
    }
    std::thread([url, on_change]() {
        std::string body;
        int st = -1;
        if (http_get(url + "/printer/objects/list", body, {}, 8)) {
            std::string low = body;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            st = low.find("gcode_macro warrig") != std::string::npos ? 1 : 0;
        }
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_warrig_mutex);
            WarRigDetectState& e = g_warrig_detect[url];
            changed   = st != -1 && st != e.state;
            if (st != -1) e.state = st;
            e.checked = std::time(nullptr);
            e.busy    = false;
        }
        BOOST_LOG_TRIVIAL(info) << "WarRig: detection on " << url << " = " << st;
        if (changed && on_change) on_change();
    }).detach();
    return -1;
}

bool QIDIDeviceApi::get_file_list(const std::string& base_url, std::string& body)
{
    return http_get(normalize_base_url(base_url) + "/server/files/list?root=gcodes", body);
}

bool QIDIDeviceApi::get_timelapse_directory(const std::string& base_url, std::string& body)
{
    return http_get(normalize_base_url(base_url) + "/server/files/directory?path=timelapse&extended=true", body);
}

bool QIDIDeviceApi::get_system_info(const std::string& base_url, std::string& body)
{
    return http_get(normalize_base_url(base_url) + "/machine/system_info", body);
}

bool QIDIDeviceApi::get_client_info(const std::string& base_url, std::string& body)
{
    // Only used to spot QIDI's cloud agent; WarRig printers don't run it.
    body = "{\"result\":{\"clients\":[]}}";
    return true;
}

bool QIDIDeviceApi::fetch_url_to_buffer(const std::string& url, std::vector<char>& out, int timeout_s)
{
    std::string body;
    if (!http_get(url, body, {}, timeout_s)) return false;
    out.assign(body.begin(), body.end());
    return !out.empty();
}

void QIDIDeviceApi::download_file_async(const std::string& base_url, const std::string& remote_name, const std::string& local_path,
                                        int timeout_s, const std::string& temp_suffix, std::function<void(bool, std::string)> on_done)
{
    const std::string url = normalize_base_url(base_url) + "/server/files/gcodes/" + Http::url_encode(remote_name);
    std::thread([url, local_path, timeout_s, temp_suffix, on_done]() {
        std::string body;
        bool ok = http_get(url, body, {}, timeout_s);
        if (ok) {
            const std::string tmp = local_path + temp_suffix;
            boost::nowide::ofstream f(tmp, std::ios::binary);
            f.write(body.data(), (std::streamsize) body.size());
            f.close();
            boost::system::error_code ec;
            boost::filesystem::rename(tmp, local_path, ec);
            ok = !ec;
        }
        if (on_done) on_done(ok, ok ? local_path : std::string());
    }).detach();
}

// ---- QIDI BOX: read Klipper's saved variables (slotN, filament_slotN, color_slotN, vendor_slotN) ----

static bool query_save_variables(const std::string& url, const std::string& api_key, json& vars)
{
    std::string body;
    if (!http_get(QIDIDeviceApi::normalize_base_url(url) + "/printer/objects/query?save_variables=variables", body, api_key))
        return false;
    try {
        json j = json::parse(body);
        vars   = j.at("result").at("status").at("save_variables").at("variables");
        return vars.is_object();
    } catch (...) {
        return false;
    }
}

static int as_int(const json& v, int def)
{
    try {
        if (v.is_number()) return v.get<int>();
        if (v.is_string()) return std::stoi(v.get<std::string>());
        if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    } catch (...) {}
    return def;
}

bool QIDIDeviceApi::get_box_state(wxString& msg, const std::string& url, const std::string& api_key)
{
    json vars;
    if (!query_save_variables(url, api_key, vars)) {
        msg = "Could not read the printer";
        return false;
    }
    const int count  = vars.contains("box_count") ? as_int(vars["box_count"], 0) : 0;
    const int enable = vars.contains("enable_box") ? as_int(vars["enable_box"], 1) : 1;
    return count > 0 && enable != 0;
}

Box_info QIDIDeviceApi::get_box_info(wxString& msg, const std::string& url, const std::string& api_key)
{
    Box_info info;
    json     vars;
    if (!query_save_variables(url, api_key, vars)) {
        msg = "Could not read the printer";
        return info;
    }
    info.box_count          = std::min(4, std::max(0, vars.contains("box_count") ? as_int(vars["box_count"], 0) : 0));
    info.auto_reload_detect = vars.contains("auto_reload_detect") ? as_int(vars["auto_reload_detect"], 0) : 0;
    for (int i = 0; i < Box_info::kSlots; ++i) {
        const std::string n = std::to_string(i);
        if (vars.contains("slot" + n)) info.slot_state[i] = as_int(vars["slot" + n], 0);
        if (vars.contains("vendor_slot" + n)) info.filament_vendor[i] = as_int(vars["vendor_slot" + n], 1);
        if (vars.contains("filament_slot" + n)) {
            const json& f = vars["filament_slot" + n];
            const int   idx = as_int(f, -1);
            if (idx >= 0)
                info.filament_index[i] = idx;
            else if (f.is_string() && !f.get<std::string>().empty())
                info.filament_type[i] = f.get<std::string>();
        }
        if (vars.contains("color_slot" + n)) {
            const json& c = vars["color_slot" + n];
            std::string hex;
            if (c.is_array() && !c.empty() && c[0].is_string()) hex = c[0].get<std::string>();
            else if (c.is_string()) hex = c.get<std::string>();
            if (!hex.empty() && hex[0] != '#') hex = "#" + hex;
            if (hex.size() == 7 || hex.size() == 9) info.filament_colors[i] = hex.substr(0, 7);
        }
    }
    return info;
}

void QIDIDeviceApi::get_color_filament_str(wxString& msg, Box_info& info, const std::string& url, const std::string& api_key)
{
    // Map numeric filament indices to material names using the printer's own filament list, when it has one.
    std::string body;
    if (!http_get(normalize_base_url(url) + "/server/files/config/officiall_filas_list.cfg", body, api_key)) return;
    std::map<int, std::string> types;
    int  current = -1;
    std::istringstream in(body);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() > 6 && line.rfind("[fila", 0) == 0 && line.back() == ']') {
            try { current = std::stoi(line.substr(5, line.size() - 6)); } catch (...) { current = -1; }
        } else if (current >= 0 && line.rfind("type", 0) == 0) {
            auto p = line.find_first_of("=:");
            if (p != std::string::npos) {
                std::string t = line.substr(p + 1);
                t.erase(0, t.find_first_not_of(' '));
                types[current] = t;
            }
        }
    }
    for (int i = 0; i < Box_info::kSlots; ++i) {
        auto it = types.find(info.filament_index[i]);
        if (it != types.end() && !it->second.empty()) info.filament_type[i] = it->second;
    }
}

}} // namespace Slic3r::GUI
