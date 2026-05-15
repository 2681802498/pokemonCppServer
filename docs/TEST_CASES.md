# 测试用例：快速验证服务（示例）

本文件提供一组可复制的命令，用于本地验证当前内嵌 simulator 的服务端行为。

前提
- 已在机器上安装 `cmake`、`make`、`redis-server`、`grpcurl`。
- 已构建可执行文件 `build/bin/pokemon_server`（或者使用 Docker 启动镜像）。

1) 启动 Redis（若本地没有 Redis）

```bash
# 在本地直接启动（或用 docker-compose 启动）
redis-server --save "" --appendonly no &
```

2) 构建并运行服务（本机调试）

```bash
mkdir -p build && cd build
cmake ..
make -j4
./bin/pokemon_server &
```

3) 检查指标页面

```bash
# metrics 默认暴露在 9102 端口
curl http://localhost:9102/metrics | head -n 40
```

4) 使用 `grpcurl` 测试 RPC

说明：当前 `CreateRoom` 的 `init_json` 字段需要是一个 JSON 字符串，且内部必须包含 `side_a` 与 `side_b`，每侧 `pokemon` 数组至少含一只宝可梦。

示例：先准备一个简单的初始化字符串（注意内层引号要转义）

```bash
INIT_JSON='{"side_a":{"name":"Side A","pokemon":[{"species":"Pikachu","level":50,"moves":["Thunderbolt"]}]},"side_b":{"name":"Side B","pokemon":[{"species":"Bulbasaur","level":50,"moves":["VineWhip"]}]}}'

# 创建房间
grpcurl -plaintext -d '{"room_id":"test-room-1","init_json":"'"$INIT_JSON"'"}' localhost:50051 calc.Calculator/CreateRoom

# 如果创建成功，服务会在 Redis 写入初始战斗快照（可选）并返回 code/message

# 发送回合动作（action 为回合 JSON 字符串，示例只给一方动作）
ACTION_JSON='{"actions":[{"side":"a","type":"move","move_name":"Thunderbolt"}]}'
grpcurl -plaintext -d '{"room_id":"test-room-1","player_id":"p1","action":"'"$ACTION_JSON"'"}' localhost:50051 calc.Calculator/SendCommand

# 查询心跳
grpcurl -plaintext -d '{}' localhost:50051 calc.Calculator/GetHeartbeat

# 销毁房间
grpcurl -plaintext -d '{"room_id":"test-room-1"}' localhost:50051 calc.Calculator/DestroyRoom
```

注意事项
- `INIT_JSON` 和 `ACTION_JSON` 示例做了最小化演示；实际要成功初始化对战，`pokemon` 对象可能需要更完整的字段（参见 `src/simulator/data` 或 `docs` 中的示例数据）。
- 如果 `CreateRoom` 返回表示初始化失败（code != 0），请检查 `pokemon` 数据格式是否满足 `BuildFromJson` 的要求。
- 嵌入模式下 simulator 已禁用全局 `cache` 写文件（避免多会话冲突），但仍会生成内存 JSON 返回用于调试。

如果你要，我可以把 `INIT_JSON` 的完整可运行样例（使用仓库内的 `data/` 文件构造）补上为文件形式，便于直接 `grpcurl -d @file.json` 调用。
