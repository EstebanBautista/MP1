// PP Racing server - Diseño 3: hilo para cada tipo de vehículo.
//
// El movimiento se reparte entre TYPES hilos: cada hilo de trabajo actualiza
// TODOS los vehículos de un tipo (enemy1..enemy5). Los hilos corren en
// paralelo durante la fase de movimiento de cada tick.
//
// Arquitectura:
//   - Hilo controlador (`controllerThread`): marca el ritmo (~30 ticks/s),
//     hace spawn, lanza a los TYPES trabajadores, espera que terminen
//     (join), elimina los autos fuera de pantalla y vuelca la dificultad.
//   - TYPES hilos de trabajo: cada iteración del tick escanea el vector de
//     autos y mueve los que coinciden con su tipo.
//   - Hilo de red (asio): websocketpp + difusión de estados desde la outbox.
//
// Sincronización y carreras:
//   - El controlador mantiene `simMutex` durante TODO el tick (incluida la
//     fase paralela). Los manejadores de mensajes del cliente también toman
//     ese mutex, por lo que ningún hilo muta el vector durante el trabajo.
//   - Cada auto es escrito por un solo hilo (el de su tipo) y leído por
//     nadie más en esa fase => no hay condición de carrera sobre los autos.
//   - La outbox (outboxMutex) entrega los snapshots JSON al hilo de red; los
//     sockets solo se tocan desde el hilo de asio.
//
// (Equivale al "Diseño 3: Hilo para cada tipo de vehículo" del enunciado:
//  rojos, verdes, negros, etc. cada uno con su propio hilo.)

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
#include <vector>

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

    // Shared state between the network thread and the simulation threads.
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

    // Controller thread: paces the tick and coordinates the type workers.
    // It holds `simMutex` for the whole tick, so the cars vector is not
    // resized while the workers hold indices / iterate it.
    std::thread controllerThread([&world, &simMutex, &outMutex, &outbox]() {
        auto nextWake = std::chrono::steady_clock::now();
        const float dt = static_cast<float>(World::TICK_MS);

        while (g_running) {
            std::string snapshot;
            {
                std::lock_guard<std::mutex> lock(simMutex);
                if (world.running) {
                    world.beginTick(dt);
                    const float mult = world.slowmoMultiplier();

                    // One worker thread per car type. Each worker moves only
                    // the cars of its own type, so no car is touched twice.
                    std::vector<std::thread> typeWorkers;
                    typeWorkers.reserve(static_cast<std::size_t>(World::TYPES));
                    for (int t = 1; t <= World::TYPES; ++t) {
                        typeWorkers.emplace_back([&world, t, mult]() {
                            for (Car &c : world.cars) {
                                if (c.type == t) {
                                    world.moveCar(c, mult);
                                }
                            }
                        });
                    }
                    for (std::thread &w : typeWorkers) {
                        w.join();
                    }

                    world.endTick();
                }
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
    controllerThread.join();
    return 0;
}