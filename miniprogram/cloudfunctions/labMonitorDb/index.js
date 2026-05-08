const cloud = require("wx-server-sdk");

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV,
});

const db = cloud.database();
const SNAPSHOT_COLLECTION = "lab_snapshot";
const HISTORY_COLLECTION = "lab_history";
const HISTORY_LIMIT = 30;

function buildEmptySnapshot() {
  return {
    groups: [1, 2, 3, 4].map(() => ({
      online: false,
      alarm: false,
      temperature: 0,
      humidity: 0,
      smoke: 0,
      light: 0,
    })),
    linkOnline: false,
    systemAlarm: false,
    updatedAt: "--",
  };
}

function normalizeGroup(group) {
  const source = group || {};

  return {
    online: !!source.online,
    alarm: !!source.alarm,
    temperature: Number(source.temperature || 0),
    humidity: Number(source.humidity || 0),
    smoke: Number(source.smoke || 0),
    light: Number(source.light || 0),
  };
}

function normalizeSnapshot(record) {
  const base = buildEmptySnapshot();
  const groups = record.groups && typeof record.groups === "object" ? record.groups : {};

  return {
    groups: base.groups.map((fallback, index) => normalizeGroup(
      groups[`g${index + 1}`] ||
      (Array.isArray(record.groups) ? record.groups[index] : null) ||
      fallback
    )),
    linkOnline: !!record.linkOnline,
    systemAlarm: !!record.systemAlarm,
    updatedAt: record.snapshotTime || record.updatedAt || "--",
  };
}

function normalizeHistoryRecord(record) {
  const snapshot = normalizeSnapshot(record);

  return {
    groups: snapshot.groups,
    linkOnline: snapshot.linkOnline,
    systemAlarm: snapshot.systemAlarm,
    updatedAt: record.snapshotTime || record.updatedAt || "--",
    updatedAtMs: Number(record.snapshotTimeMs || record.updatedAtMs || 0),
  };
}

async function getLatestSnapshot() {
  try {
    const result = await db
      .collection(SNAPSHOT_COLLECTION)
      .orderBy("snapshotTimeMs", "desc")
      .limit(1)
      .get();

    if (!result.data.length) {
      return {
        success: true,
        data: buildEmptySnapshot(),
        message: "数据库中暂无设备快照",
      };
    }

    return {
      success: true,
      data: normalizeSnapshot(result.data[0]),
    };
  } catch (error) {
    if (String(error.message || "").includes("collection")) {
      return {
        success: true,
        data: buildEmptySnapshot(),
        message: "集合 lab_snapshot 尚未创建",
      };
    }

    throw error;
  }
}

async function getRecentHistory() {
  try {
    const result = await db
      .collection(HISTORY_COLLECTION)
      .orderBy("snapshotTimeMs", "desc")
      .limit(HISTORY_LIMIT)
      .get();

    return {
      success: true,
      data: {
        records: result.data
          .map((item) => normalizeHistoryRecord(item))
          .reverse(),
      },
    };
  } catch (error) {
    if (String(error.message || "").includes("collection")) {
      return {
        success: true,
        data: {
          records: [],
        },
        message: "集合 lab_history 尚未创建",
      };
    }

    throw error;
  }
}

exports.main = async (event) => {
  try {
    switch (event.action) {
      case "getRecentHistory":
        return await getRecentHistory();
      case "getLatestSnapshot":
      default:
        return await getLatestSnapshot();
    }
  } catch (error) {
    console.error("labMonitorDb failed", error);
    return {
      success: false,
      message: error.message || "读取数据库快照失败",
    };
  }
};
