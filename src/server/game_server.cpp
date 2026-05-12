#include "game_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <random>
#include <chrono>
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
                                     std::shared_ptr<RedisClient> redis_client,
                                     const std::string &server_id)
        : room_manager_(room_manager), redis_client_(redis_client), server_id_(server_id) {}

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
        try
        {
            json init_data = json::parse(request->init_json());
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

            // Store room snapshot in Redis for Go service consumption
            // Missing fields are stored as placeholders so Go can unmarshal directly.
            // status: 1 = active, node_id: 0 = placeholder
            json room_data = BuildRoomSnapshotJson(actual_room_id, 1, 0, server_id_);

            std::cout << "[gRPC CreateRoom] Preparing to store room snapshot: room_id=" << actual_room_id 
                      << " snapshot=" << room_data.dump() << std::endl;
            bool redis_ok = redis_client_->SetRoomData(actual_room_id, room_data.dump());
            if (redis_ok) {
                std::cout << "[gRPC CreateRoom] Room snapshot stored successfully" << std::endl;
            } else {
                std::cerr << "[gRPC CreateRoom] WARNING: Failed to store room snapshot in Redis!" << std::endl;
            }

            response->set_code(0);
            response->set_message("Room created successfully");
            std::cout << "Room created via gRPC: " << actual_room_id << std::endl;
            return grpc::Status::OK;
        }
        catch (const std::exception &e)
        {
            response->set_code(5);
            response->set_message(std::string("Failed to parse init_json: ") + e.what());
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "Invalid init_json format");
        }
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

        // Store command in Redis for processing
        try
        {
            std::string response_message = "Command received";
            std::time_t now = std::time(nullptr);

            // If action is JSON and matches battle_action, return a timestamp in response
            try
            {
                json action_json = json::parse(request->action());
                if (action_json.contains("cmd") && action_json["cmd"] == "battle_action")
                {
                    response_message = "Command received at " + std::to_string(now);
                }
            }
            catch (const std::exception &)
            {
                // Non-JSON action strings are allowed; keep default response message
            }

            response_message += " | counter=" + std::to_string(room.counter);

            json command_data = {
                {"player_id", request->player_id()},
                {"action", request->action()},
                {"timestamp", now}};

            std::string command_key = request->room_id() + ":command";
            std::cout << "[gRPC SendCommand] Storing command: key=" << command_key 
                      << " data=" << command_data.dump() << std::endl;
            bool redis_ok = redis_client_->SetRoomData(command_key, command_data.dump());
            if (!redis_ok) {
                std::cerr << "[gRPC SendCommand] WARNING: Failed to store command in Redis!" << std::endl;
            }

            response->set_code(0);
            response->set_message(response_message);
            std::cout << "Command received for room " << request->room_id()
                      << " from player " << request->player_id()
                      << " action: " << request->action() << std::endl;
            return grpc::Status::OK;
        }
        catch (const std::exception &e)
        {
            response->set_code(3);
            response->set_message(std::string("Failed to process command: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to process command");
        }
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
        : pod_ip_(pod_ip), redis_url_(redis_url), grpc_port_(50051), metrics_port_(9102), metrics_running_(false)
    {
        server_id_ = GenerateServerId();
        room_manager_ = std::make_shared<RoomManager>(10); // Max 10 rooms
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

        // Register this node in Redis
        if (!redis_client_->RegisterNode(pod_ip_))
        {
            std::cerr << "Failed to register node in Redis" << std::endl;
            return false;
        }

        // Create gRPC service with server_id
        service_ = std::make_unique<GameServiceImpl>(room_manager_, redis_client_, server_id_);
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

        return true;
    }

    void GameServer::Stop()
    {
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

    static std::string BuildMetricsBody(int active_rooms, int max_rooms)
    {
        double utilization = 0.0;
        if (max_rooms > 0)
        {
            utilization = static_cast<double>(active_rooms) / static_cast<double>(max_rooms);
        }

        std::ostringstream body;
        body << "# HELP pokemon_active_rooms Current number of active rooms\n";
        body << "# TYPE pokemon_active_rooms gauge\n";
        body << "pokemon_active_rooms " << active_rooms << "\n";
        body << "# HELP pokemon_max_capacity Maximum room capacity\n";
        body << "# TYPE pokemon_max_capacity gauge\n";
        body << "pokemon_max_capacity " << max_rooms << "\n";
        body << "# HELP pokemon_room_utilization Active room utilization\n";
        body << "# TYPE pokemon_room_utilization gauge\n";
        body << "pokemon_room_utilization " << utilization << "\n";
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
                    int active_rooms = room_manager_->GetRoomCount();
                    int max_rooms = room_manager_->GetMaxRooms();
                    body = BuildMetricsBody(active_rooms, max_rooms);
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
