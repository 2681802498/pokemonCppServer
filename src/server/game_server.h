#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include "room_service.grpc.pb.h"
#include "room_manager.h"
#include "battle_engine.h"
#include "redis_client.h"

namespace pokemon_game
{

    class GameServiceImpl final : public calc::Calculator::Service
    {
    public:
        GameServiceImpl(std::shared_ptr<RoomManager> room_manager,
                        std::shared_ptr<BattleEngine> battle_engine,
                        std::shared_ptr<RedisClient> redis_client,
                        const std::string &server_id,
                        const std::string &pod_ip);

        grpc::Status CreateRoom(grpc::ServerContext *context,
                                const calc::CreateRoomRequest *request,
                                calc::CommonResponse *response) override;

        grpc::Status SendCommand(grpc::ServerContext *context,
                                 const calc::GameCommand *request,
                                 calc::CommonResponse *response) override;

        grpc::Status DestroyRoom(grpc::ServerContext *context,
                                 const calc::DestroyRoomRequest *request,
                                 calc::DestroyRoomResponse *response) override;

        grpc::Status GetHeartbeat(grpc::ServerContext *context,
                                  const calc::HeartbeatRequest *request,
                                  calc::HeartbeatResponse *response) override;

    private:
        std::shared_ptr<RoomManager> room_manager_;
        std::shared_ptr<BattleEngine> battle_engine_;
        std::shared_ptr<RedisClient> redis_client_;
        std::string server_id_;
        std::string pod_ip_;
    };

    class GameServer
    {
    public:
        GameServer(const std::string &pod_ip, const std::string &redis_url);
        ~GameServer();

        // Initialize and start the server
        bool Start();

        // Stop the server
        void Stop();

        // Wait for server to stop
        void Wait();

        // Get room manager
        std::shared_ptr<RoomManager> GetRoomManager()
        {
            return room_manager_;
        }

        std::shared_ptr<BattleEngine> GetBattleEngine()
        {
            return battle_engine_;
        }

        // Check if server is running
        bool IsRunning() const;

        // Graceful shutdown
        void GracefulShutdown();

    private:
        std::string pod_ip_;
        std::string redis_url_;
        std::string server_id_;
        std::shared_ptr<RoomManager> room_manager_;
        std::shared_ptr<BattleEngine> battle_engine_;
        std::shared_ptr<RedisClient> redis_client_;
        std::unique_ptr<GameServiceImpl> service_;
        std::unique_ptr<grpc::Server> server_;
        std::string server_address_;
        int grpc_port_;
        int metrics_port_;
        std::atomic<bool> metrics_running_;
        std::thread metrics_thread_;
        std::atomic<bool> heartbeat_running_;
        std::thread heartbeat_thread_;

        std::string GenerateServerId();

        bool StartMetricsServer();
        void StopMetricsServer();
    };

} // namespace pokemon_game
