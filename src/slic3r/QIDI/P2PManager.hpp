// WarRig Slicer: QIDI's P2P transfer goes through QIDI's cloud, which WarRig Slicer does not use.
// This keeps the public code compiling and reports that P2P is not available.
#ifndef slic3r_QIDI_P2PManager_hpp_
#define slic3r_QIDI_P2PManager_hpp_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

class P2PManager
{
public:
    struct ConnectParams
    {
        std::string client_id, server, relay_list, device_id, user_token;
    };
    struct FileTransferOptions
    {
        std::atomic<bool>*                           cancel = nullptr;
        std::function<void(int64_t, int64_t)>        progress;
        std::chrono::milliseconds                    timeout{std::chrono::seconds(30)};
        int                                          send_retries           = 0;
        bool                                         reassemble_by_sequence = true;
        bool                                         strip_chunk_header     = true;
        bool                                         filter_by_transfer_id  = false;
        bool                                         trace_chunks           = false;
        bool                                         verify_end_size        = false;
        bool                                         write_partial          = false;
    };
    struct FileTransferResult
    {
        bool              ok        = false;
        bool              cancelled = false;
        std::string       error     = "P2P is not available in WarRig Slicer";
        std::vector<char> data;
    };

    static P2PManager& instance() { static P2PManager m; return m; }

    bool isInited() const { return false; }
    bool isConnected() const { return false; }
    void disconnect() {}
    void shutdown() {}
    void triggerReconnect() {}
    bool ensureConnection(const ConnectParams&, std::function<void(bool)> on_change = nullptr)
    {
        if (on_change) on_change(false);
        return false;
    }
    FileTransferResult uploadFile(const std::string&, const std::string&, const FileTransferOptions&) { return {}; }
};

}} // namespace Slic3r::GUI

#endif
