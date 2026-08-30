#include "protocol.h"

#include <nlohmann/json.hpp>

ClientMessage parseClientMessage(const std::string &text) {
    ClientMessage msg;
    if (text.empty()) {
        return msg;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(text);
        const std::string type = j.value("type", "");

        if (type == "start") {
            msg.action = ClientAction::Start;
        } else if (type == "restart") {
            msg.action = ClientAction::Restart;
        } else if (type == "game_over") {
            msg.action = ClientAction::GameOver;
        } else if (type == "removeCar") {
            msg.action = ClientAction::RemoveCar;
            msg.carId = j.value("id", static_cast<std::uint64_t>(0));
        } else if (type == "slowmo") {
            msg.action = j.value("active", false) ? ClientAction::SlowmoOn
                                                  : ClientAction::SlowmoOff;
        }
    } catch (const std::exception &) {
        // Malformed payload: ignore.
    }

    return msg;
}