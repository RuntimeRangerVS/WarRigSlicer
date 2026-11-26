// WarRig Slicer: WarRig Link (see WarRigLink.hpp).
#include "WarRigLink.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
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
    m_link->SetHint("warrig://connect?host=192.168.0.66&ts=warrig-q2");
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
        const std::string ts = url_param(link, "ts");
        if (!ts.empty() && m_away->GetValue().IsEmpty()) m_away->SetValue(wxString::FromUTF8(ts));
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
    if (!link.empty() && link.rfind("warrig://", 0) != 0) { set_status(_L("That isn't a WarRig link. It starts with warrig://connect"), true); return; }
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

}} // namespace Slic3r::GUI
