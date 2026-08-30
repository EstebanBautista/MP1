// PP Racing server - Fase 0 (design 2 base): single-threaded baseline.
//
// The whole simulation and the WebSocket event loop run on the same thread.
// websocketpp drives everything from the asio io_service; the game tick is
// scheduled with set_timer() and therefore never races with the connection
// handlers. Later threading designs restructure only the way `World::update`
// is executed.
//
// Protocol (JSON over WebSocket):
//   server -> client  {"type":"hello", "tickMs", "width", "height", "types"}
//                     {"type":"state","tick","enemySpeed","msToRelease",
//                      "evaded","slowmo","cars":[{id,type,x,y}, ...]}
//   client -> server  {"type":"start"} | {"type":"restart"} | {"type":"game_over"}
//                     {"type":"removeCar","id":N}
//                     {"type":"slowmo","active":true|false}

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <csignal>
#include <functional>
#include <set>

#include "src/protocol.h"
#include "src/world.h"

typedef websocketpp::server<websocketpp::config::asio> WsServer;

static volatile std::sig_atomic_t g_running = 1;

void handleSignal(int) {
    g_running = 0;
}

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    World world(World::TICK_MS);
    WsServer server;

    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);

    server.init_asio();
    server.set_reuse_addr(true);

    // Active WebSocket connections (typically a single game client).
    std::set<websocketpp::connection_hdl,
             std::owner_less<websocketpp::connection_hdl>> connections;

    server.set_open_handler([&server, &connections, &world](
                                websocketpp::connection_hdl hdl) {
        connections.insert(hdl);
        server.send(hdl, world.helloJson(), websocketpp::frame::opcode::text);
    });

    server.set_close_handler([&connections](websocketpp::connection_hdl hdl) {
        connections.erase(hdl);
    });

    server.set_message_handler([&world](websocketpp::connection_hdl,
                                        WsServer::message_ptr msg) {
        ClientMessage cm = parseClientMessage(msg->get_payload());
        switch (cm.action) {
            case ClientAction::Start:
            case ClientAction::Restart:
                world.startGame();
                break;
            case ClientAction::GameOver:
                world.stopGame();
                break;
            case ClientAction::RemoveCar:
                world.removeCar(cm.carId);
                break;
            case ClientAction::SlowmoOn:
                world.setSlowmo(true);
                break;
            case ClientAction::SlowmoOff:
                world.setSlowmo(false);
                break;
            case ClientAction::None:
            default:
                break;
        }
    });

    server.listen(5000);
    server.start_accept();

    // Game loop: step the world and broadcast the resulting state. Runs on the
    // asio event loop thread, so no locking is needed in this design.
    std::function<void()> tickLoop;
    tickLoop = [&]() {
        world.update(World::TICK_MS);
        std::string state = world.stateJson();
        for (const auto &hdl : connections) {
            try {
                server.send(hdl, state, websocketpp::frame::opcode::text);
            } catch (const websocketpp::exception &) {
                // Connection vanished mid-broadcast; the close handler
                // will drop it from the set.
            }
        }
        if (g_running) {
            server.set_timer(static_cast<long>(World::TICK_MS),
                             [&tickLoop](websocketpp::lib::error_code const &ec) {
                                 if (!ec) {
                                     tickLoop();
                                 }
                             });
        }
    };

    server.set_timer(1, [&tickLoop](websocketpp::lib::error_code const &ec) {
        if (!ec) {
            tickLoop();
        }
    });

    server.run();
    return 0;
}