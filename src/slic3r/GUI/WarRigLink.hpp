// WarRig Slicer: WarRig Link.
// Links a WarRig printer to this PC without any cloud account. The printer approves each link itself:
// the slicer sends a request, the WarRig screen shows a QR code, and the owner approves it by scanning
// the QR with a phone (or tapping Allow on the screen). Once linked, the slicer reaches the printer on
// the home network first and through its away address (Tailscale or https) when away from home.
//
// Everything is kept in the printer's Moonraker database, namespace "warrig_link":
//   requests.<id> : { app, device, code, created, expires, status: pending|approved|denied }
//   devices.<id>  : { app, device, linked }
//   away          : "q2-warrig.ts.net" or "https://..."   (set on the printer screen, in the app or here)
// so WarRig Companion and the slicer always see the same links.
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
