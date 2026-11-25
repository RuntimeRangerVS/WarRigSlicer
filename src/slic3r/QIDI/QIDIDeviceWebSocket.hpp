// WarRig Slicer: Moonraker websocket connections for printers on the home network.
// Replaces QIDI's unpublished QIDIDeviceWebSocket; based on the websocket code that was public in
// QIDI Studio's QDSDeviceManager.cpp until October 2026.
#ifndef slic3r_QIDI_QIDIDeviceWebSocket_hpp_
#define slic3r_QIDI_QIDIDeviceWebSocket_hpp_

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

class QDSDevice;

class QIDIDeviceWebSocket
{
public:
    QIDIDeviceWebSocket() = default;
    ~QIDIDeviceWebSocket();

    std::function<void(const std::string& device_id, const std::string& status)>        on_status_changed;
    std::function<void(const std::string& device_id, const nlohmann::json& message)>    on_message_received;

    bool connect(const std::string& device_id, std::shared_ptr<QDSDevice> device);
    void stopConnection(const std::string& device_id);
    std::vector<std::string> getConnectionIds();

    void sendSubscribeMessage(const std::string& device_id);
    void getAllErrorList(const std::string& device_id);
    void sendCommand(const std::string& device_id, const std::string& script);
    bool sendCommand(const std::string& device_id, const std::string& scriptName, const std::string& script, const std::string& method);
    bool sendCommand(const std::string& device_id, const nlohmann::json& params, const std::string& method);
    void sendActionCommand(const std::string& device_id, const std::string& action_type);

private:
    using WebSocketClient = websocketpp::client<websocketpp::config::asio_client>;
    struct Connection
    {
        WebSocketClient                      client;
        websocketpp::connection_hdl          hdl;
        std::thread                          thread;
        std::atomic<bool>                    running{false};
        std::atomic<bool>                    stopping{false};
    };

    std::shared_ptr<Connection> get(const std::string& device_id);
    bool send(const std::string& device_id, const nlohmann::json& msg);
    void status(const std::string& device_id, const std::string& s);

    std::mutex                                                   m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Connection>> m_connections;
};

}} // namespace Slic3r::GUI

#endif
