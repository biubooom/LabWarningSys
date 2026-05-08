# labPushReceiver

用于接收 OneNET 的 HTTP Push，并将最新状态写入微信云开发数据库。

## 路由

- `GET /healthz`
- `POST /onenet/push`

## 环境变量

- `CLOUDBASE_ENV_ID`：微信云开发环境 ID
- `ONENET_PUSH_TOKEN`：OneNET 推送时携带的校验 token
- `SNAPSHOT_COLLECTION`：最新快照集合名，默认 `lab_snapshot`
- `HISTORY_COLLECTION`：历史记录集合名，默认 `lab_history`

## 推送内容要求

请求体中应至少包含这些字段之一：

- `g1_online ~ g4_online`
- `g1_alarm ~ g4_alarm`
- `g1_temperature ~ g4_temperature`
- `g1_humidity ~ g4_humidity`
- `g1_smoke ~ g4_smoke`
- `g1_light ~ g4_light`
- `link_online`
- `system_alarm`

支持字段出现在：

- 顶层
- `body`
- `params`
- `data`
- `payload`

## 输出集合

### lab_snapshot

始终保存每台设备最新一条状态。

### lab_history

保存每次推送的历史快照。
