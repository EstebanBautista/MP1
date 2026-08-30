// PP Racing server - Diseño 1: hilos independientes (un hilo por vehículo).
//
// Cada vehículo enemigo tiene SU propio hilo de ejecución: en cada tick el
// controlador lanza un std::thread por carro, cada uno responsable de
// actualizar únicamente ese vehículo, y espera (join) a que todos terminen
// antes de continuar con la eliminación y la dificultad.
//
// Arquitectura:
//   - Hilo controlador (`controllerThread`): marca el ritmo (~30 ticks/s),
//     hace spawn, crea N hilos (uno por auto), espera que terminen, elimina
//     los autos fuera de pantalla y vuelca la dificultad.
//   - Un hilo de trabajo POR vehículo: nace y muere en cada tick. Con miles
//     de vehículos esto genera miles de hilos por segundo (el objetivo del
//     diseño es evidenciar dicho costo).
//   - Hilo de red (asio): websocketpp + difusión de estados desde la outbox.
//
// Sincronización y carreras:
//   - El controlador mantiene `simMutex` durante TODO el tick (incluida la
//     fase paralela); los manejadores de mensajes del cliente también toman
//     ese mutex, así que el vector no se modifica mientras hay hilos activos.
//   - Cada hilo escribe únicamente su carro (índice fijo): no hay dos hilos
//     escribiendo el mismo auto => sin condición de carrera sobre los autos.
//   - La outbox (outboxMutex) entrega los snapshots JSON al hilo de red; los
//     sockets solo se tocan desde el hilo de asio.
//
// (Equivale al "Diseño 1: Hilos Independientes" del enunciado:
//  Carro 1 -> Thread 1, Carro 2 -> Thread 2, ... )

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/car.h"
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

    // Controller thread: paces the tick and creates one thread per car.
    // The simulation lock is held for the whole tick, so the cars vector is
    // never resized while the per-car threads are running.
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

                    // One independent thread per enemy car. Each thread owns
                    // exactly one `Car` (index captured by value). Threads are
                    // joined before the cull phase: born and destroyed every
                    // tick, O(cars) threads per second at 30 Hz.
                    std::vector<std::thread> carThreads;
                    carThreads.reserve(world.cars.size());
                    for (std::size_t i = 0; i < world.cars.size(); ++i) {
                        carThreads.emplace_back([&world, i, mult]() {
                            world.moveCar(world.cars[i], mult);
                        });
                    }
                    for (std::thread &t : carThreads) {
                        t.join();
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