const cloud = require("wx-server-sdk");
const crypto = require("crypto");
const https = require("https");

const { onenet } = require("./config");

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV,
});

const SENSOR_GROUP_COUNT = 4;

function assertConfig() {
  if (!onenet.productId || !onenet.deviceName || (!onenet.deviceKey && !onenet.productAccessKey)) {
    throw new Error("请先在 cloudfunctions/labMonitor/config.js 中填写 OneNET 配置。");
  }
}

function base64UrlEncode(input) {
  return encodeURIComponent(input);
}

function buildOnenetToken() {
  const expiry = Math.floor(Date.now() / 1000) + Number(onenet.tokenExpirySeconds || 3600);
  const usingDeviceKey = !!onenet.deviceKey;
  const resource = usingDeviceKey
    ? `products/${onenet.productId}/devices/${onenet.deviceName}`
    : `products/${onenet.productId}`;
  const signatureSource = `${expiry}\n${onenet.tokenMethod}\n${resource}\n${onenet.tokenVersion}`;
  const keyBuffer = Buffer.from(onenet.deviceKey || onenet.productAccessKey, "base64");
  const sign = crypto
    .createHmac(onenet.tokenMethod, keyBuffer)
    .update(signatureSource, "utf8")
    .digest("base64");

  return `version=${onenet.tokenVersion}&res=${base64UrlEncode(resource)}&et=${expiry}&method=${onenet.tokenMethod}&sign=${base64UrlEncode(sign)}`;
}

function httpGet(url, headers) {
  return new Promise((resolve, reject) => {
    const request = https.get(
      url,
      {
        headers,
      },
      (response) => {
        let body = "";

        response.on("data", (chunk) => {
          body += chunk;
        });
        response.on("end", () => {
          if (response.statusCode < 200 || response.statusCode >= 300) {
            reject(new Error(`OneNET 请求失败，状态码 ${response.statusCode}，响应：${body}`));
            return;
          }

          try {
            resolve(JSON.parse(body));
          } catch (error) {
            reject(new Error(`OneNET 返回了无法解析的 JSON：${body}`));
          }
        });
      }
    );

    request.on("error", (error) => {
      reject(error);
    });

    request.end();
  });
}

async function queryDeviceInfo(token) {
  const url = `${onenet.host}/mqtt/v1/devices/${encodeURIComponent(onenet.deviceName)}`;
  const response = await httpGet(url, {
    Authorization: token,
  });

  if (response.code_no !== "000000" && response.code !== "000000" && response.code !== "onenet_common_success") {
    throw new Error(response.message || "查询 OneNET 设备信息失败");
  }

  return response.data || {};
}

async function queryDeviceImage(token, deviceId) {
  const url = `${onenet.host}/mqtt/v1/devices/${encodeURIComponent(deviceId)}/image`;
  const response = await httpGet(url, {
    Authorization: token,
  });

  if (response.code_no !== "000000" && response.code !== "000000" && response.code_no !== "onenet_common_success") {
    throw new Error(response.message || "查询 OneNET 设备镜像失败");
  }

  return response.data || {};
}

function pickValue(properties, key) {
  if (!properties) {
    return 0;
  }

  const value = properties[key];

  if (value === undefined || value === null) {
    return 0;
  }

  if (typeof value === "object" && value !== null && Object.prototype.hasOwnProperty.call(value, "value")) {
    return value.value;
  }

  return value;
}

function toBoolean(value) {
  if (typeof value === "boolean") {
    return value;
  }

  if (typeof value === "number") {
    return value !== 0;
  }

  if (typeof value === "string") {
    return value === "true" || value === "1";
  }

  return false;
}

function toNumber(value) {
  const numericValue = Number(value);

  if (Number.isNaN(numericValue)) {
    return 0;
  }

  return Number(numericValue.toFixed(1));
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function formatTimestamp(seconds) {
  if (!seconds) {
    return "--";
  }

  const date = new Date(seconds * 1000);

  return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())} ${pad2(date.getHours())}:${pad2(date.getMinutes())}:${pad2(date.getSeconds())}`;
}

function normalizeSnapshot(image) {
  const reported = (((image || {}).properties || {}).state || {}).reported || {};
  const metadataReported = ((((image || {}).properties || {}).metadata || {}).reported) || {};
  const groups = [];

  for (let index = 1; index <= SENSOR_GROUP_COUNT; index += 1) {
    groups.push({
      online: toBoolean(pickValue(reported, `g${index}_online`)),
      alarm: toBoolean(pickValue(reported, `g${index}_alarm`)),
      temperature: toNumber(pickValue(reported, `g${index}_temperature`)),
      humidity: toNumber(pickValue(reported, `g${index}_humidity`)),
      smoke: toNumber(pickValue(reported, `g${index}_smoke`)),
      light: toNumber(pickValue(reported, `g${index}_light`)),
    });
  }

  const updatedTimestamp =
    ((image || {}).properties || {}).timestamp ||
    (metadataReported.g1_temperature && metadataReported.g1_temperature.timestamp) ||
    (image || {}).lastActivityTime ||
    0;

  return {
    groups,
    linkOnline: toBoolean(pickValue(reported, "link_online")) || image.connectionState === "online",
    systemAlarm: toBoolean(pickValue(reported, "system_alarm")),
    updatedAt: formatTimestamp(updatedTimestamp),
  };
}

async function getLatestSnapshot() {
  assertConfig();

  const token = buildOnenetToken();
  const deviceInfo = await queryDeviceInfo(token);
  const image = await queryDeviceImage(token, deviceInfo.device_id || deviceInfo.deviceId);

  return {
    success: true,
    data: normalizeSnapshot(image),
  };
}

exports.main = async (event) => {
  try {
    switch (event.action) {
      case "getLatestSnapshot":
      default:
        return await getLatestSnapshot();
    }
  } catch (error) {
    console.error("labMonitor failed", error);
    return {
      success: false,
      message: error.message || "获取 OneNET 数据失败",
    };
  }
};
