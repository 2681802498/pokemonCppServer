# Pokemon C++ Server - 自动拉起 Pod 失败分析报告

## 问题总结

**时间**: 2026年5月12日
**症状**: 部署到 Kubernetes 后，HPA (Horizontal Pod Autoscaler) 无法工作，Pod 无法自动扩缩容
**最终原因**: Prometheus Adapter 配置文件格式错误，导致 Custom Metrics API 未能注册

---

## 详细问题分析

### 一、用户观测到的现象

1. **HPA 状态异常**

```bash
$ kubectl describe hpa pokemon-server-hpa

ScalingActive   False   FailedGetPodsMetric
```

2. **警告信息**

```
unable to get metric pokemon_room_utilization: 
unable to fetch metrics from custom metrics API: 
no known available metric versions found
```

3. **Pod 始终停留在最小副本数 (2个)**，无法自动增长

---

### 二、问题诊断过程

#### 步骤 1: 确认 Pod 已启动

```bash
$ kubectl get pods -o wide
NAME               READY   STATUS    
pokemon-server-0   1/1     Running   
pokemon-server-1   1/1     Running   
```

✅ Pod 本身运行正常，说明问题不在应用或 StatefulSet。

#### 步骤 2: 检查 Prometheus Adapter 部署状态

```bash
$ kubectl get deployment -n monitoring | grep prometheus-adapter
monitoring           prometheus-adapter                    0/1     1    0  3d15h
```

❌ **关键发现**: Prometheus Adapter 只有 `0/1` 运行中，说明 Pod 启动失败。

#### 步骤 3: 查看 Prometheus Adapter Pod 日志

```bash
$ kubectl logs -n monitoring deployment/prometheus-adapter

F0512 03:37:06.899618       1 adapter.go:340] 
unable to load metrics discovery config: 
unable to load metrics discovery configuration: 
unable to parse metrics discovery config: 
yaml: unmarshal errors:
line 2: cannot unmarshal !!map into []config.DiscoveryRule
```

❌ **根本原因找到**: YAML 配置文件格式错误，无法被解析。

#### 步骤 4: 检查 ConfigMap 配置

```bash
$ kubectl get configmap custom-metrics-config -n monitoring -o yaml

data:
  config.yaml: |
    rules:
      default: false
      custom:
      - seriesQuery: 'pokemon_room_utilization'
        resources:
          overrides:
            namespace: {resource: "namespace"}
            pod: {resource: "pod"}
        name:
          matches: "pokemon_room_utilization"
          as: "pokemon_room_utilization"
        metricsQuery: 'avg(pokemon_room_utilization{<<.LabelMatchers>>}) by (<<.GroupBy>>)'
```

**问题分析**: 配置结构错误

---

### 三、根本原因详解

#### 错误的配置结构

```yaml
rules:
  default: false        # ❌ 这一层结构导致解析失败
  custom:              # ❌ 应该直接是数组
  - seriesQuery: ...
```

Prometheus Adapter **期望** `rules` 是一个数组 (`[]config.DiscoveryRule`)，但接收到的是一个对象 (map)，因此 YAML 反序列化失败。

#### 正确的配置结构

```yaml
rules:                 # ✅ 直接跟数组
- seriesQuery: 'pokemon_room_utilization'
  resources:
    overrides:
      namespace: {resource: "namespace"}
      pod: {resource: "pod"}
  name:
    matches: "pokemon_room_utilization"
    as: "pokemon_room_utilization"
  metricsQuery: 'avg(pokemon_room_utilization{<<.LabelMatchers>>}) by (<<.GroupBy>>)'
```

---

### 四、问题的级联影响

```
ConfigMap 格式错误
        ↓
Prometheus Adapter 无法启动 (CrashLoopBackOff)
        ↓
Custom Metrics API 未注册 (no known available metric versions)
        ↓
HPA 无法获取自定义指标 (pokemon_room_utilization)
        ↓
HPA ScalingActive = False (FailedGetPodsMetric)
        ↓
自动扩缩容不工作，Pod 数量固定
```

---

## 修复方案

### 修复步骤

#### 1. 更新 ConfigMap

```bash
kubectl patch configmap custom-metrics-config -n monitoring --type merge \
  -p '{"data":{"config.yaml":"rules:\n- seriesQuery: '\''pokemon_room_utilization'\''\n  resources:\n    overrides:\n      namespace: {resource: \"namespace\"}\n      pod: {resource: \"pod\"}\n  name:\n    matches: \"pokemon_room_utilization\"\n    as: \"pokemon_room_utilization\"\n  metricsQuery: '\''avg(pokemon_room_utilization{<<.LabelMatchers>>}) by (<<.GroupBy>>)'\''\n"}}'
```

#### 2. 重启 Prometheus Adapter

```bash
kubectl rollout restart deployment/prometheus-adapter -n monitoring
```

#### 3. 验证修复

```bash
# 等待 Pod 启动
sleep 30

# 检查 HPA 状态
kubectl describe hpa pokemon-server-hpa

# 预期输出: ScalingActive   True   SucceededGetPodsMetric
```

---

## 修复效果

### 修复前

```bash
$ kubectl get deployment -n monitoring | grep prometheus-adapter
0/1     1    0  3d15h     ❌ Pod 未运行

$ kubectl describe hpa pokemon-server-hpa
ScalingActive   False   FailedGetPodsMetric
```

### 修复后

```bash
$ kubectl get deployment -n monitoring | grep prometheus-adapter
1/1     1    1  3d15h     ✅ Pod 成功运行

$ kubectl describe hpa pokemon-server-hpa
ScalingActive   True    SucceededGetPodsMetric
Metrics:        pokemon_room_utilization on pods: 350m / 500m
```

---

## 根本原因总结

| 层面 | 问题 |
|------|------|
| **直接原因** | ConfigMap `custom-metrics-config` 的 YAML 格式错误 |
| **根本原因** | `rules` 字段配置为嵌套对象而非数组 |
| **触发链** | 格式错误 → 解析失败 → Adapter 崩溃 → API 不可用 → HPA 无指标 |
| **影响范围** | HPA 自动扩缩容失效 |
| **解决难度** | 低（YAML 格式调整） |

---

## 预防措施

### 1. ConfigMap 验证

在部署前验证 YAML 格式：

```bash
# 使用 yamllint 验证
yamllint custom-metrics-config.yaml

# 使用 kubectl dry-run 测试
kubectl apply -f custom-metrics-config.yaml --dry-run=client -o yaml
```

### 2. 监控 Adapter 状态

添加启动检查脚本：

```bash
#!/bin/bash
# 检查 Prometheus Adapter 是否正常运行
ADAPTER_READY=$(kubectl get deployment -n monitoring prometheus-adapter \
  -o jsonpath='{.status.readyReplicas}')

if [ "$ADAPTER_READY" -ne 1 ]; then
  echo "ERROR: Prometheus Adapter not ready"
  kubectl logs -n monitoring deployment/prometheus-adapter
  exit 1
fi

# 检查 HPA 是否激活
HPA_ACTIVE=$(kubectl get hpa pokemon-server-hpa \
  -o jsonpath='{.status.conditions[?(@.type=="ScalingActive")].status}')

if [ "$HPA_ACTIVE" != "True" ]; then
  echo "ERROR: HPA ScalingActive is not True"
  kubectl describe hpa pokemon-server-hpa
  exit 1
fi

echo "✅ All checks passed"
```

### 3. 配置检查清单

在应用 ConfigMap 前检查：

- [ ] `rules` 是否直接跟数组标记 (`-`)
- [ ] 没有 `custom:` 或 `default:` 嵌套结构
- [ ] `seriesQuery` 指标名称与应用暴露的指标匹配
- [ ] `metricsQuery` 模板变量正确 (`<<.LabelMatchers>>`, `<<.GroupBy>>`)
- [ ] 使用 `kubectl apply --dry-run=client` 验证

---

## 相关文档

- [服务器启动教程](SERVER_STARTUP_GUIDE.md) - 完整的部署指南
- [问题速查表](TROUBLESHOOTING.md) - 其他常见问题解决方案
- Prometheus Adapter 官方文档: https://github.com/kubernetes-sigs/prometheus-adapter

---

## 参考代码

### 正确的 Prometheus Adapter 配置

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: custom-metrics-config
  namespace: monitoring
data:
  config.yaml: |
    rules:
    - seriesQuery: 'pokemon_room_utilization'
      seriesFilters: []
      resources:
        overrides:
          namespace: {resource: "namespace"}
          pod: {resource: "pod"}
      name:
        matches: "^(pokemon_room_utilization)"
        as: "${1}"
      metricsQuery: 'avg(<<.Series>>{<<.LabelMatchers>>}) by (<<.GroupBy>>)'
```

### 对应的 HPA 配置

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: pokemon-server-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: StatefulSet
    name: pokemon-server
  minReplicas: 2
  maxReplicas: 10
  metrics:
  - type: Pods
    pods:
      metric:
        name: pokemon_room_utilization
      target:
        type: AverageValue
        averageValue: "500m"
```

---

## 学习成果

这个事件教会我们：

1. **Kubernetes 配置链**: ConfigMap → Adapter → API → HPA → Pod 扩缩容
2. **YAML 格式重要性**: 一个嵌套层级的错误会导致整条链路失效
3. **诊断方法**: 从外向内逐层排查 (HPA → Adapter → ConfigMap)
4. **日志很重要**: Prometheus Adapter 的错误日志清楚指出了 YAML 解析问题

---

## 后续改进建议

1. **添加配置验证 Webhook**: 防止错误的 ConfigMap 被应用
2. **增加健康检查**: 在部署脚本中验证整条链路完整性
3. **编写测试**: 在 CI/CD 中验证 HPA 配置和 Prometheus Adapter 正常运行
4. **文档改进**: 在部署文档中强调 YAML 格式要求
