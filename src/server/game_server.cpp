#include "game_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <random>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace pokemon_game
{

    static json BuildRoomSnapshotJson(const std::string &room_id, int status, int node_id, const std::string &server_id)
    {
        return json{
            {"room_id", room_id},
            {"status", status},
            {"node_id", node_id},
            {"server_id", server_id},
            {"players", json::array()},
            {"ready_players", json::object()},
            {"selected_pokemon", json::object()},
            {"updated_at", static_cast<int64_t>(std::time(nullptr))}};
    }

    static void LogRpcRequest(const char *method, grpc::ServerContext *context)
    {
        std::cout << "[RPC] " << method << " received from " << context->peer() << std::endl;
    }

    GameServiceImpl::GameServiceImpl(std::shared_ptr<RoomManager> room_manager,
                                     std::shared_ptr<BattleEngine> battle_engine,
                                     std::shared_ptr<RedisClient> redis_client,
                                     const std::string &server_id,
                                     const std::string &pod_ip)
        : room_manager_(room_manager), battle_engine_(battle_engine), redis_client_(redis_client), server_id_(server_id), pod_ip_(pod_ip) {}

    grpc::Status GameServiceImpl::CreateRoom(grpc::ServerContext *context,
                                             const calc::CreateRoomRequest *request,
                                             calc::CommonResponse *response)
    {
        LogRpcRequest("CreateRoom", context);

        // Check if in maintenance mode
        if (room_manager_->IsInMaintenanceMode())
        {
            response->set_code(1);
            response->set_message("Server is in maintenance mode");
            return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                                "Server is in maintenance mode");
        }

        // Check if we can create more rooms
        if (!room_manager_->CanCreateRoom())
        {
            response->set_code(2);
            response->set_message("Cannot create more rooms, limit reached");
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "Room limit reached");
        }

        // Create room with provided room_id
        std::string room_id = request->room_id();
        if (room_id.empty())
        {
            response->set_code(3);
            response->set_message("Room ID cannot be empty");
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "Room ID cannot be empty");
        }

        // Parse init_json and create room
        json init_data;
        try
        {
            init_data = json::parse(request->init_json());
        }
        catch (const std::exception &e)
        {
            response->set_code(5);
            response->set_message(std::string("Failed to parse init_json: ") + e.what());
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "Invalid init_json format");
        }

        // Create room with default parameters
        std::string actual_room_id = room_manager_->CreateRoomWithId(
            room_id, "Battle Room", 2);

        if (actual_room_id.empty())
        {
            response->set_code(4);
            response->set_message("Failed to create room");
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to create room");
        }

        std::string battle_error;
        if (!battle_engine_ || !battle_engine_->CreateSession(actual_room_id, init_data, &battle_error))
        {
            std::cerr << "[gRPC CreateRoom] Failed to initialize battle session for room " << actual_room_id
                      << ": " << battle_error << std::endl;
            room_manager_->CloseRoom(actual_room_id);
            response->set_code(6);
            response->set_message("Failed to initialize battle session: " + battle_error);
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to initialize battle session");
        }

        nlohmann::json room_state;
        if (battle_engine_->GetState(actual_room_id, &room_state, &battle_error))
        {
            std::cout << "[gRPC CreateRoom] Initial battle state: room_id=" << actual_room_id
                      << " state=" << room_state.dump() << std::endl;
            bool redis_ok = redis_client_->SetRoomData(actual_room_id, room_state.dump());
            if (!redis_ok)
            {
                std::cerr << "[gRPC CreateRoom] WARNING: Failed to store initial battle state in Redis!" << std::endl;
            }
        }

        response->set_code(0);
        response->set_message("Room created successfully");
        std::cout << "Room created via gRPC: " << actual_room_id << std::endl;
        return grpc::Status::OK;
    }

    grpc::Status GameServiceImpl::SendCommand(grpc::ServerContext *context,
                                              const calc::GameCommand *request,
                                              calc::CommonResponse *response)
    {
        LogRpcRequest("SendCommand", context);

        Room room;
        if (!room_manager_->GetRoomStatus(request->room_id(), room))
        {
            response->set_code(1);
            response->set_message("Room not found");
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Room not found");
        }

        if (!room.is_active)
        {
            response->set_code(2);
            response->set_message("Room is not active");
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Room is not active");
        }

        json turn_request;
        try
        {
            turn_request = json::parse(request->action());
        }
        catch (const std::exception &e)
        {
            response->set_code(3);
            response->set_message(std::string("Failed to parse action json: ") + e.what());
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "Invalid action json format");
        }

        nlohmann::json battle_response;
        std::string battle_error;
        if (!battle_engine_ || !battle_engine_->ProcessTurn(request->room_id(), turn_request, &battle_response, &battle_error))
        {
            std::cerr << "[gRPC SendCommand] Failed to process battle turn for room " << request->room_id()
                      << ": " << battle_error << std::endl;
            response->set_code(4);
            response->set_message("Failed to process battle turn: " + battle_error);
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to process battle turn");
        }

        // Store the full battle state in Redis
        bool redis_ok = redis_client_->SetRoomData(request->room_id(), battle_response.dump());
        if (!redis_ok)
        {
            std::cerr << "[gRPC SendCommand] WARNING: Failed to store battle state in Redis!" << std::endl;
        }

        response->set_code(0);
        // Return the full battle response JSON back to the Go server so it can sync actions/state
        try
        {
            response->set_message(battle_response.dump());
        }
        catch (const std::exception &e)
        {
            // Fallback to a simple message if serialization fails
            response->set_message(std::string("Battle turn processed successfully (failed to serialize response): ") + e.what());
        }
        std::cout << "Command processed for room " << request->room_id()
                  << " from player " << request->player_id()
                  << " action: " << request->action() << std::endl;
        return grpc::Status::OK;
    }

    grpc::Status GameServiceImpl::DestroyRoom(grpc::ServerContext *context,
                                              const calc::DestroyRoomRequest *request,
                                              calc::DestroyRoomResponse *response)
    {
        LogRpcRequest("DestroyRoom", context);

        if (!room_manager_->CloseRoom(request->room_id()))
        {
            response->set_code(1);
            response->set_message("Room not found");
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                                "Room not found");
        }

        if (battle_engine_)
        {
            battle_engine_->DestroySession(request->room_id());
        }

        // Delete from Redis
        std::cout << "[gRPC DestroyRoom] Deleting room from Redis: room_id=" << request->room_id() << std::endl;
        redis_client_->DeleteRoomData(request->room_id());

        response->set_code(0);
        response->set_message("Room destroyed successfully");
        std::cout << "Room destroyed via gRPC: " << request->room_id() << std::endl;
        return grpc::Status::OK;
    }

    grpc::Status GameServiceImpl::GetHeartbeat(grpc::ServerContext *context,
                                               const calc::HeartbeatRequest *request,
                                               calc::HeartbeatResponse *response)
    {
        LogRpcRequest("GetHeartbeat", context);

        int active_rooms = room_manager_->GetRoomCount();
        int max_rooms = room_manager_->GetMaxRooms();
        redis_client_->TouchNodeHeartbeat(pod_ip_);
        response->set_code(0);
        response->set_active_rooms(active_rooms);
        response->set_cpu_usage(0.0f); // TODO: Implement actual CPU usage detection
        response->set_memory_used(0);  // TODO: Implement actual memory usage detection
        response->set_max_capacity(max_rooms);
        response->set_server_id(server_id_);

        std::cout << "Heartbeat received. Active rooms: " << active_rooms << ", server_id: " << server_id_ << std::endl;
        return grpc::Status::OK;
    }

    std::string GameServer::GenerateServerId()
    {
        // Generate a unique server ID using timestamp and random components
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(1000, 9999);
        
        std::stringstream ss;
        ss << "server-" << millis << "-" << dis(gen);
        return ss.str();
    }

    GameServer::GameServer(const std::string &pod_ip, const std::string &redis_url)
        : pod_ip_(pod_ip), redis_url_(redis_url), grpc_port_(50051), metrics_port_(9102), metrics_running_(false), heartbeat_running_(false)
    {
        server_id_ = GenerateServerId();
        room_manager_ = std::make_shared<RoomManager>(10); // Max 10 rooms
        battle_engine_ = std::make_shared<BattleEngine>();
        redis_client_ = std::make_shared<RedisClient>();

        server_address_ = pod_ip + ":50051";
    }

    GameServer::~GameServer()
    {
        Stop();
    }

    bool GameServer::Start()
    {
        // Parse Redis URL
        std::string redis_host = "localhost";
        int redis_port = 6379;

        // Simple parsing: redis_url format expected to be "host:port"
        size_t colon_pos = redis_url_.find(':');
        if (colon_pos != std::string::npos)
        {
            redis_host = redis_url_.substr(0, colon_pos);
            try
            {
                redis_port = std::stoi(redis_url_.substr(colon_pos + 1));
            }
            catch (...)
            {
                std::cerr << "Invalid Redis URL format" << std::endl;
                return false;
            }
        }
        else
        {
            redis_host = redis_url_;
        }

        // Connect to Redis
        if (!redis_client_->Connect(redis_host, redis_port))
        {
            std::cerr << "Failed to connect to Redis" << std::endl;
            return false;
        }

        // Clean up any leftover rooms for this pod (in case of previous crash)
        redis_client_->DeleteRoomsForServer(pod_ip_);

        // Register this node in Redis
        if (!redis_client_->RegisterNode(pod_ip_))
        {
            std::cerr << "Failed to register node in Redis" << std::endl;
            return false;
        }

        // Create gRPC service with server_id
        service_ = std::make_unique<GameServiceImpl>(room_manager_, battle_engine_, redis_client_, server_id_, pod_ip_);
        std::cout << "GameServer initialized with server_id: " << server_id_ << std::endl;

        // Build and start gRPC server
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        builder.RegisterService(service_.get());

        server_ = builder.BuildAndStart();

        if (!server_)
        {
            std::cerr << "Failed to start gRPC server" << std::endl;
            return false;
        }

        std::cout << "Server started at " << pod_ip_ << ", connecting to Redis at "
                  << redis_url_ << std::endl;

        if (!StartMetricsServer())
        {
            std::cerr << "Failed to start metrics server on port " << metrics_port_ << std::endl;
        }

        // Start heartbeat refresh thread to prevent expiration
        heartbeat_running_ = true;
        heartbeat_thread_ = std::thread([this]()
                                        {
            while (heartbeat_running_)
            {
                std::this_thread::sleep_for(std::chrono::seconds(60));
                if (redis_client_ && redis_client_->IsConnected())
                {
                    redis_client_->TouchNodeHeartbeat(pod_ip_, 300);
                }
            } });

        return true;
    }

    void GameServer::Stop()
    {
        heartbeat_running_ = false;
        if (heartbeat_thread_.joinable())
        {
            heartbeat_thread_.join();
        }
        StopMetricsServer();
        if (server_)
        {
            server_->Shutdown();
            server_.reset();
        }
    }

    void GameServer::Wait()
    {
        if (server_)
        {
            server_->Wait();
        }
    }

    bool GameServer::IsRunning() const
    {
        return server_ != nullptr;
    }

    void GameServer::GracefulShutdown()
    {
        std::cout << "\nInitiating graceful shutdown..." << std::endl;

        // Set maintenance mode
        room_manager_->SetMaintenanceMode(true);

        // Wait for rooms to clear
        std::cout << "Waiting for rooms to clear..." << std::endl;
        for (int i = 0; i < 5; i++)
        {
            int room_count = room_manager_->GetRoomCount();
            std::cout << "Waiting for rooms to clear... (Active rooms: "
                      << room_count << ")" << std::endl;
            sleep(1);
        }

        // Close all remaining rooms
        auto rooms = room_manager_->GetAllRooms();
        for (const auto &pair : rooms)
        {
            std::cout << "[GracefulShutdown] Closing room: room_id=" << pair.first << std::endl;
            room_manager_->CloseRoom(pair.first);
            // Keep room data in Redis for migration to other servers
            std::cout << "[GracefulShutdown] Room data retained in Redis for migration: room_id=" << pair.first << std::endl;
        }

        // Unregister from Redis
        redis_client_->UnregisterNode(pod_ip_);

        // Stop server
        Stop();

        std::cout << "Graceful shutdown completed" << std::endl;
    }

    struct MetricsSnapshot
    {
        int local_active_rooms = 0;
        int local_max_rooms = 0;
        int global_active_rooms = 0;
        int global_capacity = 0;
        double local_utilization = 0.0;
        double global_utilization = 0.0;
        double min_utilization = 0.0;
    };

    static MetricsSnapshot CollectMetricsSnapshot(const std::shared_ptr<RoomManager> &room_manager,
                                                  const std::shared_ptr<RedisClient> &redis_client,
                                                  const std::string &pod_ip)
    {
        MetricsSnapshot snapshot;
        snapshot.local_active_rooms = room_manager->GetRoomCount();
        snapshot.local_max_rooms = room_manager->GetMaxRooms();

        if (redis_client && redis_client->IsConnected())
        {
            redis_client->TouchNodeHeartbeat(pod_ip);
        }

        if (snapshot.local_max_rooms > 0)
        {
            snapshot.local_utilization = static_cast<double>(snapshot.local_active_rooms) /
                                         static_cast<double>(snapshot.local_max_rooms);
        }

        if (!redis_client || !redis_client->IsConnected())
        {
            return snapshot;
        }

        const auto nodes = redis_client->GetAllNodes();
        const auto room_snapshots = redis_client->GetAllRoomSnapshots();

        for (const auto &entry : room_snapshots)
        {
            try
            {
                auto room_json = json::parse(entry.second);
                if (!room_json.contains("status") || room_json["status"].get<int>() != 1)
                {
                    continue;
                }
                snapshot.global_active_rooms++;
            }
            catch (const std::exception &)
            {
                continue;
            }
        }

        const int active_nodes = static_cast<int>(nodes.size());
        snapshot.global_capacity = active_nodes * snapshot.local_max_rooms;
        if (snapshot.global_capacity > 0)
        {
            snapshot.global_utilization = static_cast<double>(snapshot.global_active_rooms) /
                                          static_cast<double>(snapshot.global_capacity);
        }

        // Backward-compatible field; keep aligned with the global utilization ratio.
        snapshot.min_utilization = snapshot.global_utilization;

        return snapshot;
    }

    static std::string BuildMetricsBody(const MetricsSnapshot &snapshot)
    {
        std::ostringstream body;
        body << "# HELP pokemon_active_rooms Current number of active rooms\n";
        body << "# TYPE pokemon_active_rooms gauge\n";
        body << "pokemon_active_rooms " << snapshot.local_active_rooms << "\n";
        body << "# HELP pokemon_max_capacity Maximum room capacity\n";
        body << "# TYPE pokemon_max_capacity gauge\n";
        body << "pokemon_max_capacity " << snapshot.local_max_rooms << "\n";
        body << "# HELP pokemon_room_utilization Local room utilization\n";
        body << "# TYPE pokemon_room_utilization gauge\n";
        body << "pokemon_room_utilization " << snapshot.local_utilization << "\n";
        body << "# HELP pokemon_room_utilization_global Global room utilization across all registered pods\n";
        body << "# TYPE pokemon_room_utilization_global gauge\n";
        body << "pokemon_room_utilization_global " << snapshot.global_utilization << "\n";
        body << "# HELP pokemon_room_utilization_min Minimum room utilization across active pods\n";
        body << "# TYPE pokemon_room_utilization_min gauge\n";
        body << "pokemon_room_utilization_min " << snapshot.min_utilization << "\n";
        return body.str();
    }

    bool GameServer::StartMetricsServer()
    {
        metrics_running_ = true;
        metrics_thread_ = std::thread([this]()
                                      {
            int server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0)
            {
                return;
            }

            int opt = 1;
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(metrics_port_);

            if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
            {
                close(server_fd);
                return;
            }

            if (listen(server_fd, 16) < 0)
            {
                close(server_fd);
                return;
            }

            while (metrics_running_)
            {
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(server_fd, &read_fds);

                timeval timeout{};
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;

                int ready = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
                if (!metrics_running_)
                {
                    break;
                }
                if (ready <= 0)
                {
                    continue;
                }

                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd < 0)
                {
                    continue;
                }

                char buffer[1024];
                ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_read <= 0)
                {
                    close(client_fd);
                    continue;
                }

                buffer[bytes_read] = '\0';
                std::string request_line(buffer);
                bool is_metrics = request_line.rfind("GET /metrics", 0) == 0;

                std::string body;
                std::ostringstream response;
                if (is_metrics)
                {
                    MetricsSnapshot metrics_snapshot = CollectMetricsSnapshot(room_manager_, redis_client_, pod_ip_);
                    body = BuildMetricsBody(metrics_snapshot);
                    response << "HTTP/1.1 200 OK\r\n"
                             << "Content-Type: text/plain; version=0.0.4\r\n"
                             << "Content-Length: " << body.size() << "\r\n\r\n"
                             << body;
                }
                else
                {
                    body = "Not Found\n";
                    response << "HTTP/1.1 404 Not Found\r\n"
                             << "Content-Type: text/plain\r\n"
                             << "Content-Length: " << body.size() << "\r\n\r\n"
                             << body;
                }

                std::string response_str = response.str();
                send(client_fd, response_str.c_str(), response_str.size(), 0);
                close(client_fd);
            }

            close(server_fd); });

        return true;
    }

    void GameServer::StopMetricsServer()
    {
        metrics_running_ = false;
        if (metrics_thread_.joinable())
        {
            metrics_thread_.join();
        }
    }

} // namespace pokemon_game
