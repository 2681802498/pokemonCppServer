# Pokemon C++ 游戏服务器 - 实施指南

## 🎯 项目概览

这是一个完整的 C++ 游戏服务器骨架原型，专门为 Kubernetes 云原生部署设计。包含以下核心功能：

✅ **gRPC 服务接口** - CreateRoom, GetRoomStatus, CloseRoom  
✅ **Kubernetes 集成** - POD_IP, REDIS_URL 环境变量  
✅ **优雅停机** - SIGTERM 信号处理，5秒缓冲期  
✅ **Redis 集成** - 节点注册和房间数据持久化  
✅ **房间管理** - UUID ID，玩家计数模拟，活动计数器  
✅ **内嵌 simulator** - battle_core 直接链接进服务进程，Go 透传 JSON，C++ 返回战斗结果 JSON

## 📁 项目结构

```
pokemonCppServer/
├── proto/                          # gRPC 协议定义
│   └── room_service.proto         # 服务接口定义
├── src/server/                     # 实现代码
│   ├── main.cpp                   # 主程序入口
│   ├── game_server.h/cpp          # 游戏服务器核心
│   ├── room_manager.h/cpp         # 房间管理 (UUID, 计数器)
│   ├── redis_client.h/cpp         # Redis 客户端 (hiredis)
│   └── signal_handler.h/cpp       # SIGTERM 处理
├── CMakeLists.txt                 # CMake 构建配置
├── Dockerfile                      # Docker 镜像构建
├── docker-compose.yml             # 本地开发 Compose
├── k8s-deployment.yaml            # K8S 服务器部署
├── k8s-redis.yaml                 # K8S Redis 部署
├── setup.sh                       # 依赖安装 (自动)
├── build.sh                       # 编译脚本
├── test.sh                        # 测试脚本 (grpcurl)
├── QUICKSTART.md                  # 快速开始指南
└── README.md                       # 详细文档
```

## 🚀 快速开始 (3 种方式)

### 方式 1️⃣：Docker Compose (推荐本地开发)

```bash
cd /Users/why/Documents/goLang/pokemonCppServer

# 一键启动 (自动构建和启动 Redis)
docker-compose up --build

# 在另一个终端测试
chmod +x test.sh
./test.sh
```

### 方式 2️⃣：本地 CMake 编译

```bash
cd /Users/why/Documents/goLang/pokemonCppServer

# 安装依赖 (首次)
chmod +x setup.sh
bash setup.sh

# 构建项目
chmod +x build.sh
bash build.sh

# 启动 Redis (另一个终端)
redis-server

# 运行服务器
cd build
./bin/pokemon_server
```

### 方式 3️⃣：Kubernetes 部署

```bash
# 构建 Docker 镜像
docker build -t pokemon-cpp-server:latest .

# 部署到 K8S
kubectl apply -f k8s-redis.yaml
kubectl apply -f k8s-deployment.yaml

# 验证
kubectl get pods
kubectl get svc pokemon-server-service

# 本地测试 (端口转发)
kubectl port-forward svc/pokemon-server-service 50051:50051
./test.sh
```

## 🔧 核心实现细节

### 0. 前端到 simulator 的调用链

```text
前端 JSON -> Go 服务 -> BattleEngine -> BattleSession -> BattleToJson -> Go 服务 -> 前端 JSON
```

关键点：
- `CreateRoom` 使用 `init_json` 初始化战斗会话
- `SendCommand` 使用 `action` 传回合 JSON
- `DestroyRoom` 同时清理房间和内存中的 battle session
- simulator 的 cache 写文件在嵌入模式下已关闭，避免多会话冲突

### 1. gRPC 服务 (proto/room_service.proto)

```protobuf
service RoomService {
  rpc CreateRoom(CreateRoomRequest) returns (CreateRoomResponse) {}
  rpc GetRoomStatus(GetRoomStatusRequest) returns (GetRoomStatusResponse) {}
  rpc CloseRoom(CloseRoomRequest) returns (CloseRoomResponse) {}
}
```

当前实现中，`CreateRoomRequest.init_json` 和 `GameCommand.action` 都按 JSON 字符串处理，由服务端解析后直接传入 simulator。

**请求示例:**
```bash
grpcurl -plaintext \
  -d '{"room_name": "Arena1", "max_players": 4}' \
  localhost:50051 pokemon.game.RoomService/CreateRoom
```

### 2. Kubernetes 环境变量集成

**src/server/main.cpp:**
```cpp
const char* pod_ip = std::getenv("POD_IP");        // K8S 注入
const char* redis_url = std::getenv("REDIS_URL");  // ConfigMap
```

**启动日志输出:**
```
Server started at [POD_IP], connecting to Redis at [REDIS_URL]
```

### 3. 优雅停机 (SIGTERM 处理)

**信号处理流程:**
1. ✅ 收到 SIGTERM 信号
2. ✅ 设置 `is_maintaining = true`
3. ✅ 拒绝新的 CreateRoom 请求
4. ✅ 每秒打印 "Waiting for rooms to clear..."
5. ✅ 等待 5 秒
6. ✅ 关闭所有房间
7. ✅ 从 Redis 注销
8. ✅ 进程正常退出

**测试优雅停机:**
```bash
# 另一个终端获取 PID
ps aux | grep pokemon_server

# 发送 SIGTERM
kill -TERM <PID>

# 观察日志输出
```

### 4. Redis 集成

**src/server/redis_client.cpp:**

**节点注册:**
```cpp
SADD pokemon:server:nodes <POD_IP>  // 启动时
SREM pokemon:server:nodes <POD_IP>  // 停止时
```

**房间数据:**
```cpp
SET pokemon:room:<id> <json_data> EX 3600
```

**查询已注册节点:**
```bash
redis-cli SMEMBERS pokemon:server:nodes
```

### 5. 房间管理

**src/server/room_manager.cpp:**

- **房间 ID**: 随机 UUID 格式 (跨平台兼容)
- **玩家计数**: 每秒自动增减 (模拟)
- **活动计数器**: 房间运行计数
- **最大房间数**: 每个服务器 10 间
- **维护模式**: `is_maintaining = true` 时拒绝新房间

### 6. 内嵌 BattleEngine

- `BattleEngine` 维护 `session_id -> BattleSession`
- `CreateSession` 负责创建战斗上下文
- `ProcessTurn` 负责回合推进和状态输出
- `GetState` 负责返回当前战斗 JSON
- `DestroySession` 负责释放会话

这层适配器的存在，让 Go 服务只需要处理 JSON 和业务逻辑，不需要理解 simulator 内部对象结构。

## 📊 项目技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| C++ | 17+ | 编程语言 |
| gRPC | 1.80.0 | RPC 通信框架 |
| Protocol Buffers | 34.1 | 接口定义 |
| hiredis | 1.3.0 | Redis C 客户端 |
| CMAKE | 4.3.1+ | 构建系统 |
| Docker | 最新 | 容器化 |
| Kubernetes | 1.20+ | 容器编排 |

## 🎯 核心代码位置

| 功能 | 文件 | 行数 |
|------|------|------|
| 主程序 | [src/server/main.cpp](src/server/main.cpp) | 60+ |
| gRPC 服务实现 | [src/server/game_server.cpp](src/server/game_server.cpp) | 150+ |
| 房间管理 | [src/server/room_manager.cpp](src/server/room_manager.cpp) | 110+ |
| Redis 客户端 | [src/server/redis_client.cpp](src/server/redis_client.cpp) | 140+ |
| SIGTERM 处理 | [src/server/signal_handler.cpp](src/server/signal_handler.cpp) | 40+ |

## 🧪 测试 gRPC 接口

### 安装 grpcurl

```bash
go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest
```

### 创建房间

```bash
grpcurl -plaintext \
  -d '{"room_name": "BattleArena", "max_players": 8}' \
  localhost:50051 \
  pokemon.game.RoomService/CreateRoom
```

**响应:**
```json
{
  "success": true,
  "room_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

### 查询房间状态

```bash
grpcurl -plaintext \
  -d '{"room_id": "550e8400-e29b-41d4-a716-446655440000"}' \
  localhost:50051 \
  pokemon.game.RoomService/GetRoomStatus
```

### 关闭房间

```bash
grpcurl -plaintext \
  -d '{"room_id": "550e8400-e29b-41d4-a716-446655440000"}' \
  localhost:50051 \
  pokemon.game.RoomService/CloseRoom
```

## 🔍 调试和监控

### Docker Compose 日志

```bash
# 查看服务器日志
docker-compose logs -f pokemon-server

# 查看 Redis 日志
docker-compose logs -f redis

# 监控 Redis 命令
docker-compose exec redis redis-cli MONITOR
```

### Kubernetes 日志

```bash
# 查看 Pod 日志
kubectl logs -f deployment/pokemon-server

# 进入 Pod 容器
kubectl exec -it <pod-name> -- /bin/bash

# 查看事件
kubectl get events
```

### Redis 命令行

```bash
# 进入 Redis CLI
redis-cli

# 查看所有未来的命令
MONITOR

# 查看已注册的节点
SMEMBERS pokemon:server:nodes

# 查看房间数据
GET pokemon:room:<room-id>

# 清除所有数据
FLUSHALL
```

## 🛠️ 编译故障排查

### CMake 找不到依赖

```bash
# 清除构建目录重新开始
rm -rf build
mkdir build && cd build

# 使用 verbose 输出
cmake -DCMAKE_VERBOSE_MAKEFILE=ON ..
make VERBOSE=1
```

### 找不到 gRPC 编译器

```bash
# macOS
brew install grpc

# Ubuntu/Debian
sudo apt-get install protobuf-compiler-grpc
```

### hiredis 链接错误

```bash
# macOS
brew reinstall hiredis

# Ubuntu/Debian
sudo apt-get install libhiredis-dev
```

## 📋 部署清单

- [ ] 创建 Docker 镜像: `docker build -t pokemon-cpp-server:latest .`
- [ ] 推送镜像到 Registry: `docker push <registry>/pokemon-cpp-server:latest`
- [ ] 部署 Redis: `kubectl apply -f k8s-redis.yaml`
- [ ] 部署服务器: `kubectl apply -f k8s-deployment.yaml`
- [ ] 验证 Pod 运行: `kubectl get pods`
- [ ] 测试 gRPC 接口: `./test.sh`
- [ ] 验证 Redis 连接: `redis-cli SMEMBERS pokemon:server:nodes`
- [ ] 测试优雅停机: `kubectl delete pod <pod-name>`

## 🎓 学习资源

- [gRPC 文档](https://grpc.io/)
- [Protocol Buffers 指南](https://developers.google.com/protocol-buffers)
- [Kubernetes 文档](https://kubernetes.io/docs/)
- [hiredis 使用指南](https://github.com/redis/hiredis)
- [C++17 特性](https://en.cppreference.com/w/cpp/17)

## 📝 后续改进方向

1. **游戏逻辑**: 用真实游戏状态替换房间计数器
2. **持久化**: 数据库集成 (MySQL/PostgreSQL)
3. **监控**: Prometheus 指标导出
4. **日志**: ELK Stack 日志聚合
5. **认证**: JWT 令牌验证
6. **陈限**: 熔断器和限流
7. **负载测试**: ghz 工具测试吞吐量
8. **CI/CD**: GitHub Actions 自动构建

## 📞 支持

遇到问题? 检查以下文件:
- [README.md](README.md) - 完整功能文档
- [QUICKSTART.md](QUICKSTART.md) - 快速开始指南
- [Dockerfile](Dockerfile) - 容器构建
- [CMakeLists.txt](CMakeLists.txt) - 构建配置

---

**提示**: 所有 C++ 实现代码都在 `src/server/` 文件夹中，便于后续整体替换或集成到现有项目。
