# labPushReceiver

微信云托管服务，用于接收 ESP32 直接上报的设备快照，执行服务端告警计算，并写入微信云数据库。
当前版本已内嵌 Python GRU 时序分类服务：Node 入口负责接收设备上报并维护最近 `16` 帧窗口，Python 子服务负责推理分类结果。

## 路由

- `GET /healthz`
- `POST /device/report`
- `GET /onenet/push`
- `POST /onenet/push`

其中 `/device/report` 是主链路；`/onenet/push` 仅保留给旧 OneNET 流程做过渡兼容。

## 环境变量

- `CLOUDBASE_ENV_ID`：微信云开发环境 ID
- `CLOUDBASE_APIKEY`：CloudBase HTTP API 的 Bearer Token
- `DEVICE_REPORT_TOKEN`：设备上报鉴权 token，请与 ESP32 中配置保持一致
- `INGEST_FUNCTION_NAME`：入库云函数名，默认 `labIngest`
- `SNAPSHOT_COLLECTION`：最新快照集合名，默认 `lab_snapshot`
- `HISTORY_COLLECTION`：历史记录集合名，默认 `lab_history`
- `GRU_ENABLED`：是否启用本地 GRU 推理，默认启用
- `GRU_SERVICE_URL`：Node 侧访问本地 Python GRU 服务的地址，默认 `http://127.0.0.1:8001`

## 设备上报格式

请求头：

- `Content-Type: application/json`
- `x-device-token: <DEVICE_REPORT_TOKEN>`

请求体：

```json
{
  "deviceName": "ESP32-S3",
  "productId": "lab-warning",
  "linkOnline": true,
  "groups": [
    { "online": true, "temperature": 27.2, "humidity": 33.1, "smoke": 2.7, "light": 23.0 },
    { "online": true, "temperature": 27.2, "humidity": 32.9, "smoke": 1.8, "light": 20.4 },
    { "online": true, "temperature": 26.4, "humidity": 31.5, "smoke": 1.4, "light": 27.6 },
    { "online": true, "temperature": 26.6, "humidity": 30.7, "smoke": 1.6, "light": 31.6 }
  ]
}
```

## 服务端处理

- 将 `groups[4]` 标准化为固定 4 组数据
- 使用服务端阈值计算 `group.alarm` 和 `systemAlarm`
- 为每台设备维护最近 `16` 帧序列窗口，窗口满后调用本地 GRU 服务执行状态分类
- 为记录补齐 `snapshotTime` / `snapshotTimeMs`
- 调用 `labIngest` 云函数完成入库：
  - `lab_snapshot`
  - `lab_history`

## 数据字段

后端写入文档的核心字段：

- `deviceName`
- `productId`
- `groups`
- `linkOnline`
- `systemAlarm`
- `sequencePrediction`
- `sequenceReady`
- `sequenceLength`
- `sourceTimeMs`
- `snapshotTimeMs`
- `snapshotTime`
- `sourceType`
