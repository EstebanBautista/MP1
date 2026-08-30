#ifndef MP1_SRC_PROTOCOL_H
#define MP1_SRC_PROTOCOL_H

#include <cstdint>
#include <string>

// Client -> Server messages
enum class ClientAction {
    None,
    Start,      // {"type":"start"}
    Restart,    // {"type":"restart"}
    GameOver,   // {"type":"game_over"}
    RemoveCar,  // {"type":"removeCar","id":N}
    SlowmoOn,   // {"type":"slowmo","active":true}
    SlowmoOff   // {"type":"slowmo","active":false}
};

struct ClientMessage {
    ClientAction action = ClientAction::None;
    std::uint64_t carId = 0;
};

// Parses a client WebSocket payload into an action. Never throws; malformed
// payloads are ignored (action stays None).
ClientMessage parseClientMessage(const std::string &text);

#endif // MP1_SRC_PROTOCOL_H