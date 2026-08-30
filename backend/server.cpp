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
//   - Hilo principal (main): bucle de aceptación HTTP ligero sobre ASIO
//     standalone (en vez de websocketpp en los otros diseños).
//
// Sincronización y carreras:
//   - `taskMutex` + `taskCv` protegen la cola de tareas (productor-consumidor
//     clásico). `pendingTasks` (atómico) + `doneCv` avisan al controlador
//     cuando la tanda terminó.
//   - Cada tarea lleva {índice, multiplicador}; un solo trabajador ejecuta
//     cada tarea y escribe únicamente su carro => sin carrera sobre los autos.
//   - El controlador mantiene `simMutex` durante todo el tick (los manejadores
//     HTTP de acciones también lo toman), de modo que el vector no se
//     reasigna mientras los trabajadores usan índices estables.
//
// Transporte (variante sin websockets):
//   - El cliente no mantiene una conexión abierta: consulta por polling HTTP.
//     GET /state cada ~33 ms (un tick del juego) y POST /action para los
//     eventos de juego. Esta rama cubre la opción "(evalúe una implementación
//     sin websockets)" del enunciado.
//   - Se descartó SSE (Server-Sent Events) porque conserva una conexión
//     persistente bidireccional a medio camino y agrega lógica de stream;
//     el polling deja al servidor sin estado de conexión: cada request es
//     autocontenida y se responde con `Connection: close`.
//   - GET /state devuelve world.stateJson() on-demand bajo simMutex: como el
//     controlador mantiene ese mutex durante todo el tick, el cliente ve
//     siempre un estado completo de antes o de después de un tick, nunca a
//     medias (misma garantía atómica que difundía el websocket). Cuando el
//     juego está detenido, el estado queda congelado (mismo tick y mismos
//     autos), semántica sobre la que ya se apoyan las correcciones de
//     reconciliación del cliente (restartPending / lastSeenTick).
//   - POST /action reutiliza src/protocol.cpp (parseClientMessage) con los
//     mismos JSON que inscribían los mensajes WS originales, por lo que la
//     lógica del frontend (envíos "start/restart/game_over/removeCar/slowmo")
//     no cambió.
//
// (Equivale al "Diseño 4: Hilo asíncrono para vehículos" del enunciado:
//  grupo fijo de hilos que toman solicitudes desde una cola.)

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/car.h"
#include "src/protocol.h"
#include "src/world.h"

static volatile std::sig_atomic_t g_running = 1;

void handleSignal(int) {
    g_running = 0;
}

namespace {
struct MoveTask {
    std::size_t index;   // position in world.cars
    float mult;          // speed multiplier for this tick
};

// HTTP/1.1 connection helper on the main thread. Every polling request opens
// a short-lived TCP connection: read the request, answer, close. Handles the
// two game endpoints plus the CORS preflight; anything else returns 404.
void handleRequest(asio::ip::tcp::socket &sock, World &world,
                   std::mutex &simMutex) {
    asio::error_code ec;
    asio::streambuf buf;

    const std::size_t headersEnd = asio::read_until(sock, buf, "\r\n\r\n", ec);
    if (ec) {
        return;
    }

    std::string head(
        static_cast<const char *>(asio::buffer_cast<const void *>(buf.data())),
        headersEnd);
    buf.consume(headersEnd);

    std::istringstream headStream(head);
    std::string method, target, version;
    headStream >> method >> target >> version;
    if (method.empty() || target.empty()) {
        return;
    }

    std::size_t contentLength = 0;
    std::string line;
    while (std::getline(headStream, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        if (name == "Content-Length" || name == "content-length") {
            contentLength =
                static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
        }
    }

    std::string body;
    if (method == "POST" && contentLength > 0) {
        // The body may already be buffered after the headers; finish reading
        // only the missing bytes, then keep the first contentLength of them.
        while (buf.size() < contentLength && !ec) {
            asio::read(sock, buf, asio::transfer_at_least(1), ec);
        }
        const std::size_t take = std::min(buf.size(), contentLength);
        body.assign(
            static_cast<const char *>(asio::buffer_cast<const void *>(buf.data())),
            take);
        buf.consume(take);
    }

    const std::string cors = "Access-Control-Allow-Origin: *";
    asio::streambuf out;
    std::ostream os(&out);

    if (method == "OPTIONS") {
        // Preflight of the cross-origin POST /action from the game page.
        os << "HTTP/1.1 200 OK\r\n"
           << cors << "\r\n"
           << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "Access-Control-Max-Age: 86400\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
    } else if (method == "GET" && target == "/state") {
        std::string state;
        {
            std::lock_guard<std::mutex> lock(simMutex);
            state = world.stateJson();
        }
        os << "HTTP/1.1 200 OK\r\n"
           << cors << "\r\n"
           << "Content-Type: application/json\r\n"
           << "Content-Length: " << state.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << state;
    } else if (method == "POST" && target == "/action") {
        ClientMessage cm = parseClientMessage(body);
        {
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
        }
        os << "HTTP/1.1 200 OK\r\n"
           << cors << "\r\n"
           << "Content-Type: application/json\r\n"
           << "Content-Length: 2\r\n"
           << "Connection: close\r\n\r\n"
           << "{}";
    } else {
        os << "HTTP/1.1 404 Not Found\r\n"
           << cors << "\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
    }

    asio::write(sock, out, ec);
}
}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    World world(World::TICK_MS);

    // Shared state between the network thread and the simulation threads.
    std::mutex simMutex;  // guards `world` (controller + action handlers)

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
    std::thread controllerThread([&world, &simMutex, &taskQueue, &taskMutex,
                                  &taskCv, &pendingTasks, &doneMutex,
                                  &doneCv]() {
        auto nextWake = std::chrono::steady_clock::now();
        const float dt = static_cast<float>(World::TICK_MS);

        while (g_running) {
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
            }

            nextWake += std::chrono::milliseconds(static_cast<long>(World::TICK_MS));
            std::this_thread::sleep_until(nextWake);
        }
    });

    // Main thread: HTTP/1.1 accept loop. Every request is handled synchronously
    // and answered with `Connection: close`, keeping the server stateless (no
    // outbox, no broadcast): GET /state returns the freshest simulation
    // snapshot and POST /action funnels the game events into parseClientMessage.
    asio::io_context io;
    asio::ip::tcp::acceptor acceptor(
        io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 5000));
    acceptor.set_option(asio::socket_base::reuse_address(true));

    while (g_running) {
        asio::ip::tcp::socket sock(io);
        asio::error_code ec;
        acceptor.accept(sock, ec);
        if (ec) {
            continue;
        }
        handleRequest(sock, world, simMutex);
    }

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