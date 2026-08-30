// PP Racing server - Diseño 1: hilos independientes (un hilo por vehículo).

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

    std::mutex simMutex;                 
    std::mutex outMutex;                
    std::queue<std::string> outbox;      

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