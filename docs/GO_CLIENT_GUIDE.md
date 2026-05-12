# Go 客户端连接指南

## 服务器信息
- gRPC 地址：`127.0.0.1:50051`
- 协议：`plaintext`（未启用 TLS）
- Proto 包名：`calc`
- 服务名：`Calculator`
- Go 输出包：`./api/calc`

## 当前 RPC

### `CreateRoom`
请求：
- `room_id: string`
- `init_json: string`

响应：
- `code: int32`
- `message: string`

### `SendCommand`
请求：
- `room_id: string`
- `player_id: string`
- `action: string`

响应：
- `code: int32`
- `message: string`

### `DestroyRoom`
请求：
- `room_id: string`

响应：
- `code: int32`
- `message: string`

### `GetHeartbeat`
请求：
- 空请求

响应：
- `code: int32`
- `active_rooms: int32`
- `cpu_usage: float`
- `memory_used: uint64`
- `max_capacity: int32`

## Go 侧生成代码

建议在 Go 项目中执行：

```bash
protoc --go_out=. --go-grpc_out=. proto/room_service.proto
```

生成后可通过 `calc` 包引用 `CalculatorClient`。

## 连接示例

```go
conn, err := grpc.Dial("127.0.0.1:50051", grpc.WithTransportCredentials(insecure.NewCredentials()))
if err != nil {
    panic(err)
}
defer conn.Close()

client := calc.NewCalculatorClient(conn)
```

## 测试说明

本机已验证：
- `GetHeartbeat` 可正常返回
- Redis 依赖需要可用，当前服务器连接 `127.0.0.1:6379`

## 备注

当前服务器还会：
- 启动时创建 2 个测试房间
- 保持房间活动更新线程运行
- `GetHeartbeat` 的 `cpu_usage` 和 `memory_used` 目前为占位值
