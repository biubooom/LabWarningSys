# 实验室预警系统小程序

这是基于微信云开发模板改造的小程序 V1，用于展示四组实验室传感器实时数据。当前主链路已经改为：

- `STM32 -> ESP32 -> 云托管后端 -> 云数据库 -> 小程序`

## 目录说明

- `miniprogram/`：小程序前端
- `cloudfunctions/labMonitorDb/`：读取云数据库最新快照与历史记录
- `cloudfunctions/labIngest/`：保留的标准化快照入库云函数
- `cloudfunctions/labMonitor/`：保留的 OneNET 兼容查询云函数，不再是主链路
- `cloudhosting/labPushReceiver/`：设备直连后端，负责接收入库和告警处理
- `cloudfunctions/quickstartFunctions/`：保留的官方演示云函数

## 首次配置

### 1. 配置云开发环境

编辑 `miniprogram/app.js`：

- 将 `globalData.env` 改成你的微信云开发环境 ID

### 2. 上传云函数

在微信开发者工具中：

- 右键 `cloudfunctions/labMonitorDb`
- 选择“上传并部署：云端安装依赖”
- 右键 `cloudfunctions/labIngest`
- 选择“上传并部署：云端安装依赖”

`labMonitor` 仅在你还需要保留 OneNET 查询兼容时再上传。

### 3. 部署云托管后端

将 `cloudhosting/labPushReceiver/` 部署到微信云托管。这个目录已经包含：

- Node 入口服务：`cloudhosting/labPushReceiver/app.js`
- Python GRU 推理代码：`cloudhosting/labPushReceiver/sequence_backend/`
- 模型权重与元数据：
  - `cloudhosting/labPushReceiver/sequence_backend/artifacts/gru_backend/gru_state_dict.pt`
  - `cloudhosting/labPushReceiver/sequence_backend/artifacts/gru_backend/metadata.json`

也就是说，本地训练好的模型不是单独再部署一个服务，而是直接跟随这个云托管容器一起部署。容器启动后：

- Node 服务监听 `PORT`
- Node 进程会自动拉起本机 `127.0.0.1:8001` 上的 Python GRU 子服务
- 设备上报命中 `/device/report`
- 后端累计满 `16` 帧后自动调用 `/predict`
- 结果写入 `sequencePrediction`

云托管部署时请配置这些环境变量：

- `CLOUDBASE_ENV_ID`
- `CLOUDBASE_APIKEY`
- `DEVICE_REPORT_TOKEN`
- `INGEST_FUNCTION_NAME=labIngest`
- `SNAPSHOT_COLLECTION=lab_snapshot`
- `HISTORY_COLLECTION=lab_history`
- `GRU_ENABLED=true`
- `GRU_SERVICE_URL=http://127.0.0.1:8001`

推荐在微信开发者工具或云开发控制台中，将 `cloudhosting/labPushReceiver/` 作为云托管服务根目录部署。

部署完成后先检查：

- `GET /healthz`

返回中建议确认这些字段：

- `gruEnabled: true`
- `gruProcessRunning: true`
- `apiKeyInjected: true`
- `deviceTokenInjected: true`

如果 `gruProcessRunning` 是 `false`，通常意味着：

- Python 依赖安装失败
- 模型文件未包含进镜像
- 容器内 Python 子服务启动失败

这时优先查看云托管日志中带 `[gru]` 前缀的输出。

### 4. 配置 ESP32 设备上报

在 ESP32 配置中填写：

- `APP_CLOUD_REPORT_URL=https://你的公网域名/device/report`
- `APP_DEVICE_REPORT_TOKEN=<与云托管一致的 token>`
- `APP_PRODUCT_ID=lab-warning`
- `APP_DEVICE_NAME=ESP32-S3`

### 5. 数据读取

小程序首页和历史页默认都读取云数据库：

- 最新状态：`lab_snapshot`
- 历史记录：`lab_history`

## 本地模型如何变成后端能力

你本地训练好的 GRU 模型要进入微信云托管，关键不是“上传 `.pt` 文件到小程序前端”，而是：

1. 将模型文件放进云托管服务目录
   - 当前仓库已经放在：
     - `cloudhosting/labPushReceiver/sequence_backend/artifacts/gru_backend/gru_state_dict.pt`
     - `cloudhosting/labPushReceiver/sequence_backend/artifacts/gru_backend/metadata.json`
2. 让 Docker 镜像把这些文件一起打包
   - 当前 `cloudhosting/labPushReceiver/Dockerfile` 已经执行 `COPY . .`
3. 容器启动 Node 服务时自动启动本地 Python 推理服务
   - 当前 `cloudhosting/labPushReceiver/app.js` 中的 `startGruService()` 已经完成这一步
4. Node 接收设备上报后累计 `16` 帧，调用本地 GRU 服务预测
   - 当前 `attachSequencePrediction()` 和 `predictSequenceState()` 已经完成这一步
5. 预测结果通过 `labIngest` 云函数写回云数据库
   - 小程序前端再通过 `labMonitorDb` 云函数读取展示

所以对你当前项目来说，“部署本地模型到后端”的实际动作就是：部署 `cloudhosting/labPushReceiver/` 这个云托管服务。

## 当前返回数据结构

云函数 `labMonitorDb` 返回：

```json
{
  "success": true,
  "data": {
    "groups": [
      {
        "online": true,
        "alarm": false,
        "temperature": 25.1,
        "humidity": 53.2,
        "smoke": 11.4,
        "light": 68.0
      }
    ],
    "linkOnline": true,
    "systemAlarm": false,
    "updatedAt": "2026-05-04 18:30:00"
  }
}
```

## 数据库集合

- `lab_snapshot`：最新状态
- `lab_history`：历史快照
