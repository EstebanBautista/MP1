#ifndef MP1_SRC_WORLD_H
#define MP1_SRC_WORLD_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "car.h"

// The World is the authoritative simulation state:
//   - spawns enemy cars on a timer
//   - advances their Y position every tick
//   - removes cars that leave the bottom of the screen
//   - owns the difficulty progression (speed + spawn rate)
//
// The numeric constants mirror the frontend configuration
// (frontend/scripts/config.js) so that server movement and the client
// rendering agree pixel-to-pixel.
struct World {
    // Timing / geometry
    static constexpr double TICK_MS = 33.0;         // ~30 updates per second
    static constexpr float WIDTH = 640.0f;
    static constexpr float HEIGHT = 840.0f;
    static constexpr float SPAWN_Y = -30.0f;        // spawn just above the road
    static constexpr float REMOVAL_MARGIN = 80.0f;  // fully below the road
    static constexpr int TYPES = 5;                 // enemy1 .. enemy5

    // Difficulty (mirrors GameConfig)
    static constexpr int INITIAL_ENEMY_SPEED = 2;
    static constexpr int MAX_ENEMY_SPEED = 9;
    static constexpr long INITIAL_MS_TO_RELEASE = 500;
    static constexpr long MIN_MS_TO_RELEASE = 250;
    static constexpr long SPAWN_RATE_DECREASE = 50;
    static constexpr std::uint64_t SPEED_INCREASE_INTERVAL = 15;
    static constexpr std::uint64_t SPAWN_RATE_INTERVAL = 30;
    static constexpr double SLOWMO_MULTIPLIER = 0.5;  // matches GameConfig.SLOWMO_MULTIPLIER

    // Lane centers over the road, derived from frontend/scripts/background.js.
    const std::vector<float> laneXs;

    // Simulation state (the shared data of every threading design).
    std::vector<Car> cars;
    std::uint64_t nextId = 1;
    std::uint64_t tick = 0;

    int enemySpeed = INITIAL_ENEMY_SPEED;
    long msToRelease = INITIAL_MS_TO_RELEASE;
    std::uint64_t evaded = 0;

    bool running = false;
    bool slowmoActive = false;

    double elapsedMs = 0.0;
    double nextSpawnMs = 0.0;

    explicit World(double tickMs = TICK_MS);

    // Lifecycle driven by client messages.
    void startGame();   // start / restart: full reset, running = true
    void stopGame();    // game_over: freeze spawning and movement
    void setSlowmo(bool on);
    void removeCar(std::uint64_t id);

    // One simulation step. In the single-threaded designs this is the whole
    // tick; in the parallel movement designs the granular steps below are
    // used instead so the server can dispatch car movement to workers.
    void update(double dtMs);

    // ---- Granular steps for the threaded movement designs ----------------
    // Controller phase (single thread, lock held): advance the clock and
    // spawn a car when the release timer is due.
    void beginTick(double dtMs);

    // Worker phase: advance one car by its current speed times `mult` and
    // mark it inactive once it leaves the screen. This reads no shared state
    // besides the car itself, so distinct cars can be moved concurrently by
    // different threads without locking.
    void moveCar(Car &c, float mult);

    // Controller phase (single thread, lock held): drop inactive cars and
    // fold them into the difficulty counters (evaded / speed / spawn rate).
    void endTick();

    float slowmoMultiplier() const {
        return slowmoActive ? static_cast<float>(SLOWMO_MULTIPLIER) : 1.0f;
    }

    double tickMs() const { return tickRateMs; }
    std::string helloJson() const;
    std::string stateJson() const;

private:
    double tickRateMs;
    std::mt19937 rng;
    std::uniform_int_distribution<int> typeDist;
    std::uniform_int_distribution<std::size_t> laneDist;

    void spawnCar();
    void onCarEvaded();
    void bumpAllSpeeds(int delta);
};

#endif // MP1_SRC_WORLD_H