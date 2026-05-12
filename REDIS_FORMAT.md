# Redis 数据格式文档

## 概述

C++ 服务将房间快照存入 Redis，Go 服务通过 Redis 读取房间信息进行管理。本文档描述 Redis 中存储的完整数据格式。

---

## 房间快照 (Room Snapshot)

### Redis 键

```
pokemon:room:{room_id}
```

**示例**：
```
pokemon:room:959784
```

### 值格式 (JSON)

```json
{
  "node_id": 0,
  "players": [],
  "ready_players": {},
  "room_id": "959784",
  "selected_pokemon": {},
  "server_id": "server-1778480087-12345",
  "status": 1,
  "updated_at": 1778480087
}
```

### 字段说明

| 字段 | 类型 | 必需 | 说明 | 示例 |
|------|------|------|------|------|
| `room_id` | string | ✅ | 房间唯一标识符 | `"959784"` |
| `status` | int | ✅ | 房间状态（1=active） | `1` |
| `node_id` | int | ✅ | 节点 ID（C++ 初始化为 0，由 Go 侧后续按运行节点填充） | `0` |
| `server_id` | string | ✅ | 创建房间的服务实例标识符（随机值，用于区分节点实例） | `"server-1778480087-12345"` |
| `players` | array | ✅ | 房间内的玩家列表 | `[]` |
| `ready_players` | object | ✅ | 准备就绪的玩家映射 | `{}` |
| `selected_pokemon` | object | ✅ | 玩家选择的宝可梦映射 | `{}` |
| `updated_at` | int64 | ✅ | 最后更新时间（Unix 时间戳，秒） | `1778480087` |

---

## Redis 操作示例

### C++ 写入 (SET)

```basherver_id":"server-1778480087-12345","s
SET pokemon:room:959784 '{"node_id":0,"players":[],"ready_players":{},"room_id":"959784","selected_pokemon":{},"status":1,"updated_at":1778480087}' EX 3600
```

**说明**：
- `EX 3600`：键的 TTL 为 3600 秒（1 小时），过期后自动删除
- 值是紧凑的 JSON 字符串（无空格缩进）

### Go 读取 (GET)

```bash
GET pokemon:room:959784
```

**返回值**：
```json
{"node_id":0,"players":[],"ready_players":{},"room_id":"959784","selected_pokemon":{},"server_id":"server-1778480087-12345","status":1,"updated_at":1778480087}
```

### 查询所有房间

```bash
# 方案 1：KEYS（开发调试用，生产环境避免）
KEYS pokemon:room:*

# 方案 2：SCAN（推荐生产环境）
SCAN 0 MATCH 'pokemon:room:*' COUNT 100
```

### 删除房间

```bash
DEL pokemon:room:959784
```

---

## 其他 Redis 键

### 节点注册集合

**键**：`pokemon:server:nodes`  
**类型**：Set  
**用途**：存储所有活跃的 C++ 服务节点 IP

**操作**：
```bash
# 服务启动时注册
SADD pokemon:server:nodes 10.244.0.95

# 查看所有节点
SMEMBERS pokemon:server:nodes

# 服务停止时反注册
SREM pokemon:server:nodes 10.244.0.95
```

### 房间命令缓存

**键**：`pokemon:room:{room_id}:command`  
**类型**：String (JSON)  
**用途**：存储最近一次发送给房间的命令

**值格式**：
```json
{
  "player_id": "player_123",
  "action": "battle_action",
  "timestamp": 1778480087
}
```

**操作**：
```bash
SET pokemon:room:959784:command '{"player_id":"player_123","action":"battle_action","timestamp":1778480087}' EX 3600
```

---

## Go 服务实现指南

### 从 Redis 读取房间列表

```go
// 伪代码示例
ctx := context.Background()
client := redis.NewClient(&redis.Options{Addr: "localhost:6379"})

// 方案 1：KEYS（简单但可能阻塞）
keys, err := client.Keys(ctx, "pokemon:room:*").Result()

// 方案 2：SCAN（推荐）
var cursor uint64
var keys []string
for {
    keys, cursor, err = client.Scan(ctx, cursor, "pokemon:room:*", 100).Result()
    if cursor == 0 {
        break
    }
}

// 读取每个房间的数据
for _, key := range keys {
    val, err := client.Get(ctx, key).Result()
    var room RoomSnapshot
    json.Unmarshal([]byte(val), &room)
    // 处理房间数据
}
```

### Go Struct 定义

```go
type RoomSnapshot struct {
    RoomID           string                 `json:"room_id"`
    Status           int                    `json:"status"`
    NodeID           int                    `json:"node_id"`
    ServerID         string                 `json:"server_id"`
    Players          []interface{}          `json:"players"`
    ReadyPlayers     map[string]interface{} `json:"ready_players"`
    SelectedPokemon  map[string]interface{} `json:"selected_pokemon"`
    UpdatedAt        int64                  `json:"updated_at"`
}
```

---

## 数据流时序

```
1. C++ 服务创建房间
   └─> CreateRoom RPC 请求
      └─> 在内存中创建房间
      └─> 构建房间快照 JSON
      └─> Redis SET pokemon:room:{id} <snapshot> EX 3600
      └─> 返回 CreateRoom 成功响应

2. Go 服务读取房间数据
   └─> Get Redis 请求
      └─> SCAN/KEYS pokemon:room:*
      └─> GET pokemon:room:{id}（批量）
      └─> 解析 JSON，恢复房间状态
      └─> 返回房间列表给客户端

3. 房间销毁
   └─> DestroyRoom RPC 请求
      └─> 在内存中删除房间
      └─> Redis DEL pokemon:room:{id}

4. TTL 过期（3600 秒后）
   └─> Redis 自动清理过期键
```

---

## 注意事项

- **TTL 管理**：快照键的 TTL 为 3600 秒，Go 服务应定期检查 Redis 中的房间数据是否仍有效
- **JSON 格式**：所有 JSON 值都是紧凑格式（无空格），易于 Redis 存储和传输
- **占位符字段**：C++ 初始化的 `node_id=0` 和空的 `players`、`ready_players`、`selected_pokemon` 是占位符，由 Go 服务根据实际情况填充
- **HPA 判断**：当前扩容依据为 Redis 中活跃房间总数 ÷（活跃 pod 数 × 单 pod 最大房间数）
- **实例标识**：`server_id` 用于区分服务实例，不直接作为扩缩容依据；扩缩容以 Redis 汇总值为准
- **时间戳**：`updated_at` 使用 Unix 时间戳（秒），精度为秒级
- **并发安全**：Redis 原生支持并发读写，但 Go 服务需要处理竞态条件（如同时修改房间状态）

