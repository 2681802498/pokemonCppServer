#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
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

        // Refresh node heartbeat with TTL
        bool TouchNodeHeartbeat(const std::string &pod_ip, int ttl_seconds = 30);

        // Unregister node from Redis Set
        bool UnregisterNode(const std::string &pod_ip);

        // Delete all room keys that belong to given server (pod_ip)
        bool DeleteRoomsForServer(const std::string &pod_ip);

        // Get all registered nodes
        std::vector<std::string> GetAllNodes();

        // Get all room snapshot entries stored in Redis.
        // The returned vector contains <redis_key, json_value> pairs.
        std::vector<std::pair<std::string, std::string>> GetAllRoomSnapshots();

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
        static const std::string NODE_HEARTBEAT_PREFIX;
    };

} // namespace pokemon_game
