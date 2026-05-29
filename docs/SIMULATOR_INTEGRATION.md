# Simulator 集成说明

## 当前集成方式

当前项目采用的是内嵌式 simulator 集成：

- Go 服务接收前端 JSON
- Go 服务调用 C++ `BattleEngine`
- `BattleEngine` 在同一进程内管理 `BattleSession`
- simulator 直接返回战斗结果 JSON
- Go 服务再把 JSON 返回给前端

## 调用链

```text
前端 JSON -> Go 服务 -> BattleEngine -> BattleSession -> BattleToJson -> Go 服务 -> 前端 JSON
```

## 关键类

### `BattleEngine`
负责会话生命周期管理。

- `CreateSession(session_id, init_json)`
- `ProcessTurn(session_id, turn_json)`
- `GetState(session_id)`
- `DestroySession(session_id)`

### `BattleSession`
simulator 的核心战斗会话对象。

- `createFromJson(...)`
- `doInitialSendOut()`
- `processTurn(...)`

## 设计原因

选择内嵌方式的原因：

1. 延迟低，避免网络往返
2. 调试简单，便于直接跟踪 C++ 对象状态
3. 接入成本小，适合当前 skeleton 项目
4. 更容易和现有 `CreateRoom` / `SendCommand` 流程对接

## 注意事项

- simulator 的 cache 文件写入在嵌入模式下已关闭
- 每个 session 必须独立管理，不能共用全局 cache
- `action` 和 `init_json` 都按 JSON 字符串处理
- 如果后续要独立扩容 simulator，可以再把这层适配器迁移成 gRPC 服务

## 建议的 JSON 结构

### init_json
```json
{
  "side_a": {
    "name": "Side A",
    "pokemon": []
  },
  "side_b": {
    "name": "Side B",
    "pokemon": []
  }
}
```

### action
```json
{
  "actions": [
    {
      "side": "a",
      "type": "move",
      "move_index": 0
    },
    {
      "side": "b",
      "type": "pass"
    }
  ]
}
```

## 后续演进

如果后续需要把 simulator 单独部署，可以将 `BattleEngine` 的实现替换为 gRPC client，Go 服务侧接口保持不变。