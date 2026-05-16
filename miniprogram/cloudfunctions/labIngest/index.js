const cloud = require("wx-server-sdk");

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV,
});

const db = cloud.database();
const _ = db.command;
const SNAPSHOT_COLLECTION = "lab_snapshot";
const HISTORY_COLLECTION = "lab_history";
const HISTORY_MAX_RECORDS = 8000;
const HISTORY_DELETE_BATCH_LIMIT = 100;

function sanitizeDocId(value) {
  return String(value || "default").replace(/[^a-zA-Z0-9_-]/g, "_");
}

async function trimHistoryCollection() {
  const countResult = await db.collection(HISTORY_COLLECTION).count();
  const total = countResult.total || 0;
  const overflow = total - HISTORY_MAX_RECORDS;

  if (overflow <= 0) {
    return 0;
  }

  let deletedCount = 0;
  let remainingOverflow = overflow;

  while (remainingOverflow > 0) {
    const batchSize = Math.min(remainingOverflow, HISTORY_DELETE_BATCH_LIMIT);
    const oldestResult = await db.collection(HISTORY_COLLECTION)
      .orderBy("snapshotTimeMs", "asc")
      .limit(batchSize)
      .field({ _id: true })
      .get();

    const ids = (oldestResult.data || []).map((item) => item._id).filter(Boolean);
    if (!ids.length) {
      break;
    }

    const removeResult = await db.collection(HISTORY_COLLECTION).where({
      _id: _.in(ids),
    }).remove();

    const removed = removeResult.stats ? (removeResult.stats.removed || 0) : 0;
    deletedCount += removed;
    remainingOverflow -= ids.length;

    if (removed === 0) {
      break;
    }
  }

  return deletedCount;
}

exports.main = async (event) => {
  try {
    const snapshot = event && event.snapshot ? event.snapshot : null;

    if (!snapshot || !snapshot.deviceName) {
      return {
        success: false,
        message: "missing snapshot payload",
      };
    }

    const snapshotDocId = `${sanitizeDocId(snapshot.productId || "default")}_${sanitizeDocId(snapshot.deviceName)}`;
    const snapshotDoc = {
      deviceName: snapshot.deviceName,
      productId: snapshot.productId || "",
      groups: Array.isArray(snapshot.groups) ? snapshot.groups : [],
      linkOnline: !!snapshot.linkOnline,
      systemAlarm: !!snapshot.systemAlarm,
      thresholdState: snapshot.thresholdState || null,
      sequencePrediction: snapshot.sequencePrediction || null,
      sequenceReady: !!snapshot.sequenceReady,
      sequenceLength: Number(snapshot.sequenceLength || 0),
      sourceTimestamp: Number(snapshot.sourceTimestamp || Date.now()),
      snapshotTimeMs: Number(snapshot.updatedAtMs || Date.now()),
      snapshotTime: snapshot.updatedAt || "",
      updatedAtMs: Number(snapshot.updatedAtMs || Date.now()),
      updatedAt: snapshot.updatedAt || "",
      rawPayload: snapshot.rawPayload || {},
    };

    await db.collection(SNAPSHOT_COLLECTION).doc(snapshotDocId).set({
      data: snapshotDoc,
    });

    await db.collection(HISTORY_COLLECTION).add({
      data: {
        deviceName: snapshotDoc.deviceName,
        productId: snapshotDoc.productId,
        groups: snapshotDoc.groups,
        linkOnline: snapshotDoc.linkOnline,
        systemAlarm: snapshotDoc.systemAlarm,
        thresholdState: snapshotDoc.thresholdState,
        sequencePrediction: snapshotDoc.sequencePrediction,
        sequenceReady: snapshotDoc.sequenceReady,
        sequenceLength: snapshotDoc.sequenceLength,
        sourceTimestamp: snapshotDoc.sourceTimestamp,
        snapshotTimeMs: snapshotDoc.snapshotTimeMs,
        snapshotTime: snapshotDoc.snapshotTime,
        updatedAtMs: snapshotDoc.updatedAtMs,
        updatedAt: snapshotDoc.updatedAt,
      },
    });

    const trimmedCount = await trimHistoryCollection();

    return {
      success: true,
      message: "snapshot stored",
      historyMaxRecords: HISTORY_MAX_RECORDS,
      trimmedCount,
    };
  } catch (error) {
    console.error("labIngest failed", error);
    return {
      success: false,
      message: error.message || "ingest failed",
    };
  }
};
