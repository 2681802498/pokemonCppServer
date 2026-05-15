#include "battle_engine.h"

#include "IO/BattleSession.h"
#include "IO/BattleToJson.h"

#include <mutex>
#include <utility>

namespace pokemon_game
{

    bool BattleEngine::CreateSession(const std::string &session_id,
                                     const nlohmann::json &init_request,
                                     std::string *error)
    {
        if (session_id.empty())
        {
            if (error)
            {
                *error = "session_id cannot be empty";
            }
            return false;
        }

        std::unique_ptr<BattleSession> session;
        std::string local_error;
        auto created = BattleSession::createFromJson(init_request, &local_error);
        if (!created.has_value())
        {
            if (error)
            {
                *error = local_error;
            }
            return false;
        }

        session = std::make_unique<BattleSession>(std::move(created.value()));

        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (sessions_.find(session_id) != sessions_.end())
        {
            if (error)
            {
                *error = "session already exists: " + session_id;
            }
            return false;
        }

        sessions_.emplace(session_id, std::move(session));
        return true;
    }

    bool BattleEngine::ProcessTurn(const std::string &session_id,
                                   const nlohmann::json &turn_request,
                                   nlohmann::json *response,
                                   std::string *error)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second)
        {
            if (error)
            {
                *error = "session not found: " + session_id;
            }
            return false;
        }

        if (response == nullptr)
        {
            if (error)
            {
                *error = "response output cannot be null";
            }
            return false;
        }

        *response = it->second->processTurn(turn_request);
        return response->contains("turn");
    }

    bool BattleEngine::GetState(const std::string &session_id,
                                nlohmann::json *state,
                                std::string *error) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second)
        {
            if (error)
            {
                *error = "session not found: " + session_id;
            }
            return false;
        }

        if (state == nullptr)
        {
            if (error)
            {
                *error = "state output cannot be null";
            }
            return false;
        }

        Battle *battle = it->second->getBattle();
        if (battle == nullptr)
        {
            if (error)
            {
                *error = "battle not initialized for session: " + session_id;
            }
            return false;
        }

        *state = BattleToJson::battleAllInfoToJson(*battle);
        return true;
    }

    bool BattleEngine::DestroySession(const std::string &session_id)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return sessions_.erase(session_id) > 0;
    }

    bool BattleEngine::HasSession(const std::string &session_id) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return sessions_.find(session_id) != sessions_.end();
    }

} // namespace pokemon_game
