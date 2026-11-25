// WarRig Slicer: local-network device API (replaces QIDI's unpublished QIDIDeviceApi).
// Talks to the printer's Moonraker HTTP API on the home network. No cloud.
#ifndef slic3r_QIDI_QIDIDeviceApi_hpp_
#define slic3r_QIDI_QIDIDeviceApi_hpp_

#include <functional>
#include <string>
#include <vector>
#include <wx/string.h>
#include "QIDINetworkTypes.hpp"

namespace Slic3r { namespace GUI {

class QIDIDeviceApi
{
public:
    // Raw Moonraker JSON replies; base_url is "http://<printer ip>" (or with a port).
    static bool get_file_list(const std::string& base_url, std::string& body);
    static bool get_timelapse_directory(const std::string& base_url, std::string& body);
    static bool get_system_info(const std::string& base_url, std::string& body);
    static bool get_client_info(const std::string& base_url, std::string& body);

    static bool fetch_url_to_buffer(const std::string& url, std::vector<char>& out, int timeout_s = 30);
    static void download_file_async(const std::string&                       base_url,
                                    const std::string&                       remote_name,
                                    const std::string&                       local_path,
                                    int                                      timeout_s,
                                    const std::string&                       temp_suffix,
                                    std::function<void(bool, std::string)>   on_done);

    // QIDI BOX
    static bool     get_box_state(wxString& msg, const std::string& url, const std::string& api_key);
    static Box_info get_box_info(wxString& msg, const std::string& url, const std::string& api_key);
    static void     get_color_filament_str(wxString& msg, Box_info& info, const std::string& url, const std::string& api_key);

    static std::string normalize_base_url(const std::string& url);
};

}} // namespace Slic3r::GUI

#endif
