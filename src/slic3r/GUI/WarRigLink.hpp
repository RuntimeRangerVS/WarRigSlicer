// WarRig Slicer: WarRig Link.
// Uses the WarRig screen's link contract (WARRIG_LINK.txt, api 2: Moonraker database namespace "warrig").
// A printer is linked from the QR code on the printer screen (Wi-Fi > Remote access & phone alerts):
//   warrig://link?v=1&host=<printer LAN IP>[&remote=<Tailscale IP>]&topic=...   (WarRig Link, WARRIG_LINK.txt 7b)
// Older codes warrig://connect?host=<printer LAN IP>&ts=<tailscale name>&topic=... are still read.
// The slicer then reaches the printer on the home network first and through its Tailscale name
// (or an https address) when away from home. It reads warrig.info / warrig.box / warrig.events,
// writes its heartbeat (clients.slicer) and sends warrig.next_print with each print.
#ifndef slic3r_GUI_WarRigLink_hpp_
#define slic3r_GUI_WarRigLink_hpp_

#include <string>
#include <vector>
#include <memory>
#include <wx/dialog.h>
#include <wx/timer.h>

class wxChoice;
class wxTextCtrl;
class wxStaticText;
class wxListBox;
class wxButton;
class wxBoxSizer;
class wxCheckBox;

namespace Slic3r { namespace GUI {

namespace WarRigLink {

struct LinkInfo
{
    std::string name;  // physical printer name in the Device tab
    std::string home;  // home-network address, e.g. 192.168.0.66
    std::string away;  // away address, e.g. q2-warrig.ts.net or https://printer.example.com
    std::string id;    // this PC's link id on the printer
};

bool                  is_linked(const std::string& printer_name);
LinkInfo              get(const std::string& printer_name);
void                  set(const LinkInfo& info);
void                  remove(const std::string& printer_name);
std::vector<LinkInfo> all();

// Accepts Tailscale (100.64.0.0/10 or *.ts.net, http allowed) or any https address.
// Returns the normalised base URL, or empty with a reason in `error`.
std::string normalize_away(const std::string& input, std::string& error);

// Which address reaches the printer now: home if it answers, otherwise away. Empty if neither.
// Blocking (a few seconds at most); call from a worker thread.
std::string pick_route(const LinkInfo& info);

// Points the printer's Device-tab entry at `host`. Returns true if it changed.
bool apply_route(const std::string& printer_name, const std::string& host);

} // namespace WarRigLink

class WarRigLinkDialog : public wxDialog
{
public:
    // printer_name: an existing Device-tab printer. If it is already linked the dialog manages the link.
    WarRigLinkDialog(wxWindow* parent, const std::string& printer_name);
    ~WarRigLinkDialog() override;

    bool changed() const { return m_changed; }

private:
    void build_new_link();
    void build_manage();
    void on_printer_choice();
    void on_send_request();
    void on_tick(wxTimerEvent&);
    void tick();
    void poll_request();
    void finish_link();
    void set_status(const wxString& text, bool error = false);
    std::string selected_host() const;

    std::shared_ptr<bool> m_alive;
    bool                  m_changed { false };
    bool                  m_manage { false };

    std::string m_printer_name;
    std::string m_host;
    std::string m_request_id;
    std::string m_code;
    long        m_expires { 0 };
    bool        m_polling { false };

    wxChoice*     m_choice { nullptr };
    wxTextCtrl*   m_away { nullptr };
    wxTextCtrl*   m_link { nullptr };
    wxStaticText* m_status { nullptr };
    wxStaticText* m_code_text { nullptr };
    wxListBox*    m_devices { nullptr };
    wxButton*     m_send { nullptr };
    wxBoxSizer*   m_main { nullptr };
    wxTimer       m_timer;
    std::vector<std::string> m_names;
};

// The printer's standing switches (Settings > Print options on the WarRig screen), shown and changed from the
// slicer. The printer keeps them: the print options are Klipper saved variables, the System switches and
// Power-loss resume are WarRig settings (warrig.settings). The window reads both every few seconds, so a switch
// changed on the printer screen or in WarRig Companion shows here too.
class WarRigSwitchesDialog : public wxDialog
{
public:
    // printer: the selected printer (IP, host name or URL)
    WarRigSwitchesDialog(wxWindow* parent, const std::string& printer);
    ~WarRigSwitchesDialog() override;

private:
    struct Item
    {
        std::string key;        // saved variable, or WarRig setting when `setting`
        int         on_val;
        int         off_val;
        int         def_val;    // when the printer has no value yet
        bool        setting;
        wxCheckBox* box;
    };
    void add_group(wxBoxSizer* col, const wxString& title, const std::vector<std::pair<wxString, Item>>& rows);
    void refresh();
    void show_state(const std::string& vars_json, const std::string& settings_json);
    void on_toggle(size_t index);
    void send_variable(const std::string& script);
    void send_setting(const std::string& key, int value);
    void set_status(const wxString& text, bool error = false);

    std::shared_ptr<bool> m_alive;
    std::string           m_base;
    std::string           m_settings;      // the last warrig.settings record read (its "value")
    std::vector<Item>     m_items;
    wxStaticText*         m_status { nullptr };
    wxTimer               m_timer;
    bool                  m_busy { false };
    long long             m_hold_until { 0 };   // just changed here: don't show an older answer over it
};

}} // namespace Slic3r::GUI

#endif
