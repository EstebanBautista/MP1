#ifndef MP1_SRC_CAR_H
#define MP1_SRC_CAR_H

#include <cstdint>

// Shared representation of an enemy car.
// Fields are written by the movement threads and read by the broadcaster
// that composes the WebSocket `state` message.
struct Car {
    std::uint64_t id;   // unique, monotonically increasing
    int type;           // 1..5 -> frontend texture `enemy{type}`
    float x;            // absolute pixel position (lane center)
    float y;            // absolute pixel position (grows downward)
    float speed;        // pixels moved per server tick
    bool active;        // false when the car exits the bottom of the screen
};

#endif // MP1_SRC_CAR_H