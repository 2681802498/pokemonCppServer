#include "redis_client.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace pokemon_game
{

    const std::string RedisClient::NODES_SET_KEY = "pokemon:server:nodes";
    const std::string RedisClient::NODE_HEARTBEAT_PREFIX = "pokemon:server:heartbeat:";

    RedisClient::RedisClient() : context_(nullptr) {}

    RedisClient::~RedisClient()
    {
        Disconnect();
    }

    bool RedisClient::Connect(const std::string &host, int port, int timeout_ms)
    {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        context_ = redisConnectWithTimeout(host.c_str(), port, tv);

        if (context_ == nullptr || context_->err)
        {
            if (context_)
            {
                std::cerr << "Redis connection error: " << context_->errstr << std::endl;
                redisFree(context_);
                context_ = nullptr;
            }
            return false;
        }

        std::cout << "Connected to Redis at " << host << ":" << port << std::endl;
        return true;
    }

    void RedisClient::Disconnect()
    {
        if (context_ != nullptr)
        {
            redisFree(context_);
            context_ = nullptr;
        }
    }

    bool RedisClient::RegisterNode(const std::string &pod_ip)
    {
        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return false;
        }

        redisReply *reply = (redisReply *)redisCommand(context_,
                                                       "SADD %s %s", NODES_SET_KEY.c_str(), pod_ip.c_str());

        if (reply == nullptr)
        {
            std::cerr << "Redis command failed" << std::endl;
            return false;
        }

        bool success = reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0;
        freeReplyObject(reply);

        if (success)
        {
            TouchNodeHeartbeat(pod_ip);
            std::cout << "Node " << pod_ip << " registered in Redis" << std::endl;
        }
        return success;
    }

    bool RedisClient::TouchNodeHeartbeat(const std::string &pod_ip, int ttl_seconds)
    {
        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return false;
        }

        std::string key = NODE_HEARTBEAT_PREFIX + pod_ip;
        // Ensure minimum TTL of 5 minutes to prevent heartbeat expiration
        if (ttl_seconds < 300) {
            ttl_seconds = 300;
        }
        redisReply *reply = (redisReply *)redisCommand(context_,
                                                       "SET %s 1 EX %d", key.c_str(), ttl_seconds);

        if (reply == nullptr)
        {
            std::cerr << "Redis command failed" << std::endl;
            return false;
        }

        bool success = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::string(reply->str) == "OK";
        freeReplyObject(reply);
        return success;
    }

    bool RedisClient::UnregisterNode(const std::string &pod_ip)
    {
        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return false;
        }

        std::string heartbeat_key = NODE_HEARTBEAT_PREFIX + pod_ip;
        redisReply *reply = (redisReply *)redisCommand(context_,
                                                       "DEL %s %s", NODES_SET_KEY.c_str(), heartbeat_key.c_str());

        if (reply == nullptr)
        {
            std::cerr << "Redis command failed" << std::endl;
            return false;
        }

        bool success = reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0;
        freeReplyObject(reply);

        if (success)
        {
            std::cout << "Node " << pod_ip << " unregistered from Redis" << std::endl;
        }
        return success;
    }

    std::vector<std::string> RedisClient::GetAllNodes()
    {
        std::vector<std::string> nodes;

        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return nodes;
        }

        redisReply *reply = (redisReply *)redisCommand(context_,
                                                       "SMEMBERS %s", NODES_SET_KEY.c_str());

        if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY)
        {
            if (reply)
                freeReplyObject(reply);
            return nodes;
        }

        for (size_t i = 0; i < reply->elements; i++)
        {
            if (reply->element[i]->type == REDIS_REPLY_STRING)
            {
                std::string pod_ip(reply->element[i]->str, reply->element[i]->len);
                std::string heartbeat_key = NODE_HEARTBEAT_PREFIX + pod_ip;

                redisReply *heartbeat_reply = (redisReply *)redisCommand(context_, "EXISTS %s", heartbeat_key.c_str());
                if (heartbeat_reply == nullptr)
                {
                    continue;
                }

                bool alive = heartbeat_reply->type == REDIS_REPLY_INTEGER && heartbeat_reply->integer == 1;
                freeReplyObject(heartbeat_reply);

                if (alive)
                {
                    nodes.push_back(pod_ip);
                }
                else
                {
                    redisReply *cleanup_reply = (redisReply *)redisCommand(context_,
                                                                           "SREM %s %s", NODES_SET_KEY.c_str(), pod_ip.c_str());
                    if (cleanup_reply)
                    {
                        freeReplyObject(cleanup_reply);
                    }
                }
            }
        }

        freeReplyObject(reply);
        return nodes;
    }

    std::vector<std::pair<std::string, std::string>> RedisClient::GetAllRoomSnapshots()
    {
        std::vector<std::pair<std::string, std::string>> snapshots;

        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return snapshots;
        }

        constexpr int kScanCount = 128;
        unsigned long long cursor = 0;

        do
        {
            redisReply *scan_reply = (redisReply *)redisCommand(
                context_, "SCAN %llu MATCH %s COUNT %d", cursor, "pokemon:room:*", kScanCount);

            if (scan_reply == nullptr || scan_reply->type != REDIS_REPLY_ARRAY || scan_reply->elements < 2)
            {
                if (scan_reply)
                {
                    freeReplyObject(scan_reply);
                }
                break;
            }

            redisReply *cursor_reply = scan_reply->element[0];
            redisReply *keys_reply = scan_reply->element[1];
            cursor = std::strtoull(cursor_reply->str, nullptr, 10);

            if (keys_reply->type == REDIS_REPLY_ARRAY)
            {
                for (size_t i = 0; i < keys_reply->elements; ++i)
                {
                    redisReply *key_reply = keys_reply->element[i];
                    if (key_reply == nullptr || key_reply->type != REDIS_REPLY_STRING)
                    {
                        continue;
                    }

                    std::string key(key_reply->str, key_reply->len);
                    if (key.size() >= 8 && key.compare(key.size() - 8, 8, ":command") == 0)
                    {
                        continue;
                    }

                    redisReply *value_reply = (redisReply *)redisCommand(context_, "GET %s", key.c_str());
                    if (value_reply == nullptr)
                    {
                        continue;
                    }

                    if (value_reply->type == REDIS_REPLY_STRING)
                    {
                        snapshots.emplace_back(key, std::string(value_reply->str, value_reply->len));
                    }

                    freeReplyObject(value_reply);
                }
            }

            freeReplyObject(scan_reply);
        } while (cursor != 0);

        return snapshots;
    }

    bool RedisClient::SetRoomData(const std::string &room_id, const std::string &data)
    {
        if (!IsConnected())
        {
            std::cerr << "[Redis ERROR] Not connected, cannot set room data" << std::endl;
            return false;
        }

        std::string key = "pokemon:room:" + room_id;
        std::cout << "[Redis SET] Writing key=" << key << " data_len=" << data.length()
                  << " data=" << data.substr(0, 100) << (data.length() > 100 ? "..." : "") << std::endl;
        
        // Use binary-safe command interface to handle JSON data properly
        const char *argv[] = {"SET", key.c_str(), data.c_str(), "EX", "3600"};
        size_t argvlen[] = {3, key.length(), data.length(), 2, 4};
        
        redisReply *reply = (redisReply *)redisCommandArgv(context_, 5, argv, argvlen);

        if (reply == nullptr)
        {
            std::cerr << "[Redis ERROR] Command failed for key=" << key << std::endl;
            return false;
        }

        bool success = reply->type == REDIS_REPLY_STATUS &&
                       std::string(reply->str) == "OK";
        if (success) {
            std::cout << "[Redis SUCCESS] SET key=" << key << " completed (TTL=3600s)" << std::endl;
        } else {
            std::cerr << "[Redis ERROR] SET key=" << key << " failed, reply type=" << reply->type;
            if (reply->type == REDIS_REPLY_ERROR) {
                std::cerr << " error=" << reply->str;
            }
            std::cerr << std::endl;
        }
        freeReplyObject(reply);
        return success;
    }



    bool RedisClient::DeleteRoomData(const std::string &room_id)
    {
        if (!IsConnected())
        {
            std::cerr << "[Redis ERROR] Not connected, cannot delete room data" << std::endl;
            return false;
        }

        std::string room_key = "pokemon:room:" + room_id;
        std::string command_key = room_key + ":command";
        std::cout << "[Redis DEL] Deleting keys=" << room_key << ", " << command_key << std::endl;

        redisReply *reply = (redisReply *)redisCommand(context_,
                                                       "DEL %s %s", room_key.c_str(), command_key.c_str());

        if (reply == nullptr)
        {
            std::cerr << "[Redis ERROR] DEL command failed for keys=" << room_key << ", " << command_key << std::endl;
            return false;
        }

        bool success = reply->type == REDIS_REPLY_INTEGER;
        if (success) {
            std::cout << "[Redis SUCCESS] DEL keys=" << room_key << ", " << command_key
                      << " deleted " << reply->integer << " key(s)" << std::endl;
        } else {
            std::cerr << "[Redis ERROR] DEL keys=" << room_key << ", " << command_key
                      << " failed, reply type=" << reply->type << std::endl;
        }
        freeReplyObject(reply);
        return success;
    }


    bool RedisClient::DeleteRoomsForServer(const std::string &pod_ip)
    {
        if (!IsConnected())
        {
            std::cerr << "Redis is not connected" << std::endl;
            return false;
        }

        constexpr int kScanCount = 128;
        unsigned long long cursor = 0;
        bool any_deleted = false;

        do
        {
            redisReply *scan_reply = (redisReply *)redisCommand(
                context_, "SCAN %llu MATCH %s COUNT %d", cursor, "pokemon:room:*", kScanCount);

            if (scan_reply == nullptr || scan_reply->type != REDIS_REPLY_ARRAY || scan_reply->elements < 2)
            {
                if (scan_reply)
                    freeReplyObject(scan_reply);
                break;
            }

            redisReply *cursor_reply = scan_reply->element[0];
            redisReply *keys_reply = scan_reply->element[1];
            cursor = std::strtoull(cursor_reply->str, nullptr, 10);

            if (keys_reply->type == REDIS_REPLY_ARRAY)
            {
                for (size_t i = 0; i < keys_reply->elements; ++i)
                {
                    redisReply *key_reply = keys_reply->element[i];
                    if (key_reply == nullptr || key_reply->type != REDIS_REPLY_STRING)
                    {
                        continue;
                    }

                    std::string key(key_reply->str, key_reply->len);
                    if (key.size() >= 8 && key.compare(key.size() - 8, 8, ":command") == 0)
                    {
                        continue;
                    }

                    redisReply *value_reply = (redisReply *)redisCommand(context_, "GET %s", key.c_str());
                    if (value_reply == nullptr)
                    {
                        continue;
                    }

                    if (value_reply->type == REDIS_REPLY_STRING)
                    {
                        try
                        {
                            auto j = nlohmann::json::parse(std::string(value_reply->str, value_reply->len));
                            if (j.contains("server_id") && j["server_id"].is_string() && j["server_id"].get<std::string>() == pod_ip)
                            {
                                std::string command_key = key + ":command";
                                redisReply *del_reply = (redisReply *)redisCommand(context_, "DEL %s %s", key.c_str(), command_key.c_str());
                                if (del_reply)
                                {
                                    freeReplyObject(del_reply);
                                }
                                any_deleted = true;
                            }
                        }
                        catch (const std::exception &)
                        {
                            // ignore parse errors
                        }
                    }

                    freeReplyObject(value_reply);
                }
            }

            freeReplyObject(scan_reply);
        } while (cursor != 0);

        return any_deleted;
    }

} // namespace pokemon_game
