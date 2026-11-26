// WarRig Slicer: Moonraker websocket connections for printers on the home network.
#include "QIDIDeviceWebSocket.hpp"
#include "slic3r/GUI/QDSDeviceManager.hpp"

#include <boost/log/trivial.hpp>
#include <cstdlib>

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

QIDIDeviceWebSocket::~QIDIDeviceWebSocket()
{
    for (const auto& id : getConnectionIds())
        stopConnection(id);
}

std::shared_ptr<QIDIDeviceWebSocket::Connection> QIDIDeviceWebSocket::get(const std::string& device_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(device_id);
    if (it == m_connections.end() || it->second->stopping) return nullptr;
    return it->second;
}

std::vector<std::string> QIDIDeviceWebSocket::getConnectionIds()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> ids;
    for (const auto& kv : m_connections) ids.push_back(kv.first);
    return ids;
}

void QIDIDeviceWebSocket::status(const std::string& device_id, const std::string& s)
{
    if (on_status_changed) on_status_changed(device_id, s);
}

bool QIDIDeviceWebSocket::connect(const std::string& device_id, std::shared_ptr<QDSDevice> device)
{
    if (!device) return false;
    stopConnection(device_id);

    auto conn = std::make_shared<Connection>();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections[device_id] = conn;
    }
    std::weak_ptr<Connection> weak = conn;
    try {
        conn->client.init_asio();
        conn->client.clear_access_channels(websocketpp::log::alevel::all);
        conn->client.clear_error_channels(websocketpp::log::elevel::all);

        conn->client.set_open_handler([this, device_id](websocketpp::connection_hdl) {
            status(device_id, "connected");
            std::thread([this, device_id]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                sendSubscribeMessage(device_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                getAllErrorList(device_id);
            }).detach();
        });
        conn->client.set_message_handler([this, device_id, weak](websocketpp::connection_hdl, WebSocketClient::message_ptr msg) {
            auto c = weak.lock();
            if (!c || c->stopping) return;
            try {
                json j = json::parse(msg->get_payload());
                if (on_message_received) on_message_received(device_id, j);
            } catch (...) {}
        });
        conn->client.set_close_handler([this, device_id](websocketpp::connection_hdl) { status(device_id, "offline"); });
        conn->client.set_fail_handler([this, device_id](websocketpp::connection_hdl) { status(device_id, "offline"); });

        websocketpp::lib::error_code ec;
        auto con = conn->client.get_connection(device->m_url, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << "WarRig: websocket connect failed for " << device->m_url << ": " << ec.message();
            status(device_id, "offline");
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connections.erase(device_id);
            return false;
        }
        conn->hdl     = con->get_handle();
        conn->running = true;
        conn->client.connect(con);
        conn->thread = std::thread([conn]() {
            try { conn->client.run(); } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "WarRig: websocket thread: " << e.what();
            }
            conn->running = false;
        });
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "WarRig: websocket connect exception: " << e.what();
        status(device_id, "error");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.erase(device_id);
        return false;
    }
}

void QIDIDeviceWebSocket::stopConnection(const std::string& device_id)
{
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(device_id);
        if (it == m_connections.end()) return;
        conn = it->second;
        m_connections.erase(it);
    }
    conn->stopping = true;
    try {
        websocketpp::lib::error_code ec;
        auto con = conn->client.get_con_from_hdl(conn->hdl, ec);
        if (!ec && con && con->get_state() == websocketpp::session::state::open)
            conn->client.close(conn->hdl, websocketpp::close::status::going_away, "Connection stopped", ec);
        conn->client.stop();
    } catch (...) {}
    if (conn->thread.joinable()) {
        if (conn->thread.get_id() == std::this_thread::get_id())
            conn->thread.detach();
        else
            conn->thread.join();
    }
    conn->running = false;
}

bool QIDIDeviceWebSocket::send(const std::string& device_id, const json& msg)
{
    auto conn = get(device_id);
    if (!conn) return false;
    try {
        websocketpp::lib::error_code ec;
        conn->client.send(conn->hdl, msg.dump(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(trace) << "WarRig: websocket send to " << device_id << " failed: " << ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "WarRig: websocket send exception: " << e.what();
        return false;
    }
}

void QIDIDeviceWebSocket::sendSubscribeMessage(const std::string& device_id)
{
    json objects = json::object();
    for (const char* o : {"gcode_move", "exclude_object", "filament_switch_sensor filament_switch_sensor",
                          "fan_generic chamber_circulation_fan", "fan_generic cooling_fan", "fan_generic auxiliary_cooling_fan",
                          "output_pin polar_cooler", "print_stats_manager", "print_stats", "display_status", "toolhead",
                          "heater_bed", "extruder", "heater_generic chamber", "output_pin caselight", "save_variables",
                          "aht20_f heater_box1", "aht20_f heater_box2", "aht20_f heater_box3", "aht20_f heater_box4", "data",
                          "multi_color_controller"})
        objects[o] = nullptr;
    for (int i = 0; i <= 16; ++i)
        objects["box_stepper slot" + std::to_string(i)] = nullptr;
    send(device_id, {{"id", std::atoi(device_id.c_str())}, {"method", "printer.objects.subscribe"}, {"jsonrpc", "2.0"},
                     {"params", {{"objects", objects}}}});
}

void QIDIDeviceWebSocket::getAllErrorList(const std::string& device_id)
{
    sendCommand(device_id, "method", "get_all_error_list", "server.extensions.request");
}

void QIDIDeviceWebSocket::sendCommand(const std::string& device_id, const std::string& script)
{
    send(device_id, {{"id", std::atoi(device_id.c_str())}, {"method", "printer.gcode.script"}, {"jsonrpc", "2.0"},
                     {"params", {{"script", script}}}});
}

bool QIDIDeviceWebSocket::sendCommand(const std::string& device_id, const std::string& scriptName, const std::string& script,
                                      const std::string& method)
{
    return send(device_id, {{"id", std::atoi(device_id.c_str())}, {"method", method}, {"jsonrpc", "2.0"},
                            {"params", {{scriptName, script}}}});
}

bool QIDIDeviceWebSocket::sendCommand(const std::string& device_id, const json& params, const std::string& method)
{
    json msg = {{"id", std::atoi(device_id.c_str())}, {"method", method}, {"jsonrpc", "2.0"}};
    if (!params.is_null()) msg["params"] = params;
    return send(device_id, msg);
}

void QIDIDeviceWebSocket::sendActionCommand(const std::string& device_id, const std::string& action_type)
{
    send(device_id, {{"id", std::atoi(device_id.c_str())}, {"method", "printer.print." + action_type}, {"jsonrpc", "2.0"}});
}

}} // namespace Slic3r::GUI
