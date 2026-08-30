#include "world.h"

#include <nlohmann/json.hpp>

// Out-of-class definitions for the constexpr constants (required in C++11 for
// floating point members that get ODR-used).
constexpr double World::TICK_MS;
constexpr float World::WIDTH;
constexpr float World::HEIGHT;
constexpr float World::SPAWN_Y;
constexpr float World::REMOVAL_MARGIN;
constexpr int   World::TYPES;
constexpr int   World::INITIAL_ENEMY_SPEED;
constexpr int   World::MAX_ENEMY_SPEED;
constexpr long  World::INITIAL_MS_TO_RELEASE;
constexpr long  World::MIN_MS_TO_RELEASE;
constexpr long  World::SPAWN_RATE_DECREASE;
constexpr std::uint64_t World::SPEED_INCREASE_INTERVAL;
constexpr std::uint64_t World::SPAWN_RATE_INTERVAL;
constexpr double World::SLOWMO_MULTIPLIER;

World::World(double tickMs)
    : laneXs({160.0f, 224.0f, 288.0f, 352.0f, 416.0f, 480.0f})
    , tickRateMs(tickMs > 0.0 ? tickMs : TICK_MS)
    , rng(std::random_device{}())
    , typeDist(1, TYPES) {}

void World::startGame() {
    cars.clear();
    nextId = 1;
    tick = 0;
    enemySpeed = INITIAL_ENEMY_SPEED;
    msToRelease = INITIAL_MS_TO_RELEASE;
    evaded = 0;
    slowmoActive = false;
    running = true;
    elapsedMs = 0.0;
    nextSpawnMs = static_cast<double>(msToRelease);
}

void World::stopGame() {
    running = false;
}

void World::setSlowmo(bool on) {
    slowmoActive = on;
}

void World::removeCar(std::uint64_t id) {
    for (std::size_t i = 0; i < cars.size(); ++i) {
        if (cars[i].id == id) {
            cars.erase(cars.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

void World::spawnCar() {
    // Only reuse a lane once its most recent occupant has cleared the spawn
    // zone, so two cars never share the same spot. If every lane is still
    // blocked, skip the spawn; the release timer retries on a later tick.
    std::vector<std::size_t> freeLanes;
    for (std::size_t li = 0; li < laneXs.size(); ++li) {
        bool blocked = false;
        for (const Car &c : cars) {
            if (c.x == laneXs[li] && c.y < SPAWN_Y + SPAWN_SAFE_GAP) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            freeLanes.push_back(li);
        }
    }
    if (freeLanes.empty()) {
        return;
    }

    Car c;
    c.id = nextId++;
    c.type = typeDist(rng);
    std::uniform_int_distribution<std::size_t> pick(0, freeLanes.size() - 1);
    c.x = laneXs[freeLanes[pick(rng)]];
    c.y = SPAWN_Y;
    c.speed = static_cast<float>(enemySpeed);
    c.active = true;
    cars.push_back(c);
}

void World::onCarEvaded() {
    ++evaded;
    if (evaded % SPEED_INCREASE_INTERVAL == 0 && enemySpeed < MAX_ENEMY_SPEED) {
        ++enemySpeed;
        bumpAllSpeeds(1);
    }
    if (evaded % SPAWN_RATE_INTERVAL == 0 && msToRelease > MIN_MS_TO_RELEASE) {
        msToRelease -= SPAWN_RATE_DECREASE;
    }
}

void World::bumpAllSpeeds(int delta) {
    for (Car &c : cars) {
        c.speed += static_cast<float>(delta);
    }
}

void World::update(double dtMs) {
    if (!running) {
        return;
    }

    ++tick;
    elapsedMs += dtMs;

    // Spawn a new car when the release timer expires.
    if (elapsedMs >= nextSpawnMs) {
        spawnCar();
        nextSpawnMs = elapsedMs + static_cast<double>(msToRelease);
    }

    // Move every car and mark the ones that left the screen.
    const float mult = slowmoActive ? static_cast<float>(SLOWMO_MULTIPLIER) : 1.0f;
    std::uint64_t removedCount = 0;
    for (Car &c : cars) {
        c.y += c.speed * mult;
        if (c.y > HEIGHT + REMOVAL_MARGIN) {
            c.active = false;
            ++removedCount;
        }
    }

    // Cull inactive cars in a single pass.
    if (removedCount > 0) {
        std::vector<Car> keep;
        keep.reserve(cars.size() - static_cast<std::size_t>(removedCount));
        for (const Car &c : cars) {
            if (c.active) {
                keep.push_back(c);
            }
        }
        cars.swap(keep);

        for (std::uint64_t i = 0; i < removedCount; ++i) {
            onCarEvaded();
        }
    }
}

std::string World::helloJson() const {
    nlohmann::json j;
    j["type"] = "hello";
    j["tickMs"] = tickRateMs;
    j["width"] = WIDTH;
    j["height"] = HEIGHT;
    j["types"] = TYPES;
    return j.dump();
}

std::string World::stateJson() const {
    nlohmann::json j;
    j["type"] = "state";
    j["tick"] = tick;
    j["enemySpeed"] = enemySpeed;
    j["msToRelease"] = msToRelease;
    j["evaded"] = evaded;
    j["slowmo"] = slowmoActive;
    j["cars"] = nlohmann::json::array();
    for (const Car &c : cars) {
        nlohmann::json item;
        item["id"] = c.id;
        item["type"] = c.type;
        item["x"] = c.x;
        item["y"] = c.y;
        j["cars"].push_back(item);
    }
    return j.dump();
}