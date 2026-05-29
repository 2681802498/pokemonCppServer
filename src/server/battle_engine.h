#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "IO/BattleSession.h"

namespace pokemon_game
{

    class BattleEngine
    {
    public:
        bool CreateSession(const std::string &session_id,
                           const nlohmann::json &init_request,
                           std::string *error);

        bool ProcessTurn(const std::string &session_id,
                         const nlohmann::json &turn_request,
                         nlohmann::json *response,
                         std::string *error);

        bool GetState(const std::string &session_id,
                      nlohmann::json *state,
                      std::string *error) const;

        bool DestroySession(const std::string &session_id);

        bool HasSession(const std::string &session_id) const;

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, std::unique_ptr<BattleSession>> sessions_;
    };

} // namespace pokemon_game
