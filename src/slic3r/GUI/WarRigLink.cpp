// WarRig Slicer: WarRig Link (see WarRigLink.hpp).
#include "WarRigLink.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
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

const char* SECTION = "warrig_link";

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

std::string url_param(const std::string& link, const std::string& name)
{
    auto q = link.find('?');
    if (q == std::string::npos) return {};
    std::string query = link.substr(q + 1);
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        auto eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == name) {
            std::string v = part.substr(eq + 1), out;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '%' && i + 2 < v.size()) { out += (char)std::stoi(v.substr(i + 1, 2), nullptr, 16); i += 2; }
                else out += v[i] == '+' ? ' ' : v[i];
            }
            return out;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
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

    // a single-label name (e.g. "warrig-q2") only resolves inside the tailnet (Tailscale MagicDNS)
    if (lhost.find('.') == std::string::npos && lhost != "localhost") tailscale = true;
    if (scheme == "https") return "https://" + rest;
    if (tailscale && (scheme.empty() || scheme == "http")) return "http://" + rest;
    error = "Use a Tailscale address (100.x.x.x, warrig-q2 or name.ts.net) or a secure https:// address. "
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
    SetMinSize(wxSize(FromDIP(500), -1));
    Fit();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

WarRigLinkDialog::~WarRigLinkDialog()
{
    *m_alive = false;
    m_timer.Stop();
}

void WarRigLinkDialog::on_tick(wxTimerEvent&) { tick(); }
void WarRigLinkDialog::tick() {}
void WarRigLinkDialog::poll_request() {}
void WarRigLinkDialog::finish_link() {}

void WarRigLinkDialog::set_status(const wxString& text, bool error)
{
    if (!m_status) return;
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? kRed : kMuted);
    m_status->Wrap(FromDIP(460));
    Layout();
    Fit();
}

std::string WarRigLinkDialog::selected_host() const
{
    if (!m_choice || m_choice->GetSelection() == wxNOT_FOUND || m_choice->GetSelection() >= (int)m_names.size()) return {};
    const PhysicalPrinter* p = wxGetApp().preset_bundle->physical_printers.find_printer(m_names[m_choice->GetSelection()]);
    return p ? p->config.opt_string("print_host") : std::string();
}

void WarRigLinkDialog::build_new_link()
{
    const int pad = FromDIP(16);
    auto title = new wxStaticText(this, wxID_ANY, _L("Link a WarRig printer"));
    title->SetFont(title->GetFont().Bold().Scaled(1.3f));
    m_main->Add(title, 0, wxALL, pad);

    auto intro = new wxStaticText(this, wxID_ANY,
        _L("On the printer screen open Wi-Fi > Remote access & phone alerts (or option 6 in the WarRig PC tool). "
           "Scan the QR code with your phone and paste the link here, or just pick the printer below."));
    intro->SetForegroundColour(kMuted);
    intro->Wrap(FromDIP(460));
    m_main->Add(intro, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

    auto link_lbl = new wxStaticText(this, wxID_ANY, _L("WarRig link from the QR code"));
    link_lbl->SetForegroundColour(kMuted);
    m_main->Add(link_lbl, 0, wxLEFT | wxRIGHT, pad);
    m_code_text = nullptr;
    m_link = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(460), -1));
    m_link->SetHint("warrig://link?v=1&host=192.168.0.66");
    m_main->Add(m_link, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));

    auto or_lbl = new wxStaticText(this, wxID_ANY, _L("or pick a printer from the Device tab"));
    or_lbl->SetForegroundColour(kMuted);
    m_main->Add(or_lbl, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    wxArrayString items;
    items.Add(_L("(use the link above)"));
    m_names.push_back("");
    for (auto it = wxGetApp().preset_bundle->physical_printers.begin(); it != wxGetApp().preset_bundle->physical_printers.end(); ++it) {
        if (WarRigLink::is_linked(it->name)) continue;
        m_names.push_back(it->name);
        items.Add(wxString::FromUTF8(it->name) + "   (" + wxString::FromUTF8(it->config.opt_string("print_host")) + ")");
    }
    m_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(460), -1), items);
    int sel = 0;
    for (size_t i = 1; i < m_names.size(); ++i) if (m_names[i] == m_printer_name) sel = (int)i;
    m_choice->SetSelection(sel);
    m_main->Add(m_choice, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    auto away_lbl = new wxStaticText(this, wxID_ANY, _L("Away address (filled in from the link when Tailscale is set up)"));
    away_lbl->SetForegroundColour(kMuted);
    m_main->Add(away_lbl, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    m_away = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(460), -1));
    m_away->SetHint(_L("warrig-q2, 100.x.x.x, name.ts.net or https://"));
    m_main->Add(m_away, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
    m_link->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        const std::string link = m_link->GetValue().ToUTF8().data();
        // away address: "remote" (WarRig Link, the printer's Tailscale IP) or "ts" (older warrig://connect codes)
        std::string away = url_param(link, "remote");
        if (away.empty()) away = url_param(link, "ts");
        if (!away.empty() && m_away->GetValue().IsEmpty()) m_away->SetValue(wxString::FromUTF8(away));
        if (!url_param(link, "host").empty()) m_choice->SetSelection(0);
    });

    m_status = new wxStaticText(this, wxID_ANY, "");
    m_status->SetForegroundColour(kMuted);
    m_main->Add(m_status, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    auto buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    buttons->Add(new wxButton(this, wxID_CANCEL, _L("Close")), 0, wxRIGHT, FromDIP(8));
    m_send = new wxButton(this, wxID_ANY, _L("Link"));
    m_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_send_request(); });
    buttons->Add(m_send, 0);
    m_main->Add(buttons, 0, wxEXPAND | wxALL, pad);
}

void WarRigLinkDialog::on_printer_choice() {}

void WarRigLinkDialog::on_send_request()
{
    // where is the printer: the link's host, or the picked Device-tab printer
    const std::string link = m_link->GetValue().ToUTF8().data();
    std::string host = url_param(link, "host");
    if (!link.empty() && link.rfind("warrig://", 0) != 0) { set_status(_L("That isn't a WarRig link. It starts with warrig://link"), true); return; }
    // WarRig Link format version: refuse a newer one than this slicer knows (older warrig://connect codes have none)
    const std::string ver = url_param(link, "v");
    if (!ver.empty() && std::atoi(ver.c_str()) > 1) { set_status(_L("This link is from a newer WarRig. Update WarRig Slicer."), true); return; }
    // the Device tab wants a bare host (it adds the ports itself): drop a ":7125" style port from the link
    {
        auto colon = host.rfind(':');
        if (colon != std::string::npos && colon + 1 < host.size() && host.find_first_not_of("0123456789", colon + 1) == std::string::npos)
            host.erase(colon);
    }
    if (host.empty()) host = selected_host();
    if (host.empty()) { set_status(_L("Paste the link from the printer's QR code, or pick a printer."), true); return; }
    std::string err;
    const std::string away = WarRigLink::normalize_away(m_away->GetValue().ToUTF8().data(), err);
    if (!err.empty()) { set_status(wxString::FromUTF8(err), true); return; }

    // which Device-tab printer: the picked one, or the one already using this address
    std::string name = (m_choice->GetSelection() > 0) ? m_names[m_choice->GetSelection()] : std::string();
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    if (name.empty())
        for (auto it = printers.begin(); it != printers.end(); ++it)
            if (it->config.opt_string("print_host") == host) name = it->name;
    m_host = host;
    m_send->Enable(false);
    set_status(_L("Checking the WarRig screen..."));

    in_background<std::string>(m_alive, [host]() {
        std::string info;
        if (!QIDIDeviceApi::warrig_info(host, info)) return std::string();
        QIDIDeviceApi::warrig_heartbeat(host);
        return info;
    }, [this, host, away, name](std::string info) mutable {
        m_send->Enable(true);
        if (info.empty()) {
            set_status(_L("No WarRig screen answered at this address. Check the printer is on, running WarRig, and on the same network as this PC."), true);
            return;
        }
        PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
        if (name.empty()) {
            // not on the Device tab yet: copy the first printer's settings with this address
            auto first = printers.begin();
            if (first == printers.end()) {
                set_status(_L("Add the printer once with + on the Device tab, then link it here."), true);
                return;
            }
            std::string base = "Q2 - WarRig";
            name = base;
            for (int n = 2; printers.find_printer(name); ++n) name = base + " " + std::to_string(n);
            PhysicalPrinter copy = *first;
            copy.name = name;
            copy.file.clear();
            copy.config.set_key_value("print_host", new ConfigOptionString(host));
            printers.save_printer(copy);
        }
        WarRigLink::LinkInfo li { name, host, away, "slicer" };
        WarRigLink::set(li);
        m_changed = true;
        std::string build;
        try { json j = json::parse(info); build = j.value("version", std::string("V1")) + " build " + std::to_string(j.value("build", 0)); } catch (...) {}
        set_status(wxString::Format(_L("Linked to %s (WarRig %s).\n%s"), wxString::FromUTF8(name), wxString::FromUTF8(build),
                                    away.empty() ? _L("No away address, so it works at home only. Set up Tailscale with option 6 in the WarRig PC tool to print from anywhere.")
                                                 : _L("At home it uses the home address, away it uses the Tailscale / https address.")));
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
    m_away = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(info.away), wxDefaultPosition, wxSize(FromDIP(320), -1));
    m_away->SetHint(_L("warrig-q2, 100.x.x.x, name.ts.net or https://"));
    grid->Add(m_away, 1, wxEXPAND);
    grid->Add(lbl(_L("Using now")), 0, wxALIGN_CENTER_VERTICAL);
    auto route = new wxStaticText(this, wxID_ANY, _L("checking..."));
    grid->Add(route, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(lbl(_L("WarRig")), 0, wxALIGN_CENTER_VERTICAL);
    auto about = new wxStaticText(this, wxID_ANY, "...");
    grid->Add(about, 0, wxALIGN_CENTER_VERTICAL);
    m_main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    m_main->Add(lbl(_L("Latest on the printer")), 0, wxLEFT | wxRIGHT | wxTOP, pad);
    m_devices = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(460), FromDIP(120)));
    m_devices->SetBackgroundColour(wxColour(31, 33, 37));
    m_devices->SetForegroundColour(wxColour(232, 232, 234));
    m_main->Add(m_devices, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

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

    struct Snap { std::string route, about; std::vector<std::string> events; };
    in_background<Snap>(m_alive, [info]() {
        Snap r;
        r.route = WarRigLink::pick_route(info);
        if (r.route.empty()) return r;
        std::string inf;
        if (QIDIDeviceApi::warrig_info(r.route, inf)) {
            try {
                json j = json::parse(inf);
                r.about = j.value("version", std::string("V1")) + "  build " + std::to_string(j.value("build", 0)) + "  |  firmware " +
                          j.value("fw", std::string("?")) + "  |  macros " + j.value("macros", std::string("?")) + "  |  boxes " + std::to_string(j.value("boxes", 0));
            } catch (...) {}
        }
        std::string ev = QIDIDeviceApi::warrig_events(r.route);
        try {
            json list = json::parse(ev);
            for (auto it = list.rbegin(); it != list.rend() && r.events.size() < 12; ++it) {
                std::time_t t = (std::time_t)it->value("t", 0LL);
                char when[32] = "";
                if (std::tm* tm = std::localtime(&t)) std::strftime(when, sizeof(when), "%d %b %H:%M", tm);
                r.events.push_back(std::string(when) + "   " + it->value("type", std::string()) + "   " + it->value("msg", std::string()));
            }
        } catch (...) {}
        QIDIDeviceApi::warrig_heartbeat(r.route);
        return r;
    }, [this, route, about, info](Snap r) {
        route->SetLabel(r.route.empty() ? _L("not reachable right now") : (r.route == info.home ? _L("Home network") : _L("Away address")));
        about->SetLabel(r.about.empty() ? _L("not running") : wxString::FromUTF8(r.about));
        for (auto& l : r.events) m_devices->Append(wxString::FromUTF8(l));
        if (r.events.empty()) m_devices->Append(_L("(nothing yet)"));
        Layout(); Fit();
    });

    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        std::string err;
        std::string away = WarRigLink::normalize_away(m_away->GetValue().ToUTF8().data(), err);
        if (!err.empty()) { set_status(wxString::FromUTF8(err), true); return; }
        WarRigLink::LinkInfo i = WarRigLink::get(m_printer_name);
        i.away = away;
        WarRigLink::set(i);
        m_changed = true;
        set_status(_L("Saved."));
    });
    unlink->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxMessageBox(_L("Unlink this printer? It stays on the Device tab for the home network."), _L("WarRig Link"), wxYES_NO | wxNO_DEFAULT, this) != wxYES)
            return;
        WarRigLink::LinkInfo i = WarRigLink::get(m_printer_name);
        WarRigLink::apply_route(m_printer_name, i.home);
        WarRigLink::remove(m_printer_name);
        m_changed = true;
        EndModal(wxID_OK);
    });
}

// ---------------------------------------------------------------------------------------------
// Printer switches
WarRigSwitchesDialog::WarRigSwitchesDialog(wxWindow* parent, const std::string& printer)
    : wxDialog(parent, wxID_ANY, _L("WarRig printer switches"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_alive(std::make_shared<bool>(true))
    , m_base(base_url(printer))
    , m_timer(this)
{
    SetBackgroundColour(wxColour(38, 40, 45));
    SetForegroundColour(wxColour(232, 232, 234));
    const int pad = FromDIP(16);
    auto main = new wxBoxSizer(wxVERTICAL);

    auto title = new wxStaticText(this, wxID_ANY, _L("Printer switches"));
    title->SetFont(title->GetFont().Bold().Scaled(1.3f));
    main->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    auto intro = new wxStaticText(this, wxID_ANY,
        _L("These are the printer's own switches - the same ones as Settings > Print options on its screen and in WarRig Companion. "
           "A change here is saved on the printer straight away, and a change made there shows here. All off = like the stock printer."));
    intro->SetForegroundColour(kMuted);
    intro->Wrap(FromDIP(620));
    main->Add(intro, 0, wxALL, pad);

    auto cols  = new wxBoxSizer(wxHORIZONTAL);
    auto left  = new wxBoxSizer(wxVERTICAL);
    auto right = new wxBoxSizer(wxVERTICAL);
    typedef std::pair<wxString, Item> Row;
    add_group(left, _L("Print start"), {
        Row(_L("Skip bed mesh - uses the saved mesh"),               Item{ "warrig_skip_mesh", 1, 0, 0, false, nullptr }),
        Row(_L("Smart bed mesh - reuses it if nothing changed"),     Item{ "warrig_smart_mesh", 1, 0, 0, false, nullptr }),
        Row(_L("Fast probing - print area only, quicker moves"),     Item{ "warrig_fastprobe", 1, 0, 0, false, nullptr }),
        Row(_L("Skip Z tilt - no gantry levelling"),                 Item{ "warrig_skip_ztilt", 1, 0, 0, false, nullptr }),
        Row(_L("Keep loaded - no reload if already in the head"),    Item{ "warrig_keep_load", 1, 0, 0, false, nullptr }),
        Row(_L("Preheat early - heats when Start print opens"),      Item{ "warrig_preheat", 1, 0, 0, false, nullptr }),
        Row(_L("Speed-ups - faster heating, purge and homing"),      Item{ "warrig_fast", 1, 0, 0, false, nullptr }),
        Row(_L("Chamber pre-heat - for bed 90 C and hotter prints"), Item{ "warrig_chamber_pre", 50, 0, 0, false, nullptr }),
        Row(_L("Skip nozzle wipe - when nothing was loaded"),        Item{ "warrig_skip_wipe", 1, 0, 0, false, nullptr }) });
    add_group(right, _L("Printing and after"), {
        Row(_L("Keep in head - filament stays in the head after a print"), Item{ "warrig_after", 1, 2, 2, false, nullptr }),
        Row(_L("Quiet mode - 70% speed"),                                  Item{ "warrig_quiet", 1, 0, 0, false, nullptr }),
        Row(_L("Runout sensor - pauses the print"),                        Item{ "warrig_runout", 1, 0, 1, false, nullptr }),
        Row(_L("Power-loss resume"),                                       Item{ "plr", 1, 0, 1, true, nullptr }) });
    add_group(right, _L("System"), {
        Row(_L("Small system log - capped at 50 MB"),              Item{ "tw_log", 1, 0, 0, true, nullptr }),
        Row(_L("Wi-Fi always awake - power save off"),             Item{ "tw_wifi", 1, 0, 0, true, nullptr }),
        Row(_L("Klipper first - ahead of the other programs"),     Item{ "tw_klip", 1, 0, 0, true, nullptr }) });
    cols->Add(left, 1, wxEXPAND | wxRIGHT, pad);
    cols->Add(right, 1, wxEXPAND);
    main->Add(cols, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    m_status = new wxStaticText(this, wxID_ANY, _L("Reading the printer..."));
    m_status->SetForegroundColour(kMuted);
    main->Add(m_status, 0, wxALL, pad);

    auto close = new wxButton(this, wxID_OK, _L("Close"));
    main->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, pad);

    for (auto& it : m_items) it.box->Enable(false);     // until the printer has answered
    SetSizerAndFit(main);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { refresh(); }, m_timer.GetId());
    m_timer.Start(3000);
    refresh();
}

WarRigSwitchesDialog::~WarRigSwitchesDialog()
{
    *m_alive = false;
    m_timer.Stop();
}

void WarRigSwitchesDialog::add_group(wxBoxSizer* col, const wxString& title, const std::vector<std::pair<wxString, Item>>& rows)
{
    auto head = new wxStaticText(this, wxID_ANY, title);
    head->SetFont(head->GetFont().Bold());
    head->SetForegroundColour(kOrange);
    col->Add(head, 0, wxTOP | wxBOTTOM, FromDIP(6));
    for (const auto& r : rows) {
        Item it = r.second;
        it.box  = new wxCheckBox(this, wxID_ANY, r.first);
        it.box->SetForegroundColour(wxColour(232, 232, 234));
        const size_t index = m_items.size();
        it.box->Bind(wxEVT_CHECKBOX, [this, index](wxCommandEvent&) { on_toggle(index); });
        col->Add(it.box, 0, wxBOTTOM, FromDIP(6));
        m_items.push_back(it);
    }
}

void WarRigSwitchesDialog::set_status(const wxString& text, bool error)
{
    if (!m_status) return;
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? kRed : kMuted);
    m_status->Wrap(FromDIP(620));
    Layout();
}

void WarRigSwitchesDialog::refresh()
{
    if (m_busy) return;
    m_busy = true;
    const std::string base = m_base;
    typedef std::pair<std::string, std::string> Answer;
    in_background<Answer>(m_alive, [base]() {
        Answer a;
        http_call("GET", base + "/printer/objects/query?save_variables=variables", "", a.first, 6);
        http_call("GET", base + "/server/database/item?namespace=warrig&key=settings", "", a.second, 6);
        return a;
    }, [this](Answer a) {
        m_busy = false;
        show_state(a.first, a.second);
    });
}

void WarRigSwitchesDialog::show_state(const std::string& vars_json, const std::string& settings_json)
{
    json vars, conf;
    bool have_vars = false, have_conf = false;
    try {
        json j = json::parse(vars_json);
        vars = j.at("result").at("status").at("save_variables").at("variables");
        have_vars = vars.is_object();
    } catch (...) {}
    try {
        json j = json::parse(settings_json);
        json v = j.at("result").at("value");
        if (v.is_object() && v.contains("conf") && v["conf"].is_object()) {
            conf = v["conf"];
            m_settings = v.dump();
            have_conf = true;
        }
    } catch (...) {}
    if (!have_vars) {
        for (auto& it : m_items) it.box->Enable(false);
        set_status(_L("The printer does not answer - is it on and on this network?"), true);
        return;
    }
    if ((long long)std::time(nullptr) < m_hold_until) return;     // a change made here is still on its way
    for (auto& it : m_items) {
        const json& src = it.setting ? conf : vars;
        const bool have = it.setting ? have_conf : true;
        int val = it.def_val;
        if (have && src.contains(it.key) && src[it.key].is_number()) val = (int)src[it.key].get<double>();
        it.box->Enable(have);
        const bool on = it.on_val == 50 ? val > 0 : val == it.on_val;
        if (it.box->GetValue() != on) it.box->SetValue(on);
    }
    set_status(have_conf ? _L("In step with the printer.") : _L("In step with the printer. The System switches need the WarRig screen running (BUILD 53 or newer)."));
}

void WarRigSwitchesDialog::on_toggle(size_t index)
{
    if (index >= m_items.size()) return;
    Item& it = m_items[index];
    const bool on = it.box->GetValue();
    if (it.key == "tw_log" && on) {
        if (wxMessageBox(_L("The printer's system log is cut to 50 MB now and kept there (stock keeps 500 MB). Older log entries are deleted and cannot be brought back."),
                         _L("Small system log"), wxYES_NO | wxNO_DEFAULT, this) != wxYES) {
            it.box->SetValue(false);
            return;
        }
    }
    m_hold_until = (long long)std::time(nullptr) + 4;
    const int value = on ? it.on_val : it.off_val;
    if (it.setting) { send_setting(it.key, value); return; }
    std::string script;
    if (it.key == "warrig_quiet") script = "WARRIG_QUIET ON=" + std::to_string(value);
    else script = "SAVE_VARIABLE VARIABLE=" + it.key + " VALUE=" + std::to_string(value);
    // Skip bed mesh and Smart bed mesh can't both be on (the printer screen does the same)
    const char* other = on && it.key == "warrig_skip_mesh" ? "warrig_smart_mesh" : on && it.key == "warrig_smart_mesh" ? "warrig_skip_mesh" : nullptr;
    if (other) {
        script += std::string("\nSAVE_VARIABLE VARIABLE=") + other + " VALUE=0";
        for (auto& o : m_items) if (o.key == other) o.box->SetValue(false);
    }
    send_variable(script);
}

void WarRigSwitchesDialog::send_variable(const std::string& script)
{
    const std::string base = m_base;
    set_status(_L("Saving on the printer..."));
    in_background<bool>(m_alive, [base, script]() {
        std::string out;
        return http_call("POST", base + "/printer/gcode/script", json{ { "script", script } }.dump(), out, 20);
    }, [this](bool ok) {
        if (ok) set_status(_L("Saved on the printer."));
        else { m_hold_until = 0; set_status(_L("The printer did not take the change - it is shown again as the printer has it."), true); }
    });
}

void WarRigSwitchesDialog::send_setting(const std::string& key, int value)
{
    // warrig.settings: {"rev":N, "by":"screen|app|slicer", "conf":{...}} - the newest rev wins, the screen applies it
    json rec;
    try { rec = json::parse(m_settings); } catch (...) {}
    if (!rec.is_object() || !rec.contains("conf") || !rec["conf"].is_object()) {
        m_hold_until = 0;
        set_status(_L("The printer's settings could not be read yet - try again in a moment."), true);
        return;
    }
    int rev = 0;
    if (rec.contains("rev") && rec["rev"].is_number()) rev = (int)rec["rev"].get<double>();
    rec["rev"]       = rev + 1;
    rec["by"]        = "slicer";
    rec["conf"][key] = value;
    m_settings       = rec.dump();
    const std::string base = m_base;
    const std::string body = json{ { "namespace", "warrig" }, { "key", "settings" }, { "value", rec } }.dump();
    set_status(_L("Sending to the printer..."));
    in_background<bool>(m_alive, [base, body]() {
        std::string out;
        return http_call("POST", base + "/server/database/item", body, out, 8);
    }, [this](bool ok) {
        if (ok) set_status(_L("Sent - the printer does it now. While it is printing, a System switch flips back until the print has finished."));
        else { m_hold_until = 0; set_status(_L("The printer did not take the change - it is shown again as the printer has it."), true); }
    });
}

}} // namespace Slic3r::GUI
