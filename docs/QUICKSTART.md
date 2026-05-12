# Quick Start Guide - Pokemon C++ Game Server

## Overview
This is a production-ready skeleton for a C++ game server running in Kubernetes with the following architecture:
- **gRPC** for service communication
- **Redis** for node registration and room data persistence
- **Kubernetes** for orchestration and service discovery
- **C++17** for modern, performant code

## Directory Structure

```
pokemonCppServer/
├── proto/                           # gRPC service definitions
│   └── room_service.proto          # Room service interface
├── src/server/                      # Implementation code
│   ├── main.cpp                    # Entry point
│   ├── game_server.{h,cpp}         # Main server class
│   ├── room_manager.{h,cpp}        # Room management logic
│   ├── redis_client.{h,cpp}        # Redis integration
│   └── signal_handler.{h,cpp}      # SIGTERM handling
├── build/                           # Build artifacts (git-ignored)
├── CMakeLists.txt                  # Build configuration
├── Dockerfile                       # Container image
├── docker-compose.yml              # Local testing
├── k8s-deployment.yaml             # Kubernetes server
├── k8s-redis.yaml                  # Kubernetes Redis
├── setup.sh                        # Dependency installation
├── build.sh                        # Build script
└── test.sh                         # Test script
```

## Local Development (macOS)

### 1. Install Dependencies
```bash
chmod +x setup.sh
./setup.sh
```

### 2. Build Project
```bash
chmod +x build.sh
./build.sh
```

### 3. Run with Docker Compose (Recommended)
```bash
docker-compose up --build
```

### 4. Test in Another Terminal
```bash
# Install grpcurl if not present
go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest

# Run tests
chmod +x test.sh
./test.sh
```

## Environment Variables

- `POD_IP`: Server's Pod IP (automatically set by Kubernetes)
- `REDIS_URL`: Redis endpoint (format: `host:port`)

## Kubernetes Deployment

### 1. Build Docker Image
```bash
docker build -t pokemon-cpp-server:latest .
# If using minikube: eval $(minikube docker-env) before building
```

### 2. Deploy to Kubernetes
```bash
# Deploy Redis
kubectl apply -f k8s-redis.yaml

# Deploy game servers
kubectl apply -f k8s-deployment.yaml

# Check status
kubectl get pods
kubectl get svc

# Port forward for testing
kubectl port-forward svc/pokemon-server-service 50051:50051
```

## Core Features

### 1. gRPC Services
- `CreateRoom`: Create new game rooms
- `GetRoomStatus`: Query room information
- `CloseRoom`: Terminate rooms

### 2. Kubernetes Integration
Automatic discovery and configuration:
```cpp
POD_IP=$(env POD_IP)        // Auto-injected by Kubernetes
REDIS_URL=$(env REDIS_URL)  // From ConfigMap
```

### 3. Graceful Shutdown (SIGTERM)
When receiving SIGTERM signal:
1. Reject new room creation requests
2. Print "Waiting for rooms to clear..." every second
3. Wait 5 seconds total
4. Close all active rooms
5. Unregister from Redis
6. Exit cleanly

### 4. Room Management
- Rooms identified by UUID
- Player count simulation (increments/decrements)
- Activity counters for testing
- Max 10 rooms per server

### 5. Redis Integration
- Server registration: `SADD pokemon:server:nodes <POD_IP>`
- Room storage: `SET pokemon:room:<ID> <RoomSnapshot JSON> EX 3600`
- RoomSnapshot fields:
  - `room_id`: string
  - `status`: int
  - `node_id`: int
  - `players`: []string
  - `ready_players`: map[string]bool
  - `selected_pokemon`: map[string][]map[string]interface{}
  - `updated_at`: int64
- Service discovery via Redis Set

## API Examples

### Create Room
```bash
grpcurl -plaintext \
  -d '{"room_name": "MainArena", "max_players": 10}' \
  localhost:50051 pokemon.game.RoomService/CreateRoom
```

Response:
```json
{
  "success": true,
  "room_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

### Get Room Status
```bash
grpcurl -plaintext \
  -d '{"room_id": "550e8400-e29b-41d4-a716-446655440000"}' \
  localhost:50051 pokemon.game.RoomService/GetRoomStatus
```

Response:
```json
{
  "success": true,
  "room": {
    "room_id": "550e8400-e29b-41d4-a716-446655440000",
    "room_name": "MainArena",
    "player_count": 3,
    "max_players": 10,
    "is_active": true
  }
}
```

### Close Room
```bash
grpcurl -plaintext \
  -d '{"room_id": "550e8400-e29b-41d4-a716-446655440000"}' \
  localhost:50051 pokemon.game.RoomService/CloseRoom
```

## Development Workflow

### Using Docker Compose
```bash
# Start services
docker-compose up

# In another terminal, test
./test.sh

# View logs
docker-compose logs pokemon-server
docker-compose logs redis

# Stop
docker-compose down
```

### Local CMake Build
```bash
mkdir build && cd build
cmake ..
make -j4

# Start Redis in another terminal
redis-server

# Run server
./bin/pokemon_server
```

## Debugging

### Check Server Logs
```bash
# Docker Compose
docker-compose logs -f pokemon-server

# Kubernetes
kubectl logs -f deployment/pokemon-server
```

### Check Redis Data
```bash
# Get all registered servers
redis-cli SMEMBERS pokemon:server:nodes

# Get room data
redis-cli GET pokemon:room:<room_id>

# Monitor Redis
redis-cli MONITOR
```

### Graceful Shutdown Test
```bash
# Get PID
ps aux | grep pokemon_server

# Send SIGTERM
kill -TERM <PID>

# Observe 5-second graceful shutdown with logs
```

## Performance Considerations

1. **Thread Safety**: All room operations use std::mutex
2. **Async Signal Handling**: Uses std::atomic for signal flags
3. **Memory Management**: Smart pointers for automatic cleanup
4. **Kubernetes Native**: Respects terminationGracePeriodSeconds
5. **Scalability**: Horizontal scaling via multiple Pod replicas

## Next Steps

1. **Implement Game Logic**: Replace room counter simulation with real game state
2. **Add Persistence**: Implement more sophisticated Redis caching
3. **Monitoring**: Add Prometheus metrics
4. **Health Checks**: Extend liveness/readiness probes
5. **Load Testing**: Use tools like ghz for gRPC load testing
6. **CI/CD**: Set up automated builds with GitHub Actions

## Troubleshooting

### Build Fails
```bash
# Clean build
rm -rf build && mkdir build && cd build && cmake .. && make
```

### Docker Build Issues
```bash
# Clear Docker cache
docker system prune -a

# Rebuild with no cache
docker build --no-cache -t pokemon-cpp-server:latest .
```

### Kubernetes Pod Not Starting
```bash
# Check pod logs
kubectl describe pod <pod-name>
kubectl logs <pod-name>

# Check events
kubectl get events
```

### Redis Connection Failed
```bash
# Verify Redis service is running
kubectl get svc redis-service
kubectl logs <redis-pod-name>

# Test connection from pod
kubectl exec -it <server-pod> -- redis-cli -h redis-service ping
```
