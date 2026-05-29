#pragma once

#include <string>
#include <map>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <chrono>

namespace pokemon_game
{

    struct Room
    {
        std::string id;
        std::string name;
        int player_count;
        int max_players;
        bool is_active;
        std::chrono::system_clock::time_point created_time;
        int counter; // Counter for simulating room activity
    };

    class RoomManager
    {
    public:
        RoomManager(int max_rooms = 10);

        // Create a new room with auto-generated ID
        std::string CreateRoom(const std::string &room_name, int max_players);

        // Create a new room with specified ID
        std::string CreateRoomWithId(const std::string &room_id, const std::string &room_name, int max_players);

        // Get room status
        bool GetRoomStatus(const std::string &room_id, Room &out_room);

        // Close a room
        bool CloseRoom(const std::string &room_id);

        // Get all active rooms
        std::map<std::string, Room> GetAllRooms();

        // Get current room count
        int GetRoomCount() const;

        // Get max room capacity
        int GetMaxRooms() const;

        // Check if can create more rooms
        bool CanCreateRoom() const;

        // Simulate room activity (increment counters)
        void UpdateRoomActivity();

        // Set maintenance mode (no new rooms allowed)
        void SetMaintenanceMode(bool is_maintaining)
        {
            is_maintaining_ = is_maintaining;
        }

        // Check if in maintenance mode
        bool IsInMaintenanceMode() const
        {
            return is_maintaining_;
        }

    private:
        std::map<std::string, Room> rooms_;
        mutable std::shared_mutex mutex_;  // Changed from std::mutex to std::shared_mutex for read/write locking
        std::atomic<int> room_count_{0};   // Atomic counter for fast heartbeat queries
        int max_rooms_;
        int room_counter_; // For generating unique room IDs
        bool is_maintaining_;

        std::string GenerateRoomId();
    };

} // namespace pokemon_game
