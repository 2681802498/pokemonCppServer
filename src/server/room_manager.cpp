#include "room_manager.h"
#include <sstream>
#include <iostream>
#include <random>
#include <iomanip>

namespace pokemon_game
{

    RoomManager::RoomManager(int max_rooms)
        : max_rooms_(max_rooms), room_counter_(0), is_maintaining_(false) {}

    std::string RoomManager::GenerateRoomId()
    {
        // Generate UUID-like string using random number generator
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        for (int i = 0; i < 32; i++)
        {
            if (i == 8 || i == 12 || i == 16 || i == 20)
            {
                ss << "-";
            }
            int val = dis(gen);
            ss << std::hex << val;
        }

        return ss.str();
    }

    std::string RoomManager::CreateRoom(const std::string &room_name, int max_players)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check maintenance mode
        if (is_maintaining_)
        {
            std::cerr << "Server is in maintenance mode, cannot create new rooms" << std::endl;
            return "";
        }

        // Check room limit
        if (rooms_.size() >= static_cast<size_t>(max_rooms_))
        {
            std::cerr << "Reached maximum room limit: " << max_rooms_ << std::endl;
            return "";
        }

        // Create new room
        std::string room_id = GenerateRoomId();
        Room room;
        room.id = room_id;
        room.name = room_name;
        room.player_count = 0;
        room.max_players = max_players;
        room.is_active = true;
        room.created_time = std::chrono::system_clock::now();
        room.counter = 0;

        rooms_[room_id] = room;

        std::cout << "Room created: " << room_id << " (" << room_name << ")" << std::endl;
        return room_id;
    }

    std::string RoomManager::CreateRoomWithId(const std::string &room_id, const std::string &room_name, int max_players)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check maintenance mode
        if (is_maintaining_)
        {
            std::cerr << "Server is in maintenance mode, cannot create new rooms" << std::endl;
            return "";
        }

        // Check room limit
        if (rooms_.size() >= static_cast<size_t>(max_rooms_))
        {
            std::cerr << "Reached maximum room limit: " << max_rooms_ << std::endl;
            return "";
        }

        // Check if room_id already exists
        if (rooms_.find(room_id) != rooms_.end())
        {
            std::cerr << "Room ID already exists: " << room_id << std::endl;
            return "";
        }

        // Create new room with specified ID
        Room room;
        room.id = room_id;
        room.name = room_name;
        room.player_count = 0;
        room.max_players = max_players;
        room.is_active = true;
        room.created_time = std::chrono::system_clock::now();
        room.counter = 0;

        rooms_[room_id] = room;

        std::cout << "Room created: " << room_id << " (" << room_name << ")" << std::endl;
        return room_id;
    }

    bool RoomManager::GetRoomStatus(const std::string &room_id, Room &out_room)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = rooms_.find(room_id);
        if (it == rooms_.end())
        {
            return false;
        }

        out_room = it->second;
        return true;
    }

    bool RoomManager::CloseRoom(const std::string &room_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = rooms_.find(room_id);
        if (it == rooms_.end())
        {
            return false;
        }

        rooms_.erase(it);
        std::cout << "Room closed: " << room_id << std::endl;
        return true;
    }

    std::map<std::string, Room> RoomManager::GetAllRooms()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return rooms_;
    }

    int RoomManager::GetRoomCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return rooms_.size();
    }

    int RoomManager::GetMaxRooms() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_rooms_;
    }

    bool RoomManager::CanCreateRoom() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !is_maintaining_ && rooms_.size() < static_cast<size_t>(max_rooms_);
    }

    void RoomManager::UpdateRoomActivity()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Simulate room activity by incrementing counters
        for (auto &pair : rooms_)
        {
            pair.second.counter++;
            // Simulate player activity: randomly add/remove players
            if (pair.second.counter % 3 == 0 && pair.second.player_count < pair.second.max_players)
            {
                pair.second.player_count++;
            }
            else if (pair.second.counter % 5 == 0 && pair.second.player_count > 0)
            {
                pair.second.player_count--;
            }
        }
    }

} // namespace pokemon_game
