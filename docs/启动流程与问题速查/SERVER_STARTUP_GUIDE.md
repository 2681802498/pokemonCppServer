# Pokemon C++ Server 启动教程

## 概述

本项目是一个基于 gRPC 的 Pokemon 服务器，使用 C++ 开发，支持 Kubernetes 自动化部署和水平自动扩缩容 (HPA)。

## 系统要求

- **Docker**: 已安装并运行
- **Kubernetes**: 至少 v1.20+ (推荐 Kind 或 Docker Desktop)
- **工具链**:
  - CMake 3.10+
  - g++ 或 clang
  - protobuf 编译器
  - grpcurl (用于测试)

## 本地构建和测试

### 1. 编译项目

```bash
# 清理之前的构建
rm -rf build
mkdir build
cd build

# 执行 CMake 配置和编译
cmake ..
make -j$(nproc)
```

编译完成后，可执行文件位于 `build/bin/pokemon_server`

### 2. 本地运行

```bash
# 设置 Redis 连接（如果没有本地 Redis，需要先启动）
export REDIS_URL=localhost:6379

# 运行服务器
./build/bin/pokemon_server
```

服务器默认监听：
- **gRPC**: `0.0.0.0:50051`
- **Metrics (Prometheus)**: `0.0.0.0:9102`

### 3. 测试 gRPC 接口

```bash
# 使用 grpcurl 测试服务可用性
grpcurl -plaintext localhost:50051 list

# 调用 RoomService 的 GetRooms 方法
grpcurl -plaintext localhost:50051 \
  -d '{"limit":10}' \
  pokemon.RoomService/GetRooms
```

## Kubernetes 部署

### 前置条件

1. **建立 Docker 镜像**

```bash
# 构建镜像
docker build -t why5899/pokemon-worker:v0.8.3 .

# 推送到仓库
docker push why5899/pokemon-worker:v0.8.3
```

2. **部署 Redis**

如果集群中还没有 Redis，先部署：

```bash
kubectl apply -f k8s-redis.yaml
```

3. **部署 Prometheus + Prometheus Adapter**

自动扩缩容需要 Prometheus 和自定义指标 API：

```bash
# 使用 Helm 部署 Prometheus Stack
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm install monitoring prometheus-community/kube-prometheus-stack \
  -n monitoring --create-namespace

# 部署 Prometheus Adapter (见下文配置部分)
```

### 部署步骤

#### 步骤 1: 创建 ConfigMap 和 StatefulSet

```bash
# 部署主应用（包含 ConfigMap、StatefulSet、Service、HPA）
kubectl apply -f k8s-deployment.yaml
```

#### 步骤 2: 验证部署

```bash
# 查看 Pod 状态
kubectl get pods -o wide

# 查看 StatefulSet
kubectl get statefulsets

# 查看 HPA 状态
kubectl get hpa

# 检查应用日志
kubectl logs -f pokemon-server-0
kubectl logs -f pokemon-server-1
```

#### 步骤 3: 测试服务

**方式 A: 通过端口转发**

```bash
# 转发 gRPC 端口
kubectl port-forward pod/pokemon-server-0 9103:50051

# 在另一个终端测试
grpcurl -plaintext localhost:9103 list
```

**方式 B: 通过 Service** (集群内)

```bash
# 查看 Service
kubectl get svc

# 使用 Service DNS 名称: pokemon-server-headless:50051
```

**方式 C: 监控 Metrics**

```bash
# 转发 Prometheus 端口
kubectl port-forward -n monitoring pod/prometheus-monitoring-kube-prometheus-prometheus-0 9090:9090

# 访问 http://localhost:9090 查看指标
# 搜索 "pokemon_" 相关指标
```

### 配置文件说明

#### k8s-deployment.yaml

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: pokemon-server-config
  # Redis 连接地址 (必须指向可访问的 Redis 服务)
  redis_url: redis-service:6379

---
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: pokemon-server
spec:
  replicas: 2                          # 初始副本数
  serviceName: pokemon-server-headless # 关联的 Headless Service
  template:
    spec:
      containers:
      - name: pokemon-server
        image: why5899/pokemon-worker:v0.8.3
        imagePullPolicy: Always
        resources:
          requests:
            cpu: "100m"               # 最小 CPU
            memory: "128Mi"           # 最小内存
          limits:
            cpu: "500m"               # 最大 CPU
            memory: "256Mi"           # 最大内存

---
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: pokemon-server-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: StatefulSet
    name: pokemon-server
  minReplicas: 2                        # 最少 Pod 数
  maxReplicas: 10                       # 最多 Pod 数
  metrics:
  - type: Pods
    pods:
      metric:
        name: pokemon_room_utilization  # 自定义指标名
      target:
        type: AverageValue
        averageValue: 500m              # 平均利用率目标
```

## 自定义指标配置

### Prometheus Adapter ConfigMap

自动扩缩容依赖于 Prometheus Adapter 将 Prometheus 指标暴露为 Kubernetes Custom Metrics API。

**正确的配置格式** (`custom-metrics-config` ConfigMap):

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: custom-metrics-config
  namespace: monitoring
data:
  config.yaml: |
    rules:
    - seriesQuery: 'pokemon_room_utilization_global'
      resources:
        overrides:
          namespace: {resource: "namespace"}
          pod: {resource: "pod"}
      name:
        matches: "pokemon_room_utilization_global"
        as: "pokemon_room_utilization_global"
      metricsQuery: 'avg(pokemon_room_utilization_global{<<.LabelMatchers>>}) by (<<.GroupBy>>)'
    - seriesQuery: 'pokemon_room_utilization_peak'
      resources:
        overrides:
          namespace: {resource: "namespace"}
          pod: {resource: "pod"}
      name:
        matches: "pokemon_room_utilization_peak"
        as: "pokemon_room_utilization_peak"
      metricsQuery: 'max(pokemon_room_utilization_peak{<<.LabelMatchers>>}) by (<<.GroupBy>>)'
```

**关键点**：
- `rules:` 后面直接跟数组（`-`），不能有 `custom:` 或 `default: false` 的嵌套结构
- `seriesQuery` 指定 Prometheus 中的指标名称
- `metricsQuery` 定义如何聚合指标（此例使用平均值）
- `<<.LabelMatchers>>` 和 `<<.GroupBy>>` 是 Prometheus Adapter 的模板变量

### 应用程序如何暴露指标

应用在 `9102` 端口暴露 Prometheus 格式的指标：

```
# HELP pokemon_room_utilization Local room utilization metric
# TYPE pokemon_room_utilization gauge
pokemon_room_utilization 0.25
# HELP pokemon_room_utilization_global Global room utilization across registered pods
# TYPE pokemon_room_utilization_global gauge
pokemon_room_utilization_global 0.35
# HELP pokemon_room_utilization_peak Peak room utilization across pods
# TYPE pokemon_room_utilization_peak gauge
pokemon_room_utilization_peak 0.90
```

## 监控和调试

### 查看 HPA 状态

```bash
# 实时监控 HPA
watch kubectl get hpa

# 详细信息
kubectl describe hpa pokemon-server-hpa

# 查看 HPA 事件
kubectl get events --sort-by='.lastTimestamp'
```

### 查看指标可用性

```bash
# 检查 Custom Metrics API 是否已注册
kubectl get apiservices | grep custom

# 查询具体指标
kubectl get --raw \
  "/apis/custom.metrics.k8s.io/v1beta1/namespaces/default/pods/*/pokemon_room_utilization_global"
```

### 查看 Pod 日志

```bash
# 查看单个 Pod 日志
kubectl logs -f pokemon-server-0

# 查看多个 Pod 日志
kubectl logs -f -l app=pokemon-server --all-containers=true

# 查看前面的日志
kubectl logs --previous pokemon-server-0
```

## 故障排除快速参考

| 症状 | 原因 | 解决方案 |
|------|------|--------|
| Pod 无法启动 | Redis 连接失败 | 检查 `REDIS_URL` ConfigMap，确保 Redis 服务运行 |
| HPA 不工作 | Prometheus Adapter 未启动 | 检查 Adapter ConfigMap 格式，重启 Deployment |
| 指标显示 `<unknown>` | Prometheus 中没有指标数据 | 确保应用正确暴露指标在 `:9102/metrics` |
| Pod 频繁重启 | Readiness Probe 失败 | 检查 gRPC 端口 `50051` 是否正常监听 |

更多详细信息，请参考 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)。
