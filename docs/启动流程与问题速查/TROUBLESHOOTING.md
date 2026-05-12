# Pokemon C++ Server 问题速查表

## 快速诊断清单

在报告问题前，按以下步骤诊断：

```bash
# 1. 检查基本状态
kubectl get pods -o wide
kubectl get svc
kubectl get hpa

# 2. 查看事件日志
kubectl get events --sort-by='.lastTimestamp' | tail -30

# 3. 检查 Pod 日志
kubectl logs -f pokemon-server-0

# 4. 检查应用配置
kubectl get configmap pokemon-server-config -o yaml
```

---

## 常见问题及解决方案

### 一、Pod 启动失败

#### 问题 1.1: Pod 状态为 `CrashLoopBackOff`

**症状**: Pod 反复重启，日志显示应用立即退出

```
NAME               READY   STATUS             RESTARTS   
pokemon-server-0   0/1     CrashLoopBackOff   5 (30s ago)
```

**可能原因** | **诊断命令** | **解决方案**
---|---|---
Redis 连接失败 | `kubectl logs pokemon-server-0 \| grep -i redis` | 检查 Redis 服务是否运行：`kubectl get pods \| grep redis`
配置文件缺失 | `kubectl get configmap` | 确认 `pokemon-server-config` ConfigMap 存在
端口被占用 | `kubectl describe pod pokemon-server-0` | 检查节点资源是否充足

**具体检查**:

```bash
# 查看详细错误日志
kubectl logs pokemon-server-0 --previous

# 检查 ConfigMap
kubectl get configmap pokemon-server-config -o yaml

# 验证 Redis 连接
kubectl run -it --rm debug --image=redis:latest --restart=Never -- \
  redis-cli -h redis-service -p 6379 ping
```

---

#### 问题 1.2: Pod 状态为 `Pending`

**症状**: Pod 一直处于 `Pending` 状态，无法被调度

```
NAME               READY   STATUS    
pokemon-server-0   0/1     Pending
```

**可能原因** | **诊断命令**
---|---
集群资源不足 | `kubectl describe nodes` 查看可用资源
PVC 无法绑定 | `kubectl get pvc`
节点标签不匹配 | `kubectl describe pod pokemon-server-0` 查看 `Events`

**解决方案**:

```bash
# 查看 Pod 为什么没有被调度
kubectl describe pod pokemon-server-0

# 检查节点资源
kubectl top nodes

# 查看节点详情
kubectl describe node <node-name>

# 如需临时跳过资源限制，修改 resources 部分
kubectl set resources statefulset pokemon-server \
  --requests=cpu=50m,memory=64Mi \
  --limits=cpu=200m,memory=128Mi
```

---

#### 问题 1.3: Pod 状态为 `ImagePullBackOff`

**症状**: Pod 无法拉取镜像

```
NAME               READY   STATUS              
pokemon-server-0   0/1     ImagePullBackOff
```

**可能原因** | **诊断命令** | **解决方案**
---|---|---
镜像不存在 | `docker image ls \| grep pokemon` | 重新构建并推送镜像
镜像标签错误 | 检查 `k8s-deployment.yaml` 中的 `image` 字段 | 更新正确的镜像标签
无权限访问私有仓库 | `kubectl get secret` | 配置 ImagePullSecret

**具体步骤**:

```bash
# 检查名称空间中的镜像拉取密钥
kubectl get secrets

# 如果使用私有仓库，创建密钥
kubectl create secret docker-registry regcred \
  --docker-server=<registry> \
  --docker-username=<user> \
  --docker-password=<password>

# 在 StatefulSet spec.imagePullSecrets 中添加
imagePullSecrets:
- name: regcred

# 验证镜像是否存在
docker pull why5899/pokemon-worker:v0.8.3
```

---

### 二、Readiness/Liveness Probe 失败

#### 问题 2.1: 应用启动慢，Probe 超时

**症状**: Pod 日志显示应用正常运行，但仍处于 `NotReady`

**原因**: 应用启动时间长，超过了 Probe 初始延迟

**解决方案**:

```bash
# 增加初始延迟时间（修改 k8s-deployment.yaml）
livenessProbe:
  tcpSocket:
    port: 50051
  initialDelaySeconds: 30    # 改为 30（默认 15）
  periodSeconds: 10
  
readinessProbe:
  tcpSocket:
    port: 50051
  initialDelaySeconds: 15    # 改为 15（默认 5）
  periodSeconds: 5

# 应用修改
kubectl apply -f k8s-deployment.yaml
```

---

#### 问题 2.2: gRPC 端口不响应

**症状**: Probe 日志显示 `connection refused` on port 50051

**原因**: 
- 应用在错误的端口监听
- gRPC 服务未正确启动
- 防火墙阻止

**诊断**:

```bash
# 进入 Pod 检查端口
kubectl exec -it pokemon-server-0 -- sh

# 在 Pod 内检查监听的端口
ss -tlnp | grep -E "50051|9102"

# 尝试连接到 gRPC 端口
nc -zv localhost 50051

# 检查应用日志中的启动信息
kubectl logs pokemon-server-0 | grep -i "listening\|listen\|port"
```

**解决方案**:

```bash
# 确保应用确实监听了 50051 和 9102
# 检查源代码中的监听地址配置
grep -r "50051\|9102" src/

# 如果应用代码正确，可能需要重建镜像
docker build -t why5899/pokemon-worker:v0.8.4 .
docker push why5899/pokemon-worker:v0.8.4

# 更新 StatefulSet 镜像
kubectl set image statefulset/pokemon-server \
  pokemon-server=why5899/pokemon-worker:v0.8.4

# 滚动更新
kubectl rollout status statefulset/pokemon-server
```

---

### 三、HPA (自动扩缩容) 问题

#### 问题 3.1: HPA ScalingActive 为 False - 无法获取指标

**症状**: 

```bash
$ kubectl describe hpa pokemon-server-hpa

ScalingActive   False   FailedGetPodsMetric  
unable to get metric pokemon_room_utilization: 
unable to fetch metrics from custom metrics API: 
no known available metric versions found
```

**根本原因**: Prometheus Adapter 未正确启动，Custom Metrics API 不可用

**诊断**:

```bash
# 1. 检查 Prometheus Adapter Deployment 状态
kubectl get deployment -n monitoring | grep prometheus-adapter
# 预期结果: 1/1 Running

# 2. 查看 Adapter Pod 日志
kubectl logs -n monitoring deployment/prometheus-adapter --tail=50

# 3. 检查 ConfigMap 配置格式
kubectl get configmap custom-metrics-config -n monitoring -o yaml
```

**解决方案**:

**核心问题**: ConfigMap 中的 YAML 格式错误

❌ **错误格式** (会导致问题):
```yaml
data:
  config.yaml: |
    rules:
      default: false
      custom:
      - seriesQuery: 'pokemon_room_utilization'
        ...
```

✅ **正确格式**（当前实现为“全局利用率 + 峰值兜底”）:
```yaml
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

**修复步骤**:

```bash
# 1. 编辑或重新应用 ConfigMap
kubectl patch configmap custom-metrics-config -n monitoring --type merge -p '{"data":{"config.yaml":"rules:\n- seriesQuery: '\''pokemon_room_utilization_global'\''\n  resources:\n    overrides:\n      namespace: {resource: \"namespace\"}\n      pod: {resource: \"pod\"}\n  name:\n    matches: \"pokemon_room_utilization_global\"\n    as: \"pokemon_room_utilization_global\"\n  metricsQuery: '\''avg(pokemon_room_utilization_global{<<.LabelMatchers>>}) by (<<.GroupBy>>)\''\n- seriesQuery: '\''pokemon_room_utilization_peak'\''\n  resources:\n    overrides:\n      namespace: {resource: \"namespace\"}\n      pod: {resource: \"pod\"}\n  name:\n    matches: \"pokemon_room_utilization_peak\"\n    as: \"pokemon_room_utilization_peak\"\n  metricsQuery: '\''max(pokemon_room_utilization_peak{<<.LabelMatchers>>}) by (<<.GroupBy>>)\''\n"}}'

# 2. 重启 Prometheus Adapter
kubectl rollout restart deployment/prometheus-adapter -n monitoring

# 3. 等待 Pod 启动
kubectl wait --for=condition=ready pod -l app.kubernetes.io/name=prometheus-adapter -n monitoring --timeout=300s

# 4. 验证 HPA 恢复
kubectl describe hpa pokemon-server-hpa

# 预期结果: ScalingActive   True   SucceededGetPodsMetric

# 现在 HPA 依赖两个指标：
# - pokemon_room_utilization_global: 全局利用率
# - pokemon_room_utilization_peak: 单 Pod 峰值兜底
```

---

#### 问题 3.2: 指标显示 `<unknown>`

**症状**:

```bash
$ kubectl describe hpa pokemon-server-hpa

Metrics:                               ( current / target )
  "pokemon_room_utilization" on pods:  <unknown> / 500m
```

**原因**: Prometheus 中没有 `pokemon_room_utilization` 指标数据

**诊断**:

```bash
# 1. 检查应用是否暴露了指标
kubectl port-forward pod/pokemon-server-0 9102:9102
# 在另一个终端:
curl http://localhost:9102/metrics | grep pokemon_room_utilization

# 2. 检查 Prometheus 是否收集了指标
kubectl port-forward -n monitoring pod/prometheus-monitoring-kube-prometheus-prometheus-0 9090:9090
# 访问 http://localhost:9090
# 在搜索框中输入 "pokemon_room_utilization"

# 3. 是否正确配置了 ServiceMonitor
kubectl get servicemonitors -A
kubectl describe servicemonitor -n default
```

**解决方案**:

```bash
# 1. 确保应用在 :9102 暴露 Prometheus 格式的指标
# 在应用代码中验证:
grep -r "9102\|prometheus\|metrics" src/

# 2. 如果应用没有暴露指标，需要修改代码添加:
#    prometheus_room_utilization gauge (pod标签, namespace标签)

# 3. 重建镜像后更新部署
docker build -t why5899/pokemon-worker:v0.8.5 .
docker push why5899/pokemon-worker:v0.8.5
kubectl set image statefulset/pokemon-server pokemon-server=why5899/pokemon-worker:v0.8.5

# 4. 等待新 Pod 启动后，Prometheus 会自动抓取指标（通常 1-2 分钟）
# 验证:
kubectl exec -n monitoring pod/prometheus-monitoring-kube-prometheus-prometheus-0 -- \
  curl -s 'http://localhost:9090/api/v1/query?query=pokemon_room_utilization' | jq
```

---

### 四、网络和连接问题

#### 问题 4.1: 无法通过 Service 访问应用

**症状**: 从集群内无法连接到 `pokemon-server-headless:50051`

**诊断**:

```bash
# 1. 确认 Service 存在
kubectl get svc pokemon-server-headless

# 2. 检查 Endpoint
kubectl get endpoints pokemon-server-headless

# 3. 从测试 Pod 连接
kubectl run -it --rm debug --image=nicolaka/netshoot --restart=Never -- bash
nc -zv pokemon-server-headless.default.svc.cluster.local 50051

# 4. 检查 DNS 解析
nslookup pokemon-server-headless.default.svc.cluster.local
```

**解决方案**:

```bash
# 如果 Endpoint 为空，确保 Pod 标签正确
kubectl get pods --show-labels | grep pokemon

# Service 中 selector 应该与 Pod labels 匹配
kubectl get svc pokemon-server-headless -o yaml | grep selector

# 如果不匹配，需要删除并重新应用 Service
kubectl delete svc pokemon-server-headless
kubectl apply -f k8s-deployment.yaml
```

---

#### 问题 4.2: Port Forward 连接超时

**症状**: `kubectl port-forward` 执行成功但无法连接

```bash
$ kubectl port-forward pod/pokemon-server-0 9103:50051
Forwarding from 127.0.0.1:9103 -> 50051
Forwarding from [::1]:9103 -> 50051
^C
```

**原因**: Pod 内应用未监听，或端口映射配置错误

**解决方案**:

```bash
# 1. 检查 Pod 内实际监听的端口
kubectl exec pokemon-server-0 -- ss -tlnp

# 2. 验证容器配置中的 containerPort
kubectl get pod pokemon-server-0 -o yaml | grep -A 10 "ports:"

# 3. 确认应用确实在监听（检查日志）
kubectl logs pokemon-server-0 | grep -i "listen\|accepting\|port"

# 4. 尝试更长的超时时间
kubectl port-forward pod/pokemon-server-0 9103:50051 --pod-running-timeout=5m
```

---

### 五、Redis 相关问题

#### 问题 5.1: Redis 连接失败

**症状**: Pod 日志显示 `redis connection refused` 或 `timeout`

```
E0512 03:42:31.123456   1 redis_client.cpp:45] Failed to connect to redis: Connection refused
```

**诊断**:

```bash
# 1. 确认 Redis 服务正在运行
kubectl get pods | grep redis
kubectl get svc | grep redis

# 2. 验证 Redis 地址配置
kubectl get configmap pokemon-server-config -o yaml | grep redis_url

# 3. 从应用 Pod 测试连接
kubectl exec pokemon-server-0 -- redis-cli -h <redis-host> -p 6379 ping
```

**解决方案**:

```bash
# 如果 Redis 服务未运行，部署它
kubectl apply -f k8s-redis.yaml

# 等待 Redis Pod 启动
kubectl wait --for=condition=ready pod -l app=redis-server --timeout=300s

# 更新 ConfigMap 中的 Redis 地址
kubectl patch configmap pokemon-server-config --type merge \
  -p '{"data":{"redis_url":"redis-server:6379"}}'

# 重启应用 Pod
kubectl rollout restart statefulset/pokemon-server
```

---

### 六、资源和性能问题

#### 问题 6.1: Pod 因资源限制被杀死

**症状**: Pod 状态为 `OOMKilled` 或 `Evicted`

```
NAME               READY   STATUS      
pokemon-server-0   0/1     OOMKilled
```

**原因**: 应用内存使用超过限制

**解决方案**:

```bash
# 1. 查看当前限制
kubectl get pod pokemon-server-0 -o yaml | grep -A 5 "limits\|requests"

# 2. 增加内存限制
kubectl set resources statefulset pokemon-server \
  --limits=memory=512Mi \
  --requests=memory=256Mi

# 3. 监控内存使用
kubectl top pods -l app=pokemon-server --containers

# 4. 查看历史事件
kubectl describe pod pokemon-server-0

# 5. 如果持续超限，可能需要优化代码
```

---

#### 问题 6.2: HPA 不工作，Pod 不扩缩容

**症状**: HPA 状态为 `True`，但副本数不变

**原因可能**:
1. 指标未达到扩缩容阈值
2. 已经达到 min/max replicas 限制
3. HPA 冷却时间未过

**诊断**:

```bash
# 1. 查看 HPA 详细信息
kubectl describe hpa pokemon-server-hpa

# 2. 查看 HPA 事件
kubectl get events | grep HorizontalPodAutoscaler

# 3. 检查当前指标值
kubectl get hpa pokemon-server-hpa -o jsonpath='{.status.currentMetrics[*]}'

# 4. 查看目标值
kubectl get hpa pokemon-server-hpa -o jsonpath='{.spec.metrics[*]}'
```

**解决方案**:

```bash
# 如果想立即扩缩，手动调整副本数
kubectl scale statefulset pokemon-server --replicas=5

# 查看扩缩进度
kubectl get statefulsets -w

# 等待所有新 Pod 就绪
kubectl wait --for=condition=ready pod -l app=pokemon-server --timeout=600s
```

---

## 调试技巧

### 一、内部调试

```bash
# 进入 Pod Shell
kubectl exec -it pokemon-server-0 -- bash

# 安装 debug 工具
kubectl debug -it pokemon-server-0 --image=ubuntu:latest

# 查看进程
ps aux

# 查看网络连接
ss -tlnp

# 查看环境变量
env | grep -E "REDIS|POD"
```

### 二、日志查询

```bash
# 实时日志
kubectl logs -f pokemon-server-0

# 查看前一个容器的日志 (Pod 重启后)
kubectl logs --previous pokemon-server-0

# 查看所有 Pod 的日志
kubectl logs -f -l app=pokemon-server --all-containers=true

# 导出日志到文件
kubectl logs pokemon-server-0 > pod.log

# 搜索特定错误
kubectl logs -l app=pokemon-server --tail=1000 | grep ERROR
```

### 三、事件追踪

```bash
# 按时间排序查看事件
kubectl get events --sort-by='.lastTimestamp'

# 持续监控事件
kubectl get events -w

# 查看特定资源的事件
kubectl describe statefulset pokemon-server
```

### 四、资源监控

```bash
# 实时监控 Pod 资源
kubectl top pods -l app=pokemon-server --containers

# 监控节点资源
kubectl top nodes

# 监听 Pod 状态变化
kubectl get pods -l app=pokemon-server -w
```

---

## 完整的故障排除流程

如果遇到问题，按以下顺序排查：

```bash
# 1. 检查 Pod 状态
kubectl get pods -o wide
kubectl describe pod pokemon-server-0

# 2. 查看日志
kubectl logs pokemon-server-0

# 3. 检查配置
kubectl get configmap pokemon-server-config -o yaml
kubectl get svc
kubectl get hpa

# 4. 验证连接性
kubectl run -it --rm debug --image=nicolaka/netshoot --restart=Never -- bash
ping pokemon-server-headless
nc -zv pokemon-server-headless 50051

# 5. 查看事件
kubectl get events --sort-by='.lastTimestamp' | tail -20

# 6. 检查资源
kubectl top pods
kubectl describe nodes

# 7. 收集所有诊断信息用于求助
kubectl cluster-info dump --output-directory=./cluster-dump
```

---

## 更多帮助

- 查看完整的部署指南: [SERVER_STARTUP_GUIDE.md](SERVER_STARTUP_GUIDE.md)
- Kubernetes 官方文档: https://kubernetes.io/docs/
- gRPC 文档: https://grpc.io/docs/
- Prometheus 文档: https://prometheus.io/docs/
