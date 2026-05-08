const express = require("express");

const app = express();
const port = process.env.PORT || 80;

function readEnv(...keys) {
  for (const key of keys) {
    if (process.env[key]) {
      return process.env[key];
    }
  }

  return "";
}

const envId = readEnv("CLOUDBASE_ENV_ID", "CLOUDBASE-ENV-ID", "TCB_ENV");
const pushToken = readEnv("ONENET_PUSH_TOKEN", "ONENET-PUSH-TOKEN");
const snapshotCollectionName = readEnv("SNAPSHOT_COLLECTION", "SNAPSHOT-COLLECTION") || "lab_snapshot";
const historyCollectionName = readEnv("HISTORY_COLLECTION", "HISTORY-COLLECTION") || "lab_history";
const manualApiKey = readEnv("CLOUDBASE_APIKEY", "CLOUDBASE-APIKEY");
const snapshotModelName = readEnv("SNAPSHOT_MODEL", "SNAPSHOT-MODEL") || snapshotCollectionName;
const historyModelName = readEnv("HISTORY_MODEL", "HISTORY-MODEL") || historyCollectionName;
const apiBaseUrl = envId ? `https://${envId}.api.tcloudbasegateway.com` : "";

app.use(express.json({ limit: "1mb" }));
app.use(express.urlencoded({ extended: true, limit: "1mb" }));
app.use(express.text({ type: "*/*", limit: "1mb" }));

function formatTimestamp(ms) {
  const formatter = new Intl.DateTimeFormat("zh-CN", {
    timeZone: "Asia/Shanghai",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  });

  const parts = formatter.formatToParts(new Date(ms));
  const valueMap = {};

  for (const part of parts) {
    if (part.type !== "literal") {
      valueMap[part.type] = part.value;
    }
  }

  return `${valueMap.year}-${valueMap.month}-${valueMap.day} ${valueMap.hour}:${valueMap.minute}:${valueMap.second}`;
}

function digValue(source, key) {
  if (!source || typeof source !== "object") {
    return undefined;
  }

  if (Object.prototype.hasOwnProperty.call(source, key)) {
    const value = source[key];

    if (
      value &&
      typeof value === "object" &&
      !Array.isArray(value) &&
      Object.prototype.hasOwnProperty.call(value, "value")
    ) {
      return value.value;
    }

    return value;
  }

  if (source.body && Object.prototype.hasOwnProperty.call(source.body, key)) {
    return source.body[key];
  }

  if (source.params && Object.prototype.hasOwnProperty.call(source.params, key)) {
    return source.params[key];
  }

  if (source.data && Object.prototype.hasOwnProperty.call(source.data, key)) {
    const value = source.data[key];

    if (
      value &&
      typeof value === "object" &&
      !Array.isArray(value) &&
      Object.prototype.hasOwnProperty.call(value, "value")
    ) {
      return value.value;
    }

    return value;
  }

  if (source.payload && typeof source.payload === "object" && Object.prototype.hasOwnProperty.call(source.payload, key)) {
    return source.payload[key];
  }

  return undefined;
}

function deepFindValue(source, key, depth = 0) {
  if (!source || depth > 6) {
    return undefined;
  }

  if (Array.isArray(source)) {
    for (const item of source) {
      const result = deepFindValue(item, key, depth + 1);

      if (result !== undefined) {
        return result;
      }
    }

    return undefined;
  }

  if (typeof source !== "object") {
    return undefined;
  }

  if (Object.prototype.hasOwnProperty.call(source, key)) {
    const value = source[key];

    if (
      value &&
      typeof value === "object" &&
      !Array.isArray(value) &&
      Object.prototype.hasOwnProperty.call(value, "value")
    ) {
      return value.value;
    }

    return value;
  }

  for (const [childKey, childValue] of Object.entries(source)) {
    if (childKey === key) {
      return childValue;
    }

    if (
      childValue &&
      typeof childValue === "object" &&
      !Array.isArray(childValue) &&
      Object.prototype.hasOwnProperty.call(childValue, "value")
    ) {
      if (childKey === key) {
        return childValue.value;
      }
    }

    const nested = deepFindValue(childValue, key, depth + 1);

    if (nested !== undefined) {
      return nested;
    }
  }

  return undefined;
}

function pickMetricValue(source, key) {
  const directValue = digValue(source, key);

  if (directValue !== undefined) {
    return directValue;
  }

  const deepValue = deepFindValue(source, key);

  if (deepValue !== undefined) {
    return deepValue;
  }

  return undefined;
}

function tryParseJson(text) {
  if (typeof text !== "string") {
    return null;
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    return null;
  }
}

function unwrapPayload(input) {
  if (!input) {
    return {};
  }

  if (typeof input === "string") {
    const parsedJson = tryParseJson(input);

    if (parsedJson) {
      return unwrapPayload(parsedJson);
    }

    const params = new URLSearchParams(input);
    const msg = params.get("msg");

    if (msg) {
      const parsedMsg = tryParseJson(msg);
      return parsedMsg || { msg };
    }

    return {};
  }

  if (typeof input === "object") {
    if (typeof input.msg === "string") {
      const parsedMsg = tryParseJson(input.msg);

      if (parsedMsg) {
        return parsedMsg;
      }
    }

    return input;
  }

  return {};
}

function buildPayloadShape(source, depth = 0) {
  if (depth > 2) {
    return "...";
  }

  if (Array.isArray(source)) {
    return source.slice(0, 3).map((item) => buildPayloadShape(item, depth + 1));
  }

  if (!source || typeof source !== "object") {
    return source;
  }

  const result = {};

  for (const [key, value] of Object.entries(source)) {
    if (value && typeof value === "object") {
      result[key] = buildPayloadShape(value, depth + 1);
    } else {
      result[key] = typeof value;
    }
  }

  return result;
}

function buildParamSamples(source) {
  const params =
    source &&
    source.data &&
    source.data.params &&
    typeof source.data.params === "object"
      ? source.data.params
      : {};

  const sampleKeys = [
    "g1_temperature",
    "g1_online",
    "g1_humidity",
    "g1_smoke",
    "g1_light",
    "link_online",
    "system_alarm",
  ];

  const result = {};

  for (const key of sampleKeys) {
    if (Object.prototype.hasOwnProperty.call(params, key)) {
      result[key] = params[key];
    }
  }

  return result;
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

function normalizePayload(payload) {
  const source = unwrapPayload(payload);
  const nowMs = Date.now();
  const groups = {
    g1: {
      online: toBoolean(pickMetricValue(source, "g1_online")),
      alarm: toBoolean(pickMetricValue(source, "g1_alarm")),
      temperature: toNumber(pickMetricValue(source, "g1_temperature")),
      humidity: toNumber(pickMetricValue(source, "g1_humidity")),
      smoke: toNumber(pickMetricValue(source, "g1_smoke")),
      light: toNumber(pickMetricValue(source, "g1_light")),
    },
    g2: {
      online: toBoolean(pickMetricValue(source, "g2_online")),
      alarm: toBoolean(pickMetricValue(source, "g2_alarm")),
      temperature: toNumber(pickMetricValue(source, "g2_temperature")),
      humidity: toNumber(pickMetricValue(source, "g2_humidity")),
      smoke: toNumber(pickMetricValue(source, "g2_smoke")),
      light: toNumber(pickMetricValue(source, "g2_light")),
    },
    g3: {
      online: toBoolean(pickMetricValue(source, "g3_online")),
      alarm: toBoolean(pickMetricValue(source, "g3_alarm")),
      temperature: toNumber(pickMetricValue(source, "g3_temperature")),
      humidity: toNumber(pickMetricValue(source, "g3_humidity")),
      smoke: toNumber(pickMetricValue(source, "g3_smoke")),
      light: toNumber(pickMetricValue(source, "g3_light")),
    },
    g4: {
      online: toBoolean(pickMetricValue(source, "g4_online")),
      alarm: toBoolean(pickMetricValue(source, "g4_alarm")),
      temperature: toNumber(pickMetricValue(source, "g4_temperature")),
      humidity: toNumber(pickMetricValue(source, "g4_humidity")),
      smoke: toNumber(pickMetricValue(source, "g4_smoke")),
      light: toNumber(pickMetricValue(source, "g4_light")),
    },
  };

  return {
    deviceName: pickMetricValue(source, "deviceName") || pickMetricValue(source, "device_name") || "ESP32-S3",
    productId: pickMetricValue(source, "productId") || pickMetricValue(source, "product_id") || "",
    groups,
    linkOnline: toBoolean(pickMetricValue(source, "link_online")),
    systemAlarm: toBoolean(pickMetricValue(source, "system_alarm")),
    sourceTimeMs: Number(pickMetricValue(source, "timestamp") || pickMetricValue(source, "dataTimestamp") || nowMs),
    snapshotTimeMs: nowMs,
    snapshotTime: formatTimestamp(nowMs),
  };
}

async function withTimeout(promise, label, timeoutMs = 15000) {
  let timeoutHandle;

  const timeoutPromise = new Promise((_, reject) => {
    timeoutHandle = setTimeout(() => {
      reject(new Error(`${label} timeout after ${timeoutMs}ms`));
    }, timeoutMs);
  });

  try {
    return await Promise.race([promise, timeoutPromise]);
  } finally {
    clearTimeout(timeoutHandle);
  }
}

async function postModelRecord(modelName, doc, label) {
  if (!apiBaseUrl) {
    throw new Error("missing cloudbase env id");
  }

  if (!manualApiKey) {
    throw new Error("missing cloudbase api key");
  }

  const response = await withTimeout(
    fetch(`${apiBaseUrl}/v1/model/prod/${encodeURIComponent(modelName)}/create`, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${manualApiKey}`,
        "Content-Type": "application/json",
        Accept: "application/json",
      },
      body: JSON.stringify({
        data: doc,
      }),
    }),
    `${label} request`
  );

  const rawText = await response.text();
  let result = null;

  try {
    result = rawText ? JSON.parse(rawText) : null;
  } catch (error) {
    result = {
      rawText,
    };
  }

  if (!response.ok) {
    console.error(`${label} response error`, JSON.stringify({
      status: response.status,
      body: result,
    }));
    throw new Error(`${label} http ${response.status}: ${typeof result === "string" ? result : JSON.stringify(result)}`);
  }

  if (result && result.code) {
    console.error(`${label} api error`, JSON.stringify(result));
    throw new Error(`${label} failed: ${result.code}${result.message ? ` ${result.message}` : ""}${result.requestId ? ` requestId=${result.requestId}` : ""}`);
  }

  return result;
}

async function addSnapshot(snapshot) {
  const doc = {
    deviceName: snapshot.deviceName,
    productId: snapshot.productId,
    groups: snapshot.groups,
    linkOnline: snapshot.linkOnline,
    systemAlarm: snapshot.systemAlarm,
    sourceTimeMs: snapshot.sourceTimeMs,
    snapshotTimeMs: snapshot.snapshotTimeMs,
    snapshotTime: snapshot.snapshotTime,
  };

  await postModelRecord(snapshotModelName, doc, "snapshot create");
}

async function appendHistory(snapshot) {
  await postModelRecord(historyModelName, {
    deviceName: snapshot.deviceName,
    productId: snapshot.productId,
    groups: snapshot.groups,
    linkOnline: snapshot.linkOnline,
    systemAlarm: snapshot.systemAlarm,
    sourceTimeMs: snapshot.sourceTimeMs,
    snapshotTimeMs: snapshot.snapshotTimeMs,
    snapshotTime: snapshot.snapshotTime,
  }, "history create");
}

app.get("/healthz", (req, res) => {
  res.json({
    ok: true,
    envId,
    snapshotCollectionName,
    historyCollectionName,
    snapshotModelName,
    historyModelName,
    apiBaseUrl,
    apiKeyInjected: !!manualApiKey,
    mode: "cloudbase-model-http-api",
  });
});

app.get("/onenet/push", (req, res) => {
  const msg = typeof req.query.msg === "string" ? req.query.msg : "";

  if (msg) {
    res.status(200).type("text/plain").send(msg);
    return;
  }

  res.status(200).json({
    success: true,
    message: "push endpoint is ready",
  });
});

app.post("/onenet/push", async (req, res) => {
  try {
    const requestBody = unwrapPayload(req.body);
    const receivedToken =
      req.header("x-onenet-token") ||
      req.query.token ||
      (requestBody && requestBody.token) ||
      "";

    console.log("onenet payload shape", JSON.stringify(buildPayloadShape(requestBody)));
    console.log("onenet param samples", JSON.stringify(buildParamSamples(requestBody)));

    if (pushToken && receivedToken && receivedToken !== pushToken) {
      res.status(403).json({
        success: false,
        message: "invalid token",
      });
      return;
    }

    const snapshot = normalizePayload(requestBody);

    await addSnapshot(snapshot);
    await appendHistory(snapshot);

    res.json({
      success: true,
      message: "snapshot synced",
    });
  } catch (error) {
    console.error("onenet push failed", error);
    res.status(500).json({
      success: false,
      message: error.message || "sync failed",
    });
  }
});

app.listen(port, () => {
  console.log(`labPushReceiver listening on ${port}`);
});
