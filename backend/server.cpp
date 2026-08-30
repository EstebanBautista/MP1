// PP Racing server - Diseño 4: hilo asíncrono para vehículos (pool + cola).
//
// Un grupo FIJO de hilos de trabajo (worker pool) ejecuta las tareas de
// movimiento que les llegan desde una COLA compartida. Los vehículos NO
// tienen un hilo permanente: cada tick, el controlador encola una tarea
// "moveCar(index)" por vehículo y los trabajadores las toman de la cola.
//
// Arquitectura:
//   - Hilo controlador (`controllerThread`): marca el ritmo (~30 ticks/s),
//     hace spawn, encola N tareas (una por auto), espera a que toda la tanda
//     termine, elimina los autos fuera de pantalla y vuelca la dificultad.
//   - Pool fijo de K hilos (K = nº de núcleos, acotado): duermen en la cola
//     esperando trabajo y consumen tareas conforme llegan.
//   - Hilo de red (asio): websocketpp + difusión de estados desde la outbox.
//
// Sincronización y carreras:
//   - `taskMutex` + `taskCv` protegen la cola de tareas (productor-consumidor
//     clásico). `pendingTasks` (atómico) + `doneCv` avisan al controlador
//     cuando la tanda terminó.
//   - Cada tarea lleva {índice, multiplicador}; un solo trabajador ejecuta
//     cada tarea y escribe únicamente su carro => sin carrera sobre los autos.
//   - El controlador mantiene `simMutex` durante todo el tick (los manejadores
//     de mensajes del cliente también lo toman), de modo que el vector no se
//     reasigna mientras los trabajadores usan índices estables.
//
// (Equivale al "Diseño 4: Hilo asíncrono para vehículos" del enunciado:
//  grupo fijo de hilos que toman solicitudes desde una cola.)

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
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

namespace {
struct MoveTask {
    std::size_t index;   // position in world.cars
    float mult;          // speed multiplier for this tick
};
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

    // Worker pool plumbing.
    const std::size_t hardwareThreads =
        std::thread::hardware_concurrency() > 0
            ? static_cast<std::size_t>(std::thread::hardware_concurrency())
            : 4u;
    const std::size_t poolSize =
        std::min<std::size_t>(std::max<std::size_t>(hardwareThreads, 2u), 16u);

    std::queue<MoveTask> taskQueue;
    std::mutex taskMutex;
    std::condition_variable taskCv;      // workers wait for new tasks
    std::atomic<std::size_t> pendingTasks{0};
    std::mutex doneMutex;
    std::condition_variable doneCv;      // controller waits for a finished batch
    std::atomic<bool> workersStop{false};

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

    // Fixed worker pool: each thread loops forever waiting for tasks on the
    // shared queue. It executes the task (moving only its assigned car) and
    // notifies the controller when the batch is done.
    std::vector<std::thread> pool;
    pool.reserve(poolSize);
    for (std::size_t w = 0; w < poolSize; ++w) {
        pool.emplace_back([&world, &taskQueue, &taskMutex, &taskCv,
                           &pendingTasks, &doneMutex, &doneCv, &workersStop]() {
            for (;;) {
                MoveTask task;
                {
                    std::unique_lock<std::mutex> lk(taskMutex);
                    taskCv.wait(lk, [&]() {
                        return workersStop.load(std::memory_order_acquire) ||
                               !taskQueue.empty();
                    });
                    if (taskQueue.empty()) {
                        if (workersStop.load(std::memory_order_acquire)) {
                            return;  // shutdown requested and no work left
                        }
                        continue;
                    }
                    task = taskQueue.front();
                    taskQueue.pop();
                }

                world.moveCar(world.cars[task.index], task.mult);

                // Last worker of the batch wakes the controller up.
                if (pendingTasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::lock_guard<std::mutex> lk(doneMutex);
                    doneCv.notify_all();
                }
            }
        });
    }

    // Controller thread: paces the tick, publishes one task per car to the
    // shared queue and waits for all of them before the cull phase. The
    // simulation lock is held for the whole tick, so the cars vector is
    // never resized while the workers use their indices.
    std::thread controllerThread([&world, &simMutex, &outMutex, &outbox,
                                  &taskQueue, &taskMutex, &taskCv,
                                  &pendingTasks, &doneMutex, &doneCv]() {
        auto nextWake = std::chrono::steady_clock::now();
        const float dt = static_cast<float>(World::TICK_MS);

        while (g_running) {
            std::string snapshot;
            {
                std::lock_guard<std::mutex> lock(simMutex);
                if (world.running) {
                    world.beginTick(dt);
                    const float mult = world.slowmoMultiplier();

                    // Publish the batch: one task per enemy car.
                    const std::size_t count = world.cars.size();
                    pendingTasks.store(count, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lk(taskMutex);
                        for (std::size_t i = 0; i < count; ++i) {
                            taskQueue.push({i, mult});
                        }
                        taskCv.notify_all();
                    }

                    // Wait until every task of this tick has been executed.
                    {
                        std::unique_lock<std::mutex> lk(doneMutex);
                        doneCv.wait(lk, [&]() {
                            return pendingTasks.load(std::memory_order_acquire) == 0;
                        });
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

    // Shut the worker pool down cleanly.
    workersStop.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(taskMutex);
        taskCv.notify_all();
    }
    for (std::thread &w : pool) {
        w.join();
    }

    return 0;
}