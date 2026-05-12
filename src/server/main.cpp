#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include "game_server.h"
#include "signal_handler.h"

using namespace pokemon_game;

int main(int argc, char *argv[])
{
    // Read environment variables
    const char *pod_ip = std::getenv("POD_IP");
    const char *redis_url = std::getenv("REDIS_URL");

    // Set defaults for local development
    std::string pod_ip_str = pod_ip ? pod_ip : "127.0.0.1";
    std::string redis_url_str = redis_url ? redis_url : "127.0.0.1:6379";

    std::cout << "=== Pokemon Game Server ===" << std::endl;
    std::cout << "POD_IP: " << pod_ip_str << std::endl;
    std::cout << "REDIS_URL: " << redis_url_str << std::endl;

    // Create game server
    GameServer server(pod_ip_str, redis_url_str);

    // Initialize signal handler
    SignalHandler &signal_handler = SignalHandler::GetInstance();
    signal_handler.RegisterSigTermHandler([&server]()
                                          { server.GracefulShutdown(); });

    // Start server
    if (!server.Start())
    {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "Server started at " << pod_ip_str
              << ", connecting to Redis at " << redis_url_str << std::endl;

    std::thread sigterm_thread([&signal_handler, &server]()
                               {
        while (server.IsRunning()) {
            if (signal_handler.RunHandlerIfReceived()) {
                std::cout << "\nReceived SIGTERM signal" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } });

    // Simulate room activity in background thread
    std::thread activity_thread([&server]()
                                {
        auto room_manager = server.GetRoomManager();
        while (server.IsRunning()) {
            room_manager->UpdateRoomActivity();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } });

    std::cout << "Waiting for grpc requests..." << std::endl;

    // Wait for shutdown signal
    server.Wait();

    if (sigterm_thread.joinable())
    {
        sigterm_thread.join();
    }

    activity_thread.join();

    std::cout << "Server stopped" << std::endl;
    return 0;
}
