#pragma once

#include <string>
#include <memory>
#include <vector>
#include <hiredis/hiredis.h>

namespace pokemon_game
{

    class RedisClient
    {
    public:
        RedisClient();
        ~RedisClient();

        // Connect to Redis server
        bool Connect(const std::string &host, int port, int timeout_ms = 5000);

        // Register node in Redis Set
        bool RegisterNode(const std::string &pod_ip);

        // Unregister node from Redis Set
        bool UnregisterNode(const std::string &pod_ip);

        // Get all registered nodes
        std::vector<std::string> GetAllNodes();

        // Store room data
        bool SetRoomData(const std::string &room_id, const std::string &data);

        // Delete room data and related room command data
        bool DeleteRoomData(const std::string &room_id);

        // Is connected
        bool IsConnected() const { return context_ != nullptr; }

        // Disconnect
        void Disconnect();

    private:
        redisContext *context_;
        static const std::string NODES_SET_KEY;
    };

} // namespace pokemon_game
