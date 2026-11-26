// WarRig Slicer: WarRig Link (see WarRigLink.hpp).
#include "WarRigLink.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <random>
#include <thread>
#include <cstdio>
#include <functional>

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/Utils/Http.hpp"
#include "../QIDI/QIDIDeviceApi.hpp"

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

namespace {

const char* NS      = "warrig_link";
const char* SECTION = "warrig_link";
const char* APP     = "WarRig Slicer";

std::string base_url(const std::string& host) { return QIDIDeviceApi::normalize_base_url(host); }

bool http_call(const std::string& method, const std::string& url, const std::string& body, std::string& out, long timeout_s = 8)
{
    bool ok = false;
    try {
        Http http = method == "POST" ? Http::post(url) : method == "DELETE" ? Http::del(url) : Http::get(url);
        http.timeout_connect(std::min<long>(timeout_s, 5)).timeout_max(timeout_s);
        if (!body.empty()) {
            http.header("Content-Type", "application/json");
            http.set_post_body(body);
        }
        http.on_complete([&](std::string b, unsigned status) { out = std::move(b); ok = status >= 200 && status < 300; })
            .on_error([&](std::string b, std::string err, unsigned status) {
                out = std::move(b);
                BOOST_LOG_TRIVIAL(info) << "WarRig Link: " << method << " " << url << " -> " << status << " " << err;
                ok = false;
            })
            .perform_sync();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "WarRig Link: " << url << " exception: " << e.what();
        ok = false;
    }
    return ok;
}

bool db_get(const std::string& host, const std::string& key, json& value)
{
    std::string body;
    if (!http_call("GET", base_url(host) + "/server/database/item?namespace=" + NS + "&key=" + key, {}, body)) return false;
    try {
        json j = json::parse(body);
        if (j.contains("result") && j["result"].contains("value")) { value = j["result"]["value"]; return true; }
    } catch (...) {}
    return false;
}

bool db_set(const std::string& host, const std::string& key, const json& value)
{
    json req = { { "namespace", NS }, { "key", key }, { "value", value } };
    std::string body;
    return http_call("POST", base_url(host) + "/server/database/item", req.dump(), body);
}

bool db_del(const std::string& host, const std::string& key)
{
    std::string body;
    return http_call("DELETE", base_url(host) + "/server/database/item?namespace=" + std::string(NS) + "&key=" + key, {}, body);
}

std::string random_from(const std::string& alphabet, size_t n)
{
    static std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    std::string s;
    for (size_t i = 0; i < n; ++i) s += alphabet[pick(gen)];
    return s;
}

std::string pc_name()
{
    std::string n = wxGetHostName().ToUTF8().data();
    return n.empty() ? std::string("This PC") : n;
}

// run `work` on a worker thread, then `done` on the UI thread if the dialog is still open
template<class R>
void in_background(std::shared_ptr<bool> alive, std::function<R()> work, std::function<void(R)> done)
{
    std::thread([alive, work, done]() {
        R r = work();
        wxGetApp().CallAfter([alive, done, r]() { if (*alive) done(r); });
    }).detach();
}

const wxColour kOrange(255, 122, 18), kMuted(160, 164, 172), kRed(232, 72, 60), kGreen(111, 224, 138);

} // namespace

// ---------------------------------------------------------------------------------------------
namespace WarRigLink {

LinkInfo get(const std::string& printer_name)
{
    LinkInfo info;
    info.name = printer_name;
    std::string raw = wxGetApp().app_config->get(SECTION, printer_name);
    if (raw.empty()) return info;
    try {
        json j = json::parse(raw);
        info.home = j.value("home", "");
        info.away = j.value("away", "");
        info.id   = j.value("id", "");
    } catch (...) {}
    return info;
}

bool is_linked(const std::string& printer_name) { return !get(printer_name).id.empty(); }

void set(const LinkInfo& info)
{
    json j = { { "home", info.home }, { "away", info.away }, { "id", info.id } };
    wxGetApp().app_config->set(SECTION, info.name, j.dump());
    wxGetApp().app_config->save();
}

void remove(const std::string& printer_name)
{
    wxGetApp().app_config->erase(SECTION, printer_name);
    wxGetApp().app_config->save();
}

std::vector<LinkInfo> all()
{
    std::vector<LinkInfo> out;
    if (!wxGetApp().app_config->has_section(SECTION)) return out;
    for (const auto& kv : wxGetApp().app_config->get_section(SECTION)) {
        LinkInfo i = get(kv.first);
        if (!i.id.empty()) out.push_back(i);
    }
    return out;
}

std::string normalize_away(const std::string& input, std::string& error)
{
    std::string s = input;
    s.erase(0, s.find_first_not_of(" \t"));
    while (!s.empty() && (s.back() == ' ' || s.back() == '/' || s.back() == '\t')) s.pop_back();
    if (s.empty()) { error.clear(); return {}; }
    std::string scheme, rest = s;
    auto p = s.find("://");
    if (p != std::string::npos) { scheme = s.substr(0, p); rest = s.substr(p + 3); }
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::string host = rest.substr(0, rest.find_first_of(":/"));
    std::string lhost = host;
    std::transform(lhost.begin(), lhost.end(), lhost.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (host.empty()) { error = "No address entered."; return {}; }

    bool tailscale = lhost.size() > 7 && lhost.compare(lhost.size() - 7, 7, ".ts.net") == 0;
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (sscanf(lhost.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && a == 100 && b >= 64 && b <= 127 && c < 256 && d < 256)
        tailscale = true;

    if (scheme == "https") return "https://" + rest;
    if (tailscale && (scheme.empty() || scheme == "http")) return "http://" + rest;
    error = "Use a Tailscale address (100.x.x.x or name.ts.net) or a secure https:// address. "
            "Plain internet addresses are refused so the printer is never open to the internet.";
    return {};
}

std::string pick_route(const LinkInfo& info)
{
    std::string body;
    if (!info.home.empty() && http_call("GET", base_url(info.home) + "/server/info", {}, body, 3)) return info.home;
    if (!info.away.empty() && http_call("GET", base_url(info.away) + "/server/info", {}, body, 6)) return info.away;
    return {};
}

bool apply_route(const std::string& printer_name, const std::string& host)
{
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    PhysicalPrinter* p = printers.find_printer(printer_name);
    // the Device tab wants a bare host for plain http (it adds the ports itself); https stays a full address
    const std::string target = host.rfind("http://", 0) == 0 ? host.substr(7) : host;
    if (!p || target.empty() || p->config.opt_string("print_host") == target) return false;
    PhysicalPrinter copy = *p;
    copy.config.set_key_value("print_host", new ConfigOptionString(target));
    printers.save_printer(copy);
    BOOST_LOG_TRIVIAL(info) << "WarRig Link: " << printer_name << " now uses " << target;
    return true;
}

} // namespace WarRigLink

// ---------------------------------------------------------------------------------------------
WarRigLinkDialog::WarRigLinkDialog(wxWindow* parent, const std::string& printer_name)
    : wxDialog(parent, wxID_ANY, _L("WarRig Link"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_alive(std::make_shared<bool>(true))
    , m_printer_name(printer_name)
    , m_timer(this)
{
    SetBackgroundColour(wxColour(38, 40, 45));
    SetForegroundColour(wxColour(232, 232, 234));
    m_main = new wxBoxSizer(wxVERTICAL);
    m_manage = !printer_name.empty() && WarRigLink::is_linked(printer_name);
    if (m_manage) build_manage(); else build_new_link();
    SetSizerAndFit(m_main);
    SetMinSize(wxSize(FromDIP(480), -1));
    Fit();
    CenterOnParent();
    Bind(wxEVT_TIMER, &WarRigLinkDialog::on_tick, this, m_timer.GetId());
    wxGetApp().UpdateDlgDarkUI(this);
}

WarRigLinkDialog::~WarRigLinkDialog()
{
    *m_alive = false;
    m_timer.Stop();
}

void WarRigLinkDialog::set_status(const wxString& text, bool error)
{
    if (!m_status) return;
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? kRed : kMuted);
    m_status->Wrap(FromDIP(440));
    Layout();
    Fit();
}

std::string WarRigLinkDialog::selected_host() const
{
    if (!m_choice || m_choice->GetSelection() == wxNOT_FOUND) return {};
    const std::string name = m_names[m_choice->GetSelection()];
    const PhysicalPrinter* p = wxGetApp().preset_bundle->physical_printers.find_printer(name);
    return p ? p->config.opt_string("print_host") : std::string();
}

void WarRigLinkDialog::build_new_link()
{
    const int pad = FromDIP(16);
    auto title = new wxStaticText(this, wxID_ANY, _L("Link a printer"));
    title->SetFont(title->GetFont().Bold().Scaled(1.3f));
    m_main->Add(title, 0, wxALL, pad);

    auto intro = new wxStaticText(this, wxID_ANY,
        _L("Pick the printer. The WarRig screen will show a QR code: scan it with your phone to approve, or tap Allow on the screen. "
           "Do this once at home; after that the printer works from anywhere."));
    intro->SetForegroundColour(kMuted);
    intro->Wrap(FromDIP(440));
    m_main->Add(intro, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

    for (auto it = wxGetApp().preset_bundle->physical_printers.begin(); it != wxGetApp().preset_bundle->physical_printers.end(); ++it)
        if (!WarRigLink::is_linked(it->name)) m_names.push_back(it->name);

    if (m_names.empty()) {
        auto none = new wxStaticText(this, wxID_ANY, _L("Add the printer with + on the Device tab first (home network), then link it here."));
        none->Wrap(FromDIP(440));
        m_main->Add(none, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    } else {
        wxArrayString items;
        for (auto& n : m_names) {
            const PhysicalPrinter* p = wxGetApp().preset_bundle->physical_printers.find_printer(n);
            items.Add(wxString::FromUTF8(n) + "   (" + wxString::FromUTF8(p ? p->config.opt_string("print_host") : "") + ")");
        }
        m_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(440), -1), items);
        int sel = 0;
        for (size_t i = 0; i < m_names.size(); ++i) if (m_names[i] == m_printer_name) sel = (int)i;
        m_choice->SetSelection(sel);
        m_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { on_printer_choice(); });
        m_main->Add(m_choice, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

        auto away_lbl = new wxStaticText(this, wxID_ANY, _L("Away address (optional): Tailscale 100.x.x.x / name.ts.net, or https://"));
        away_lbl->SetForegroundColour(kMuted);
        m_main->Add(away_lbl, 0, wxLEFT | wxRIGHT, pad);
        m_away = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(440), -1));
        m_away->SetHint(_L("Leave empty to use the one set on the printer"));
        m_main->Add(m_away, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, FromDIP(6));
    }

    m_code_text = new wxStaticText(this, wxID_ANY, "");
    m_code_text->SetFont(m_code_text->GetFont().Bold().Scaled(1.8f));
    m_code_text->SetForegroundColour(kOrange);
    m_code_text->Hide();
    m_main->Add(m_code_text, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    m_status = new wxStaticText(this, wxID_ANY, "");
    m_status->SetForegroundColour(kMuted);
    m_main->Add(m_status, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    auto buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto close = new wxButton(this, wxID_CANCEL, _L("Close"));
    buttons->Add(close, 0, wxRIGHT, FromDIP(8));
    m_send = new wxButton(this, wxID_ANY, _L("Send link request"));
    m_send->Enable(false);
    m_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_send_request(); });
    buttons->Add(m_send, 0);
    m_main->Add(buttons, 0, wxEXPAND | wxALL, pad);

    if (m_choice) on_printer_choice();
}

void WarRigLinkDialog::on_printer_choice()
{
    m_host = selected_host();
    if (m_send) m_send->Enable(false);
    set_status(_L("Checking the printer for WarRig..."));
    std::string host = m_host;
    in_background<int>(m_alive, [host]() {
        // ask the printer directly (the cached answer may belong to another printer)
        std::string body;
        if (!http_call("GET", base_url(host) + "/printer/objects/list", {}, body, 6)) return -1;
        std::transform(body.begin(), body.end(), body.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (body.find("gcode_macro warrig") != std::string::npos || body.find("gcode_macro wrt_") != std::string::npos) return 1;
        std::string cfg;
        if (http_call("GET", base_url(host) + "/server/files/list?root=config", {}, cfg, 6)) {
            std::transform(cfg.begin(), cfg.end(), cfg.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (cfg.find("warrig") != std::string::npos) return 1;
        }
        return 0;
    }, [this, host](int st) {
        if (host != m_host) return;
        if (st == 1) { set_status(_L("WarRig found. Ready to link.")); if (m_send) m_send->Enable(true); }
        else if (st == 0) set_status(_L("WarRig wasn't found on this printer. Only printers running WarRig can be linked."), true);
        else set_status(_L("The printer didn't answer. Check it's on and on the same network as this PC."), true);
    });
}

void WarRigLinkDialog::on_send_request()
{
    if (m_host.empty()) return;
    std::string err;
    std::string away = m_away ? WarRigLink::normalize_away(m_away->GetValue().ToUTF8().data(), err) : std::string();
    if (!err.empty()) { set_status(wxString::FromUTF8(err), true); return; }

    m_request_id = random_from("0123456789abcdef", 12);
    std::string raw = random_from("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", 6);
    m_code = raw.substr(0, 3) + "-" + raw.substr(3);
    const long now = (long)std::time(nullptr);
    m_expires = now + 300;
    m_printer_name = m_names[m_choice->GetSelection()];
    m_send->Enable(false);
    m_choice->Enable(false);
    if (m_away) m_away->Enable(false);
    set_status(_L("Sending the link request..."));

    json req = { { "app", APP }, { "device", pc_name() }, { "code", m_code }, { "created", now }, { "expires", m_expires }, { "status", "pending" } };
    std::string host = m_host, id = m_request_id;
    in_background<bool>(m_alive, [host, id, req]() {
        bool ok = db_set(host, "requests." + id, req);
        // tell the screen and anyone watching the console
        std::string body;
        json g = { { "script", "RESPOND PREFIX=\"WarRig Link\" MSG=\"Link request from " + req["device"].get<std::string>() + " - code " + req["code"].get<std::string>() + "\"" } };
        if (ok) http_call("POST", base_url(host) + "/printer/gcode/script", g.dump(), body, 5);
        return ok;
    }, [this](bool ok) {
        if (!ok) {
            set_status(_L("The printer refused the request. Make sure WarRig is up to date."), true);
            m_send->Enable(true); m_choice->Enable(true); if (m_away) m_away->Enable(true);
            return;
        }
        m_code_text->SetLabel(wxString::FromUTF8(m_code));
        m_code_text->Show();
        m_timer.Start(1000);
        tick(); // show the countdown straight away
    });
}

void WarRigLinkDialog::on_tick(wxTimerEvent&) { tick(); }

void WarRigLinkDialog::tick()
{
    const long left = m_expires - (long)std::time(nullptr);
    if (left <= 0) {
        m_timer.Stop();
        set_status(_L("The request ran out of time. Send it again when you're at the printer."), true);
        m_code_text->Hide();
        m_send->Enable(true); m_choice->Enable(true); if (m_away) m_away->Enable(true);
        std::string host = m_host, id = m_request_id;
        std::thread([host, id]() { db_del(host, "requests." + id); }).detach();
        return;
    }
    set_status(wxString::Format(_L("Waiting for approval on the printer... %d:%02d\nOn the WarRig screen: scan the QR code with your phone, or tap Allow. The code above should match the screen."),
                                int(left / 60), int(left % 60)));
    if (left % 2 == 0 && !m_polling) poll_request();
}

void WarRigLinkDialog::poll_request()
{
    m_polling = true;
    std::string host = m_host, id = m_request_id;
    in_background<std::string>(m_alive, [host, id]() {
        json v;
        if (!db_get(host, "requests." + id, v) || !v.is_object()) return std::string("missing");
        return v.value("status", std::string("pending"));
    }, [this](std::string st) {
        m_polling = false;
        if (!m_timer.IsRunning()) return;
        if (st == "approved") { m_timer.Stop(); finish_link(); }
        else if (st == "denied" || st == "missing") {
            m_timer.Stop();
            m_code_text->Hide();
            set_status(st == "denied" ? _L("The link was turned down on the printer.") : _L("The request was removed on the printer."), true);
            m_send->Enable(true); m_choice->Enable(true); if (m_away) m_away->Enable(true);
        }
    });
}

void WarRigLinkDialog::finish_link()
{
    set_status(_L("Approved. Finishing the link..."));
    std::string err;
    std::string away_typed = m_away ? WarRigLink::normalize_away(m_away->GetValue().ToUTF8().data(), err) : std::string();
    std::string host = m_host, id = m_request_id;
    struct Result { std::string away, key; bool ok; };
    in_background<Result>(m_alive, [host, id, away_typed]() {
        Result r { away_typed, {}, true };
        json v;
        if (r.away.empty() && db_get(host, "away", v) && v.is_string()) {
            std::string e;
            r.away = WarRigLink::normalize_away(v.get<std::string>(), e);
        } else if (!r.away.empty()) {
            db_set(host, "away", r.away);
        }
        std::string body;
        if (http_call("GET", base_url(host) + "/access/api_key", {}, body, 5)) {
            try { json j = json::parse(body); if (j.contains("result") && j["result"].is_string()) r.key = j["result"].get<std::string>(); } catch (...) {}
        }
        r.ok = db_set(host, "devices." + id, json { { "app", APP }, { "device", pc_name() }, { "linked", (long)std::time(nullptr) } });
        db_del(host, "requests." + id);
        return r;
    }, [this, host, id](Result r) {
        if (!r.ok) { set_status(_L("The printer approved the link but couldn't save it. Try again."), true); return; }
        WarRigLink::LinkInfo info { m_printer_name, host, r.away, id };
        WarRigLink::set(info);
        if (!r.key.empty()) {
            PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
            if (PhysicalPrinter* p = printers.find_printer(m_printer_name)) {
                PhysicalPrinter copy = *p;
                copy.config.set_key_value("printhost_apikey", new ConfigOptionString(r.key));
                printers.save_printer(copy);
            }
        }
        m_changed = true;
        m_code_text->Hide();
        set_status(r.away.empty()
            ? _L("Linked. This printer is now under WarRig Link.\nNo away address is set yet, so it only works at home. Add one here or on the printer to print from anywhere.")
            : _L("Linked. This printer is now under WarRig Link and works at home or away."));
        m_status->SetForegroundColour(kGreen);
        m_send->Hide();
        Layout(); Fit();
    });
}

void WarRigLinkDialog::build_manage()
{
    const int pad = FromDIP(16);
    WarRigLink::LinkInfo info = WarRigLink::get(m_printer_name);
    m_host = info.home;

    auto title = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(m_printer_name) + " - " + _L("WarRig Link"));
    title->SetFont(title->GetFont().Bold().Scaled(1.3f));
    m_main->Add(title, 0, wxALL, pad);

    auto grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(12));
    grid->AddGrowableCol(1);
    auto lbl = [this](const wxString& t) { auto s = new wxStaticText(this, wxID_ANY, t); s->SetForegroundColour(kMuted); return s; };
    grid->Add(lbl(_L("Home address")), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(info.home)), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(lbl(_L("Away address")), 0, wxALIGN_CENTER_VERTICAL);
    m_away = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(info.away), wxDefaultPosition, wxSize(FromDIP(300), -1));
    m_away->SetHint(_L("100.x.x.x, name.ts.net or https://"));
    grid->Add(m_away, 1, wxEXPAND);
    grid->Add(lbl(_L("Using now")), 0, wxALIGN_CENTER_VERTICAL);
    auto route = new wxStaticText(this, wxID_ANY, _L("checking..."));
    grid->Add(route, 0, wxALIGN_CENTER_VERTICAL);
    m_main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    m_main->Add(lbl(_L("Linked to this printer")), 0, wxLEFT | wxRIGHT | wxTOP, pad);
    m_devices = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(440), FromDIP(90)));
    m_devices->SetBackgroundColour(wxColour(31, 33, 37));
    m_devices->SetForegroundColour(wxColour(232, 232, 234));
    m_main->Add(m_devices, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
    auto shared = lbl(_L("This list is kept on the printer, so WarRig Companion shows the same links."));
    m_main->Add(shared, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    m_status = new wxStaticText(this, wxID_ANY, "");
    m_main->Add(m_status, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    auto buttons = new wxBoxSizer(wxHORIZONTAL);
    auto unlink = new wxButton(this, wxID_ANY, _L("Unlink"));
    buttons->Add(unlink, 0);
    buttons->AddStretchSpacer(1);
    buttons->Add(new wxButton(this, wxID_CANCEL, _L("Close")), 0, wxRIGHT, FromDIP(8));
    auto save = new wxButton(this, wxID_ANY, _L("Save"));
    buttons->Add(save, 0);
    m_main->Add(buttons, 0, wxEXPAND | wxALL, pad);

    // fill the devices list and the route from the printer
    in_background<std::pair<std::vector<std::string>, std::string>>(m_alive, [info]() {
        std::pair<std::vector<std::string>, std::string> r;
        std::string route = WarRigLink::pick_route(info);
        r.second = route;
        json v;
        if (!route.empty() && db_get(route, "devices", v) && v.is_object()) {
            for (auto it = v.begin(); it != v.end(); ++it) {
                const json& d = it.value();
                std::string line = (d.is_object() ? d.value("app", std::string("?")) + "  -  " + d.value("device", std::string("")) : std::string("?"));
                if (it.key() == info.id) line += "   (this PC)";
                r.first.push_back(line);
            }
        }
        return r;
    }, [this, route, info](std::pair<std::vector<std::string>, std::string> r) {
        route->SetLabel(r.second.empty() ? _L("not reachable right now") : (r.second == info.home ? _L("Home network") : _L("Away address")));
        for (auto& l : r.first) m_devices->Append(wxString::FromUTF8(l));
        if (r.first.empty()) m_devices->Append(_L("(list not available)"));
        Layout();
    });

    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        std::string err;
        std::string away = WarRigLink::normalize_away(m_away->GetValue().ToUTF8().data(), err);
        if (!err.empty()) { set_status(wxString::FromUTF8(err), true); return; }
        WarRigLink::LinkInfo i = WarRigLink::get(m_printer_name);
        i.away = away;
        WarRigLink::set(i);
        m_changed = true;
        std::string host = i.home;
        std::thread([host, away]() { db_set(host, "away", away); }).detach();
        set_status(_L("Saved."));
    });
    unlink->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxMessageBox(_L("Unlink this printer? It stays on the Device tab for the home network."), _L("WarRig Link"), wxYES_NO | wxNO_DEFAULT, this) != wxYES)
            return;
        WarRigLink::LinkInfo i = WarRigLink::get(m_printer_name);
        std::thread([i]() { std::string r = WarRigLink::pick_route(i); if (!r.empty()) db_del(r, "devices." + i.id); }).detach();
        WarRigLink::apply_route(m_printer_name, i.home);
        WarRigLink::remove(m_printer_name);
        m_changed = true;
        EndModal(wxID_OK);
    });
}

}} // namespace Slic3r::GUI
