# Pokemon C++ Game Server - Skeleton Prototype

A high-performance C++ game server skeleton designed for Kubernetes cloud-native deployment.

## Project Structure

```
pokemonCppServer/
├── proto/                    # gRPC protocol buffer definitions
│   └── room_service.proto   # Room service interface
├── src/
│   └── server/              # Game server implementation
│       ├── main.cpp         # Entry point
│       ├── game_server.h    # Game server interface
│       ├── game_server.cpp  # Game server implementation
│       ├── room_manager.h   # Room management interface
│       ├── room_manager.cpp # Room management implementation
│       ├── redis_client.h   # Redis client interface
│       ├── redis_client.cpp # Redis client implementation
│       ├── signal_handler.h # Signal handling interface
│       └── signal_handler.cpp # Signal handling implementation
├── CMakeLists.txt          # CMake build configuration
├── Dockerfile              # Container image definition
├── docker-compose.yml      # Local development compose file
├── k8s-deployment.yaml     # Kubernetes deployment manifest
├── k8s-redis.yaml          # Kubernetes Redis service manifest
└── README.md               # This file

```

## Features

### 1. gRPC Service Interface
- **CreateRoom**: Create new game rooms with specified player capacity
- **GetRoomStatus**: Query current room status and player count
- **CloseRoom**: Close game rooms

### 2. Kubernetes Integration
- Automatic POD_IP discovery from environment
- Redis URL configuration from environment variables
- Graceful shutdown with SIGTERM handling
- Health checks and readiness probes

### 3. Signal Handling & Graceful Shutdown
- SIGTERM signal handlers
- 5-second graceful shutdown period
- Automatic room cleanup
- Redis node deregistration

### 4. Redis Integration
- Server node registration in Redis Set
- Room data persistence
- Built with hiredis C library

### 5. Room Management
- Room creation with UUID-based IDs
- Player count simulation
- Room activity tracking with counters
- Maintenance mode support

### 6. Embedded Simulator Integration
- Go service receives JSON from the front end
- `pokemon_server` directly invokes the embedded simulator through `BattleEngine`
- Battle session state is kept in memory and returned as JSON
- Redis is still available for persistence and monitoring, but no longer used as the primary simulator bridge

## Prerequisites

### Local Development
```bash
# macOS (Homebrew)
brew install cmake protobuf grpc hiredis nlohmann-json uuid

# Ubuntu/Debian
sudo apt-get install cmake protobuf-compiler-grpc libgrpc++-dev \
  libhiredis-dev nlohmann-json3-dev uuid-dev

# Fedora/RHEL
sudo dnf install cmake protobuf-compiler grpc-devel hiredis-devel \
  nlohmann_json-devel util-linux-devel
```

### Docker Development
```bash
docker-compose up --build
```

### Kubernetes
```bash
kubectl apply -f k8s-redis.yaml
kubectl apply -f k8s-deployment.yaml
```

## Building

### CMake Build
```bash
cd pokemonCppServer
mkdir build && cd build
cmake ..
make -j4
./bin/pokemon_server
```

### Docker Build
```bash
docker build -t pokemon-cpp-server:latest .
```

## Environment Variables

- `POD_IP`: Server's pod IP (automatically injected by Kubernetes)
- `REDIS_URL`: Redis server URL (format: `host:port`)

## Running

### Local Development with CMake
```bash
# Terminal 1: Start Redis
redis-server

# Terminal 2: Build and run server
cd build
./bin/pokemon_server
```

### Docker Compose (Recommended for Local Dev)
```bash
docker-compose up --build
```

### Kubernetes Deployment
```bash
# Deploy Redis
kubectl apply -f k8s-redis.yaml

# Deploy game servers (after building Docker image)
kubectl apply -f k8s-deployment.yaml

# Port-forward to test
kubectl port-forward svc/pokemon-server-service 50051:50051
```

## Testing with grpcurl

```bash
# Check server status
grpcurl -plaintext -d '{"room_name": "TestRoom", "max_players": 4}' \
  localhost:50051 pokemon.game.RoomService/CreateRoom

# Get room status
grpcurl -plaintext -d '{"room_id": "your-room-id"}' \
  localhost:50051 pokemon.game.RoomService/GetRoomStatus

# Close room
grpcurl -plaintext -d '{"room_id": "your-room-id"}' \
  localhost:50051 pokemon.game.RoomService/CloseRoom
```

## Graceful Shutdown

The server handles SIGTERM signals for graceful shutdown:

1. Sets maintenance mode (rejects new CreateRoom requests)
2. Prints "Waiting for rooms to clear..." every second for 5 seconds
3. Closes all remaining rooms
4. Unregisters from Redis
5. Exits cleanly

Test graceful shutdown:
```bash
# Get server PID
ps aux | grep pokemon_server

# Send SIGTERM
kill -TERM <PID>
```

## Architecture Notes

- **Thread-safe**: Uses std::mutex for room management
- **Async-ready**: Signal handling with std::atomic flags
- **Container-native**: Respects Kubernetes conventions (POD_IP, graceful shutdown)
- **Redis-backed**: Node discovery via Redis Set
- **Activity simulation**: Automatic player count changes for testing
- **Embedded battle core**: simulator is linked into the server process through `battle_core`

## Request Flow

1. Front end sends JSON to the Go-facing gRPC endpoint.
2. Go service parses the payload and forwards it to `BattleEngine`.
3. `BattleEngine` creates or looks up a `BattleSession` in memory.
4. The simulator processes the turn and returns battle JSON.
5. Go service returns the JSON response to the front end.

## Implementation Details

### Room Manager
- Max 10 rooms per server
- UUID-based room IDs
- Player count simulation (increments/decrements every 1 second)
- Counter per room for activity tracking

### Redis Client
- Set-based node registration (key: `pokemon:server:nodes`)
- Room data keys: `pokemon:room:<room_id>`
- Automatic TTL for room data (3600 seconds)

### Graceful Shutdown
- Catches SIGTERM signal
- Maintains a 5-second grace period
- Logs "Waiting for rooms to clear..." each second
- Automatic cleanup of Redis entries

## Technologies Used

- C++17 required
- gRPC for RPC communication
- hiredis for Redis connection
- CMake for build system
- Docker for containerization
- Kubernetes for orchestration
- UUID for unique ID generation
- nlohmann/json for JSON serialization

## Next Steps

1. Implement actual game logic in room counters
2. Add player session management
3. Integrate with Go services for room requests
4. Add persistent storage for game state
5. Implement health check endpoint
6. Add monitoring and metrics (Prometheus)
7. Configure autoscaling policies
