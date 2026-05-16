const express = require("express");
const { spawn } = require("child_process");

const app = express();
const port = process.env.PORT || 80;

const SENSOR_GROUP_COUNT = 4;
const SEQUENCE_LENGTH = 16;
const TEMP_ALARM_THRESHOLD_C = 45.0;
const LOW_TEMP_THRESHOLD_C = 15.0;
const HUMI_ALARM_THRESHOLD_RH = 80.0;
const SMOKE_ALARM_THRESHOLD_PERCENT = 20.0;
const CORNER_NAMES = ["东北角", "西北角", "东南角", "西南角"];
const THRESHOLD_STATES = [
  { stateLabel: "THRESHOLD_FIRE", displayName: "火灾", shortName: "FIRE", key: "fire", priority: 5 },
  { stateLabel: "THRESHOLD_GAS_LEAK", displayName: "气体泄漏", shortName: "GAS", key: "gasLeak", priority: 4 },
  { stateLabel: "THRESHOLD_HIGH_TEMP", displayName: "高温", shortName: "HOT", key: "highTemp", priority: 3 },
  { stateLabel: "THRESHOLD_LOW_TEMP", displayName: "低温", shortName: "COLD", key: "lowTemp", priority: 2 },
  { stateLabel: "THRESHOLD_HIGH_HUMID", displayName: "高湿", shortName: "HUMID", key: "highHumid", priority: 1 },
];
const THRESHOLD_NORMAL_STATE = {
  stateLabel: "THRESHOLD_NORMAL",
  displayName: "正常",
  shortName: "NORMAL",
  priority: 0,
};

function areAdjacentGroups(groupIdA, groupIdB) {
  const normalizedA = Number(groupIdA);
  const normalizedB = Number(groupIdB);
  const adjacentPairs = new Set(["1-2", "1-3", "2-4", "3-4"]);
  const pairKey = [normalizedA, normalizedB].sort((left, right) => left - right).join("-");
  return adjacentPairs.has(pairKey);
}

function readEnv(...keys) {
  for (const key of keys) {
    if (process.env[key]) {
      return process.env[key];
    }
  }

  return "";
}

const envId = readEnv("CLOUDBASE_ENV_ID", "CLOUDBASE-ENV-ID", "TCB_ENV");
const deviceReportToken = readEnv("DEVICE_REPORT_TOKEN", "DEVICE-REPORT-TOKEN");
const pushToken = readEnv("ONENET_PUSH_TOKEN", "ONENET-PUSH-TOKEN");
const snapshotCollectionName = readEnv("SNAPSHOT_COLLECTION", "SNAPSHOT-COLLECTION") || "lab_snapshot";
const historyCollectionName = readEnv("HISTORY_COLLECTION", "HISTORY-COLLECTION") || "lab_history";
const manualApiKey = readEnv("CLOUDBASE_APIKEY", "CLOUDBASE-APIKEY");
const ingestFunctionName = readEnv("INGEST_FUNCTION_NAME", "INGEST-FUNCTION-NAME") || "labIngest";
const apiBaseUrl = envId ? `https://${envId}.api.tcloudbasegateway.com` : "";
const gruServiceUrl = readEnv("GRU_SERVICE_URL", "GRU-SERVICE-URL") || "http://127.0.0.1:8001";
const gruEnabled = readEnv("GRU_ENABLED", "GRU-ENABLED") !== "false";
const deviceSequenceWindows = new Map();
let gruProcess = null;

app.use(express.json({ limit: "1mb" }));
app.use(express.urlencoded({ extended: true, limit: "1mb" }));
app.use(express.text({ type: "*/*", limit: "1mb" }));

function startGruService() {
  if (!gruEnabled || gruProcess) {
    return;
  }

  const pythonCommand = process.env.PYTHON || "python3";
  const args = [
    "-m",
    "uvicorn",
    "sequence_backend.service:app",
    "--host",
    "127.0.0.1",
    "--port",
    "8001",
  ];

  gruProcess = spawn(pythonCommand, args, {
    cwd: __dirname,
    env: {
      ...process.env,
      PYTHONUNBUFFERED: "1",
    },
    stdio: ["ignore", "pipe", "pipe"],
  });

  gruProcess.stdout.on("data", (chunk) => {
    process.stdout.write(`[gru] ${chunk}`);
  });

  gruProcess.stderr.on("data", (chunk) => {
    process.stderr.write(`[gru] ${chunk}`);
  });

  gruProcess.on("exit", (code, signal) => {
    console.error(`gru service exited code=${code ?? "null"} signal=${signal ?? "null"}`);
    gruProcess = null;
  });
}

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

  for (const childValue of Object.values(source)) {
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

  return deepFindValue(source, key);
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

function sanitizeGroup(group) {
  const source = group || {};

  return {
    online: toBoolean(source.online),
    temperature: toNumber(source.temperature),
    humidity: toNumber(source.humidity),
    smoke: toNumber(source.smoke),
    light: toNumber(source.light),
  };
}

function computeGroupAlarm(group) {
  if (!group.online) {
    return false;
  }

  const thresholds = computeGroupThresholds(group);
  return thresholds.fire || thresholds.gasLeak || thresholds.highTemp || thresholds.lowTemp || thresholds.highHumid;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function getCornerName(index) {
  return CORNER_NAMES[index] || `第${index + 1}角`;
}

function computeGroupThresholds(group) {
  if (!group || !group.online) {
    return {
      fire: false,
      gasLeak: false,
      highTemp: false,
      lowTemp: false,
      highHumid: false,
    };
  }

  const highTemp = Number(group.temperature || 0) >= TEMP_ALARM_THRESHOLD_C;
  const lowTemp = Number(group.temperature || 0) <= LOW_TEMP_THRESHOLD_C;
  const gasLeak = Number(group.smoke || 0) >= SMOKE_ALARM_THRESHOLD_PERCENT;
  const highHumid = Number(group.humidity || 0) >= HUMI_ALARM_THRESHOLD_RH;
  const fire = highTemp && gasLeak;

  return {
    fire,
    gasLeak,
    highTemp,
    lowTemp,
    highHumid,
  };
}

function computeInstantSeverity(group) {
  if (!group || !group.online) {
    return 0;
  }

  const temperatureSeverity = clamp((group.temperature - 30) / 50, 0, 1);
  const humiditySeverity = clamp((group.humidity - 60) / 30, 0, 1);
  const smokeSeverity = clamp((group.smoke - 12) / 58, 0, 1);
  const strongLightSeverity = clamp((group.light - 68) / 24, 0, 1);
  const lowLightSeverity = clamp((18 - group.light) / 18, 0, 1);
  const lightSeverity = Math.max(strongLightSeverity, lowLightSeverity);
  const alarmBonus = group.alarm ? 0.18 : 0;

  return clamp(
    temperatureSeverity * 0.34 +
    humiditySeverity * 0.14 +
    smokeSeverity * 0.34 +
    lightSeverity * 0.18 +
    alarmBonus,
    0,
    1,
  );
}

function buildLocationDisplayName(mode, corners) {
  const cornerNames = corners.map((corner) => getCornerName(corner - 1));

  if (!cornerNames.length) {
    return "无明确源头";
  }

  if (mode === "single") {
    return `${cornerNames[0]}疑似源头`;
  }
  if (mode === "adjacent") {
    return `${cornerNames.join(" / ")}相邻区域`;
  }
  if (mode === "uncertain") {
    return `${cornerNames.join(" / ")}接近并列`;
  }
  if (mode === "global") {
    return "全局异常";
  }
  if (mode === "multi") {
    return `${cornerNames.join(" / ")}多角区域`;
  }
  return "无明确源头";
}

function buildThresholdLocation(matchedGroups) {
  if (!Array.isArray(matchedGroups) || !matchedGroups.length) {
    return {
      mode: "clear",
      primaryCornerId: 0,
      matchedGroupIds: [],
      originDisplayName: "无明确源头",
    };
  }

  const orderedIds = matchedGroups.map((item) => item.groupId);
  let mode = "single";

  if (orderedIds.length >= SENSOR_GROUP_COUNT) {
    mode = "global";
  } else if (orderedIds.length === 2 && areAdjacentGroups(orderedIds[0], orderedIds[1])) {
    mode = "adjacent";
  } else if (orderedIds.length > 1) {
    mode = "multi";
  }

  return {
    mode,
    primaryCornerId: orderedIds[0] || 0,
    matchedGroupIds: orderedIds,
    originDisplayName: buildLocationDisplayName(mode, orderedIds),
  };
}

function computeThresholdState(groups, sequenceWindow) {
  const orderedGroups = Array.isArray(groups) ? groups : [];
  const frames = Array.isArray(sequenceWindow) ? sequenceWindow : [];

  for (const definition of THRESHOLD_STATES) {
    const matchedGroups = orderedGroups.map((group, index) => {
      const thresholds = computeGroupThresholds(group);
      if (!thresholds[definition.key]) {
        return null;
      }

      let firstHitFrame = 0;
      if (frames.length) {
        const hitIndex = frames.findIndex((frame) => {
          const metrics = frame[index] || [0, 0, 0, 0];
          return computeGroupThresholds({
            online: true,
            temperature: Number(metrics[0] || 0),
            humidity: Number(metrics[1] || 0),
            smoke: Number(metrics[2] || 0),
            light: Number(metrics[3] || 0),
          })[definition.key];
        });
        firstHitFrame = hitIndex >= 0 ? hitIndex : frames.length;
      }

      return {
        groupId: index + 1,
        firstHitFrame,
      };
    })
      .filter(Boolean)
      .sort((left, right) => {
        if (left.firstHitFrame !== right.firstHitFrame) {
          return left.firstHitFrame - right.firstHitFrame;
        }
        return left.groupId - right.groupId;
      });

    if (matchedGroups.length) {
      const location = buildThresholdLocation(matchedGroups);
      return {
        stateLabel: definition.stateLabel,
        displayName: definition.displayName,
        shortName: definition.shortName,
        priority: definition.priority,
        primaryCornerId: location.primaryCornerId,
        matchedGroupIds: location.matchedGroupIds,
        originDisplayName: location.originDisplayName,
        mode: location.mode,
      };
    }
  }

  return {
    ...THRESHOLD_NORMAL_STATE,
    primaryCornerId: 0,
    matchedGroupIds: [],
    originDisplayName: "无明确源头",
    mode: "clear",
  };
}

function roundMetric(value, digits = 4) {
  const numericValue = Number(value);
  if (!Number.isFinite(numericValue)) {
    return 0;
  }

  return Number(numericValue.toFixed(digits));
}

function summarizeSequenceWindow(sequenceWindow, groups, sequencePrediction) {
  if (!Array.isArray(sequenceWindow) || !sequenceWindow.length) {
    return null;
  }

  const firstFrame = sequenceWindow[0];
  const latestFrame = sequenceWindow[sequenceWindow.length - 1];
  const stateLabel = sequencePrediction && typeof sequencePrediction.stateLabel === "string"
    ? sequencePrediction.stateLabel
    : "";
  const sortedProbabilities = sequencePrediction && sequencePrediction.probabilities && typeof sequencePrediction.probabilities === "object"
    ? Object.entries(sequencePrediction.probabilities)
      .map(([label, probability]) => ({
        label,
        probability: roundMetric(probability),
      }))
      .sort((left, right) => right.probability - left.probability)
    : [];

  const groupTrends = groups.map((group, index) => {
    const frameSeries = sequenceWindow.map((frame) => frame[index] || [0, 0, 0, 0]);
    const first = firstFrame[index] || [0, 0, 0, 0];
    const latest = latestFrame[index] || first;
    const temperatureSeries = frameSeries.map((item) => Number(item[0] || 0));
    const humiditySeries = frameSeries.map((item) => Number(item[1] || 0));
    const smokeSeries = frameSeries.map((item) => Number(item[2] || 0));
    const lightSeries = frameSeries.map((item) => Number(item[3] || 0));

    const average = (values) => values.reduce((sum, value) => sum + value, 0) / values.length;

    return {
      groupId: index + 1,
      cornerName: getCornerName(index),
      online: !!group.online,
      alarm: !!group.alarm,
      firstFrame: {
        temperature: roundMetric(first[0], 1),
        humidity: roundMetric(first[1], 1),
        smoke: roundMetric(first[2], 1),
        light: roundMetric(first[3], 1),
      },
      latestFrame: {
        temperature: roundMetric(latest[0], 1),
        humidity: roundMetric(latest[1], 1),
        smoke: roundMetric(latest[2], 1),
        light: roundMetric(latest[3], 1),
      },
      avgTemperature: roundMetric(average(temperatureSeries), 2),
      avgHumidity: roundMetric(average(humiditySeries), 2),
      avgSmoke: roundMetric(average(smokeSeries), 2),
      avgLight: roundMetric(average(lightSeries), 2),
      maxTemperature: roundMetric(Math.max(...temperatureSeries), 1),
      maxHumidity: roundMetric(Math.max(...humiditySeries), 1),
      maxSmoke: roundMetric(Math.max(...smokeSeries), 1),
      maxLight: roundMetric(Math.max(...lightSeries), 1),
      minTemperature: roundMetric(Math.min(...temperatureSeries), 1),
      minHumidity: roundMetric(Math.min(...humiditySeries), 1),
      minSmoke: roundMetric(Math.min(...smokeSeries), 1),
      minLight: roundMetric(Math.min(...lightSeries), 1),
      temperatureRise: roundMetric(Number(latest[0] || 0) - Number(first[0] || 0), 2),
      humidityRise: roundMetric(Number(latest[1] || 0) - Number(first[1] || 0), 2),
      smokeRise: roundMetric(Number(latest[2] || 0) - Number(first[2] || 0), 2),
      lightRise: roundMetric(Number(latest[3] || 0) - Number(first[3] || 0), 2),
      instantSeverity: roundMetric(computeInstantSeverity(group)),
    };
  });

  return {
    windowLength: sequenceWindow.length,
    inferredStateLabel: stateLabel,
    probabilitiesSorted: sortedProbabilities,
    groupTrends,
  };
}

function computeLocationAnalysis(sequenceWindow, groups, sequencePrediction) {
  if (!Array.isArray(sequenceWindow) || !sequenceWindow.length) {
    return null;
  }

  const latestFrame = sequenceWindow[sequenceWindow.length - 1];
  const firstFrame = sequenceWindow[0];
  const stateLabel = sequencePrediction && typeof sequencePrediction.stateLabel === "string"
    ? sequencePrediction.stateLabel
    : "";
  const modelConfidence = sequencePrediction && Number.isFinite(sequencePrediction.confidence)
    ? Number(sequencePrediction.confidence)
    : 0;

  const cornerScores = groups.map((group, index) => {
    const current = latestFrame[index] || [0, 0, 0, 0];
    const first = firstFrame[index] || current;
    const frameSeries = sequenceWindow.map((frame) => frame[index] || current);
    const maxTemperature = Math.max(...frameSeries.map((item) => Number(item[0] || 0)));
    const maxSmoke = Math.max(...frameSeries.map((item) => Number(item[2] || 0)));
    const temperatureRise = Number(current[0] || 0) - Number(first[0] || 0);
    const smokeRise = Number(current[2] || 0) - Number(first[2] || 0);
    const humidityRise = Number(current[1] || 0) - Number(first[1] || 0);
    const lightSwing = Math.abs(Number(current[3] || 0) - Number(first[3] || 0));
    const instantSeverity = computeInstantSeverity(group);

    let score;
    if (stateLabel === "STATE_FIRE") {
      score = (
        clamp((maxTemperature - 40) / 45, 0, 1) * 0.28 +
        clamp((maxSmoke - 20) / 70, 0, 1) * 0.32 +
        clamp(temperatureRise / 24, 0, 1) * 0.18 +
        clamp(smokeRise / 36, 0, 1) * 0.16 +
        instantSeverity * 0.06
      );
    } else if (stateLabel === "STATE_GAS_LEAK") {
      score = (
        clamp((maxSmoke - 18) / 72, 0, 1) * 0.38 +
        clamp(smokeRise / 38, 0, 1) * 0.24 +
        clamp((Number(current[1] || 0) - 42) / 32, 0, 1) * 0.08 +
        clamp((Number(current[0] || 0) - 28) / 24, 0, 1) * 0.06 +
        instantSeverity * 0.24
      );
    } else if (stateLabel === "STATE_HIGH_HUMID") {
      score = (
        clamp((Math.max(...frameSeries.map((item) => Number(item[1] || 0))) - 70) / 25, 0, 1) * 0.44 +
        clamp(humidityRise / 24, 0, 1) * 0.28 +
        clamp((Number(current[0] || 0) - 26) / 18, 0, 1) * 0.06 +
        instantSeverity * 0.22
      );
    } else if (stateLabel === "STATE_NORMAL") {
      score = instantSeverity * 0.35;
    } else {
      score = (
        clamp((maxTemperature - 35) / 40, 0, 1) * 0.2 +
        clamp((maxSmoke - 18) / 68, 0, 1) * 0.24 +
        clamp(Math.abs(humidityRise) / 24, 0, 1) * 0.08 +
        clamp(lightSwing / 48, 0, 1) * 0.12 +
        instantSeverity * 0.36
      );
    }

    score = clamp(score, 0, 1);
    return {
      groupId: index + 1,
      cornerName: getCornerName(index),
      score,
      severityPercent: Math.round(score * 100),
      instantSeverity: Number(instantSeverity.toFixed(4)),
      temperatureRise: Number(temperatureRise.toFixed(2)),
      smokeRise: Number(smokeRise.toFixed(2)),
      maxTemperature: Number(maxTemperature.toFixed(1)),
      maxSmoke: Number(maxSmoke.toFixed(1)),
    };
  });

  const sortedScores = [...cornerScores].sort((left, right) => right.score - left.score);
  const top1 = sortedScores[0];
  const top2 = sortedScores[1] || null;
  const scoreValues = cornerScores.map((item) => item.score);
  const averageScore = scoreValues.reduce((sum, value) => sum + value, 0) / scoreValues.length;
  const variance = scoreValues.reduce((sum, value) => sum + ((value - averageScore) ** 2), 0) / scoreValues.length;
  const stddev = Math.sqrt(variance);
  const topGap = top2 ? top1.score - top2.score : top1.score;
  const highScoreCorners = cornerScores.filter((item) => item.score >= 0.58).map((item) => item.groupId);
  const isAdjacentPair = top2 ? areAdjacentGroups(top1.groupId, top2.groupId) : false;

  let mode = "single";
  let candidateCorners = [top1.groupId];
  let confidence = clamp(top1.score * 0.7 + topGap * 0.6 + modelConfidence * 0.2, 0, 1);

  if (stateLabel === "STATE_NORMAL" && averageScore < 0.18) {
    mode = "clear";
    candidateCorners = [];
    confidence = clamp((1 - averageScore) * 0.9, 0, 1);
  } else if (highScoreCorners.length >= 3 && stddev <= 0.11) {
    mode = "global";
    candidateCorners = highScoreCorners;
    confidence = clamp(0.55 + averageScore * 0.35 + modelConfidence * 0.15, 0, 1);
  } else if (top2 && topGap <= 0.06) {
    mode = isAdjacentPair ? "adjacent" : "uncertain";
    candidateCorners = [top1.groupId, top2.groupId];
    confidence = clamp(0.46 + (1 - topGap) * 0.18 + modelConfidence * 0.2, 0, 1);
  } else if (top2 && topGap <= 0.12 && isAdjacentPair) {
    mode = "adjacent";
    candidateCorners = [top1.groupId, top2.groupId];
    confidence = clamp(0.52 + top1.score * 0.22 + modelConfidence * 0.18, 0, 1);
  } else if (top1.score <= 0.2 && stddev <= 0.06) {
    mode = "uncertain";
    candidateCorners = [top1.groupId];
    confidence = clamp(0.35 + modelConfidence * 0.15, 0, 1);
  }

  return {
    mode,
    modeLabel: mode.toUpperCase(),
    candidateCorners,
    primaryCornerId: candidateCorners[0] || 0,
    secondaryCornerIds: candidateCorners.slice(1),
    confidence: Number(confidence.toFixed(4)),
    displayName: buildLocationDisplayName(mode, candidateCorners),
    cornerScores,
    stats: {
      averageScore: Number(averageScore.toFixed(4)),
      stddev: Number(stddev.toFixed(4)),
      topGap: Number(topGap.toFixed(4)),
    },
  };
}

function buildDeviceKey(snapshot) {
  return `${snapshot.productId || "default"}::${snapshot.deviceName || "ESP32-S3"}`;
}

function buildCompactSequencePrediction(sequencePrediction) {
  if (!sequencePrediction || typeof sequencePrediction !== "object") {
    return null;
  }

  return {
    stateLabel: typeof sequencePrediction.stateLabel === "string" ? sequencePrediction.stateLabel : "",
    displayName: typeof sequencePrediction.displayName === "string" ? sequencePrediction.displayName : "",
    confidence: Number(sequencePrediction.confidence || 0),
    originLabel: typeof sequencePrediction.originLabel === "string" ? sequencePrediction.originLabel : "",
    originDisplayName: typeof sequencePrediction.originDisplayName === "string" ? sequencePrediction.originDisplayName : "",
    originConfidence: Number(sequencePrediction.originConfidence || 0),
  };
}

function buildCompactThresholdState(thresholdState) {
  if (!thresholdState || typeof thresholdState !== "object") {
    return null;
  }

  return {
    stateLabel: typeof thresholdState.stateLabel === "string" ? thresholdState.stateLabel : THRESHOLD_NORMAL_STATE.stateLabel,
    displayName: typeof thresholdState.displayName === "string" ? thresholdState.displayName : THRESHOLD_NORMAL_STATE.displayName,
    shortName: typeof thresholdState.shortName === "string" ? thresholdState.shortName : THRESHOLD_NORMAL_STATE.shortName,
    primaryCornerId: Number(thresholdState.primaryCornerId || 0),
    originDisplayName: typeof thresholdState.originDisplayName === "string" ? thresholdState.originDisplayName : "无明确源头",
    matchedGroupIds: Array.isArray(thresholdState.matchedGroupIds) ? thresholdState.matchedGroupIds.map((item) => Number(item || 0)).filter(Boolean) : [],
    priority: Number(thresholdState.priority || 0),
    mode: typeof thresholdState.mode === "string" ? thresholdState.mode : "clear",
  };
}

function buildSequenceFrame(groups) {
  return groups.map((group) => ([
    toNumber(group.temperature),
    toNumber(group.humidity),
    toNumber(group.smoke),
    toNumber(group.light),
  ]));
}

function appendSequenceFrame(snapshot) {
  const deviceKey = buildDeviceKey(snapshot);
  const currentWindow = deviceSequenceWindows.get(deviceKey) || [];
  currentWindow.push(buildSequenceFrame(snapshot.groups));

  while (currentWindow.length > SEQUENCE_LENGTH) {
    currentWindow.shift();
  }

  deviceSequenceWindows.set(deviceKey, currentWindow);
  return currentWindow;
}

async function predictSequenceState(sequenceWindow) {
  if (!gruEnabled) {
    return null;
  }

  const response = await fetch(`${gruServiceUrl.replace(/\/$/, "")}/predict`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Accept: "application/json",
    },
    body: JSON.stringify({
      sequence: sequenceWindow,
    }),
    signal: AbortSignal.timeout(5000),
  });

  const responseBody = await response.text();
  let parsedBody = null;

  try {
    parsedBody = responseBody ? JSON.parse(responseBody) : null;
  } catch (error) {
    parsedBody = null;
  }

  if (!response.ok) {
    throw new Error(`gru predict http ${response.status}: ${responseBody}`);
  }

  if (!parsedBody || typeof parsedBody !== "object") {
    throw new Error("gru predict returned empty body");
  }

  return {
    stateLabel: parsedBody.state_label || "",
    displayName: parsedBody.display_name || "",
    confidence: Number(parsedBody.confidence || 0),
    probabilities: parsedBody.probabilities && typeof parsedBody.probabilities === "object"
      ? parsedBody.probabilities
      : {},
    originLabel: parsedBody.origin_label || "",
    originDisplayName: parsedBody.origin_display_name || "",
    originConfidence: Number(parsedBody.origin_confidence || 0),
    originProbabilities: parsedBody.origin_probabilities && typeof parsedBody.origin_probabilities === "object"
      ? parsedBody.origin_probabilities
      : {},
  };
}

async function attachSequencePrediction(snapshot) {
  const sequenceWindow = appendSequenceFrame(snapshot);

  snapshot.sequenceLength = sequenceWindow.length;
  snapshot.sequenceReady = sequenceWindow.length >= SEQUENCE_LENGTH;
  snapshot.sequencePrediction = null;
  snapshot.thresholdState = computeThresholdState(snapshot.groups, sequenceWindow);
  snapshot.systemAlarm = snapshot.thresholdState.stateLabel !== THRESHOLD_NORMAL_STATE.stateLabel;

  if (!snapshot.sequenceReady) {
    return snapshot;
  }

  try {
    snapshot.sequencePrediction = await predictSequenceState(sequenceWindow);
    const locationAnalysis = computeLocationAnalysis(sequenceWindow, snapshot.groups, snapshot.sequencePrediction);
    if (snapshot.sequencePrediction && locationAnalysis) {
      snapshot.sequencePrediction.originLabel = locationAnalysis.modeLabel;
      snapshot.sequencePrediction.originDisplayName = locationAnalysis.displayName;
      snapshot.sequencePrediction.originConfidence = locationAnalysis.confidence;
      snapshot.sequencePrediction.originMode = locationAnalysis.mode;
      snapshot.sequencePrediction.primaryCornerId = locationAnalysis.primaryCornerId;
      snapshot.sequencePrediction.secondaryCornerIds = locationAnalysis.secondaryCornerIds;
      snapshot.sequencePrediction.candidateCorners = locationAnalysis.candidateCorners;
      snapshot.sequencePrediction.cornerScores = locationAnalysis.cornerScores;
      snapshot.sequencePrediction.locationStats = locationAnalysis.stats;
    }
    if (snapshot.sequencePrediction) {
      snapshot.sequencePrediction.debug = summarizeSequenceWindow(
        sequenceWindow,
        snapshot.groups,
        snapshot.sequencePrediction,
      );
    }
  } catch (error) {
    console.error("gru predict failed", error);
  }

  return snapshot;
}

function normalizeDeviceReport(payload) {
  const source = unwrapPayload(payload);
  const nowMs = Date.now();
  const sourceGroups = Array.isArray(source.groups) ? source.groups : null;

  if (!sourceGroups || sourceGroups.length < SENSOR_GROUP_COUNT) {
    throw new Error("device report requires groups[4]");
  }

  const groups = sourceGroups.slice(0, SENSOR_GROUP_COUNT).map((group) => {
    const normalized = sanitizeGroup(group);
    return {
      ...normalized,
      alarm: computeGroupAlarm(normalized),
    };
  });

  return {
    sourceType: "device-report",
    deviceName: source.deviceName || source.device_name || "ESP32-S3",
    productId: source.productId || source.product_id || "lab-warning",
    groups,
    linkOnline: Object.prototype.hasOwnProperty.call(source, "linkOnline")
      ? toBoolean(source.linkOnline)
      : true,
    systemAlarm: groups.some((group) => group.alarm),
    sourceTimeMs: Number(source.reportedAtMs || source.reported_at_ms || nowMs),
    snapshotTimeMs: nowMs,
    snapshotTime: formatTimestamp(nowMs),
    thresholdState: null,
    sequencePrediction: null,
    sequenceReady: false,
    sequenceLength: 0,
    rawPayload: source,
  };
}

function normalizeOnenetPush(payload) {
  const source = unwrapPayload(payload);
  const nowMs = Date.now();
  const groups = [];

  for (let index = 1; index <= SENSOR_GROUP_COUNT; index += 1) {
    const normalized = sanitizeGroup({
      online: pickMetricValue(source, `g${index}_online`),
      temperature: pickMetricValue(source, `g${index}_temperature`),
      humidity: pickMetricValue(source, `g${index}_humidity`),
      smoke: pickMetricValue(source, `g${index}_smoke`),
      light: pickMetricValue(source, `g${index}_light`),
    });

    groups.push({
      ...normalized,
      alarm: computeGroupAlarm(normalized),
    });
  }

  return {
    sourceType: "onenet-push",
    deviceName: pickMetricValue(source, "deviceName") || pickMetricValue(source, "device_name") || "ESP32-S3",
    productId: pickMetricValue(source, "productId") || pickMetricValue(source, "product_id") || "",
    groups,
    linkOnline: toBoolean(pickMetricValue(source, "link_online")),
    systemAlarm: groups.some((group) => group.alarm),
    sourceTimeMs: Number(pickMetricValue(source, "timestamp") || pickMetricValue(source, "dataTimestamp") || nowMs),
    snapshotTimeMs: nowMs,
    snapshotTime: formatTimestamp(nowMs),
    thresholdState: null,
    sequencePrediction: null,
    sequenceReady: false,
    sequenceLength: 0,
    rawPayload: source,
  };
}

async function invokeIngestFunction(snapshot) {
  if (!apiBaseUrl) {
    throw new Error("missing cloudbase env id");
  }

  if (!manualApiKey) {
    throw new Error("missing cloudbase api key");
  }

  const response = await fetch(`${apiBaseUrl}/v1/functions/${encodeURIComponent(ingestFunctionName)}`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${manualApiKey}`,
      "Content-Type": "application/json",
      Accept: "application/json",
    },
    body: JSON.stringify({
      snapshot: {
        deviceName: snapshot.deviceName,
        productId: snapshot.productId,
        groups: snapshot.groups,
        linkOnline: snapshot.linkOnline,
        systemAlarm: snapshot.systemAlarm,
        thresholdState: snapshot.thresholdState,
        sequencePrediction: snapshot.sequencePrediction,
        sequenceReady: !!snapshot.sequenceReady,
        sequenceLength: Number(snapshot.sequenceLength || 0),
        sourceTimestamp: snapshot.sourceTimeMs,
        updatedAtMs: snapshot.snapshotTimeMs,
        updatedAt: snapshot.snapshotTime,
        rawPayload: snapshot.rawPayload,
      },
    }),
  });

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
    console.error("ingest function response error", JSON.stringify({
      status: response.status,
      body: result,
    }));
    throw new Error(`ingest function http ${response.status}: ${typeof result === "string" ? result : JSON.stringify(result)}`);
  }

  if (result && result.success === false) {
    throw new Error(`ingest function failed: ${result.message || "unknown error"}`);
  }

  return result;
}

function isAuthorizedDeviceRequest(req) {
  if (!deviceReportToken) {
    return true;
  }

  const token = req.header("x-device-token") || req.header("authorization")?.replace(/^Bearer\s+/i, "") || "";
  return token === deviceReportToken;
}

app.get("/healthz", (req, res) => {
  res.json({
    ok: true,
    envId,
    snapshotCollectionName,
    historyCollectionName,
    ingestFunctionName,
    apiBaseUrl,
    gruServiceUrl,
    gruEnabled,
    gruProcessRunning: !!gruProcess,
    apiKeyInjected: !!manualApiKey,
    deviceTokenInjected: !!deviceReportToken,
    mode: "device-report-http",
  });
});

app.post("/device/report", async (req, res) => {
  try {
    if (!isAuthorizedDeviceRequest(req)) {
      res.status(403).json({
        success: false,
        message: "invalid device token",
      });
      return;
    }

    const requestBody = unwrapPayload(req.body);
    console.log("device report payload shape", JSON.stringify(buildPayloadShape(requestBody)));

    const snapshot = await attachSequencePrediction(normalizeDeviceReport(requestBody));
    await invokeIngestFunction(snapshot);

    res.json({
      success: true,
      message: "device snapshot synced",
      alarm: {
        linkOnline: snapshot.linkOnline,
        systemAlarm: snapshot.systemAlarm,
        groups: snapshot.groups.map((group) => ({
          online: group.online,
          alarm: group.alarm,
        })),
      },
      thresholdState: buildCompactThresholdState(snapshot.thresholdState),
      sequencePrediction: buildCompactSequencePrediction(snapshot.sequencePrediction),
      sequenceReady: snapshot.sequenceReady,
      sequenceLength: snapshot.sequenceLength,
    });
  } catch (error) {
    console.error("device report failed", error);
    res.status(String(error.message || "").includes("requires groups[4]") ? 400 : 500).json({
      success: false,
      message: error.message || "device report failed",
    });
  }
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

    if (pushToken && receivedToken && receivedToken !== pushToken) {
      res.status(403).json({
        success: false,
        message: "invalid token",
      });
      return;
    }

    const snapshot = await attachSequencePrediction(normalizeOnenetPush(requestBody));
    await invokeIngestFunction(snapshot);

    res.json({
      success: true,
      message: "snapshot synced",
      thresholdState: buildCompactThresholdState(snapshot.thresholdState),
      sequencePrediction: buildCompactSequencePrediction(snapshot.sequencePrediction),
      sequenceReady: snapshot.sequenceReady,
      sequenceLength: snapshot.sequenceLength,
    });
  } catch (error) {
    console.error("onenet push failed", error);
    res.status(500).json({
      success: false,
      message: error.message || "sync failed",
    });
  }
});

startGruService();

app.listen(port, () => {
  console.log(`labPushReceiver listening on ${port}`);
});
