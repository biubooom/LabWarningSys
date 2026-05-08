# 实验室预警系统小程序

这是基于微信云开发模板改造的小程序 V1，用于展示四组实验室传感器实时数据。

## 目录说明

- `miniprogram/`：小程序前端
- `cloudfunctions/labMonitor/`：保留的 OneNET 直连查询云函数
- `cloudfunctions/labMonitorDb/`：读取云数据库最新快照的云函数
- `cloudfunctions/labIngest/`：接收标准化快照并写入云数据库
- `cloudhosting/labPushReceiver/`：接收 OneNET HTTP Push 并写入云数据库的服务骨架
- `cloudfunctions/quickstartFunctions/`：保留的官方演示云函数

## 首次配置

### 1. 配置云开发环境

编辑 `miniprogram/app.js`：

- 将 `globalData.env` 改成你的微信云开发环境 ID

### 2. 配置小程序读取云函数

当前首页默认读取 `labMonitorDb`，无需在小程序前端配置 OneNET 密钥。

### 3. 上传云函数

在微信开发者工具中：

- 右键 `cloudfunctions/labMonitorDb`
- 选择“上传并部署：云端安装依赖”
- 右键 `cloudfunctions/labIngest`
- 选择“上传并部署：云端安装依赖”
- 右键 `cloudfunctions/labMonitor`
- 选择“上传并部署：云端安装依赖”

### 4. 部署 OneNET 推送接收服务

将 `cloudhosting/labPushReceiver/` 部署到微信云托管或你自己的公网 Node.js 服务，并配置这些环境变量：

- `CLOUDBASE_ENV_ID`
- `ONENET_PUSH_TOKEN`
- `SNAPSHOT_COLLECTION=lab_snapshot`
- `HISTORY_COLLECTION=lab_history`

### 5. 在 OneNET 中配置 HTTP Push

将推送地址配置为：

- `POST https://你的公网域名/onenet/push`

并携带你设置的 `ONENET_PUSH_TOKEN`。

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

## OneNET 推送字段约定

需要在 OneNET 中存在以下属性标识符：

- `g1_online` `g1_temperature` `g1_humidity` `g1_smoke` `g1_light` `g1_alarm`
- `g2_online` `g2_temperature` `g2_humidity` `g2_smoke` `g2_light` `g2_alarm`
- `g3_online` `g3_temperature` `g3_humidity` `g3_smoke` `g3_light` `g3_alarm`
- `g4_online` `g4_temperature` `g4_humidity` `g4_smoke` `g4_light` `g4_alarm`
- `link_online`
- `system_alarm`
