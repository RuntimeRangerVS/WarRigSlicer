// WarRig Slicer: WarRig Link.
// Uses the WarRig screen's link contract (WARRIG_LINK.txt, api 2: Moonraker database namespace "warrig").
// A printer is linked from the QR code on the printer screen (Wi-Fi > Remote access & phone alerts):
//   warrig://connect?host=<printer LAN IP>&ts=<tailscale name>&topic=...
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

}} // namespace Slic3r::GUI

#endif
