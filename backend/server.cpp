// PP Racing server - Diseño 2: hilo único de actualización.
//
// Un solo hilo de ejecución (`simThread`) es el encargado de actualizar TODOS
// los vehículos enemigos: el bucle de juego vive en ese hilo y, en cada tick,
// ejecuta `World::update` (spawn + movimiento + eliminación + dificultad).
//
// La red (websocketpp / asio) corre en el hilo principal: los manejadores de
// mensajes del cliente solo mutan el mundo bajo `simMutex`, y un `set_timer`
// difunde cada estado desde una cola (outbox) para no tocar los sockets desde
// el hilo de simulación.
//
// Datos compartidos y sincronización:
//   - El `World` completo es compartido. Un solo `std::mutex` (simMutex)
//     serializa toda la simulación y todo acceso de los manejadores de red;
//     como hay un único hilo de actualización no hace falta más.
//   - La cola `outbox` (con su propio mutex) entrega los snapshots JSON al
//     hilo de red; nunca se envía desde un hilo que no sea el de asio.
//
// (Equivale al "Diseño 2: Hilo único de Actualización" del enunciado.)

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <chrono>
#include <csignal>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>

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

    // Shared state between the network thread and the simulation thread.
    std::mutex simMutex;                 // guards `world`
    std::mutex outMutex;                 // guards `outbox`
    std::queue<std::string> outbox;      // state snapshots ready to broadcast

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

    // Client messages only touch the world through the simulation lock.
    server.set_message_handler([&world, &simMutex](websocketpp::connection_hdl,
                                                   WsServer::message_ptr msg) {
        ClientMessage cm = parseClientMessage(msg->get_payload());
        std::lock_guard<std::mutex> lock(simMutex);
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

    // THE updater thread: the only thread that runs the simulation. It paces
    // itself to ~30 ticks/s and leaves the snapshots for the network thread.
    std::thread simThread([&world, &simMutex, &outMutex, &outbox]() {
        auto nextWake = std::chrono::steady_clock::now();
        while (g_running) {
            std::string snapshot;
            {
                std::lock_guard<std::mutex> lock(simMutex);
                world.update(World::TICK_MS);
                snapshot = world.stateJson();
            }
            {
                std::lock_guard<std::mutex> lock(outMutex);
                outbox.push(std::move(snapshot));
            }

            nextWake += std::chrono::milliseconds(static_cast<long>(World::TICK_MS));
            std::this_thread::sleep_until(nextWake);
        }
    });

    // Broadcast dispatcher on the asio thread: drains one snapshot per timer
    // callback and sends it to every connection.
    std::function<void()> broadcastLoop;
    broadcastLoop = [&]() {
        std::string snapshot;
        {
            std::lock_guard<std::mutex> lock(outMutex);
            if (!outbox.empty()) {
                snapshot = std::move(outbox.front());
                outbox.pop();
            }
        }

        if (!snapshot.empty()) {
            for (const auto &hdl : connections) {
                try {
                    server.send(hdl, snapshot, websocketpp::frame::opcode::text);
                } catch (const websocketpp::exception &) {
                    // Connection vanished mid-broadcast; the close handler
                    // will drop it from the set.
                }
            }
        }

        if (g_running) {
            server.set_timer(static_cast<long>(World::TICK_MS),
                             [&broadcastLoop](websocketpp::lib::error_code const &ec) {
                                 if (!ec) {
                                     broadcastLoop();
                                 }
                             });
        }
    };
    server.set_timer(1, [&broadcastLoop](websocketpp::lib::error_code const &ec) {
        if (!ec) {
            broadcastLoop();
        }
    });

    server.run();
    simThread.join();
    return 0;
}