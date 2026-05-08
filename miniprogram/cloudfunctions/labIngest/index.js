const cloud = require("wx-server-sdk");

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV,
});

const db = cloud.database();
const SNAPSHOT_COLLECTION = "lab_snapshot";
const HISTORY_COLLECTION = "lab_history";

function sanitizeDocId(value) {
  return String(value || "default").replace(/[^a-zA-Z0-9_-]/g, "_");
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
      sourceTimestamp: Number(snapshot.sourceTimestamp || Date.now()),
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
        sourceTimestamp: snapshotDoc.sourceTimestamp,
        updatedAtMs: snapshotDoc.updatedAtMs,
        updatedAt: snapshotDoc.updatedAt,
      },
    });

    return {
      success: true,
      message: "snapshot stored",
    };
  } catch (error) {
    console.error("labIngest failed", error);
    return {
      success: false,
      message: error.message || "ingest failed",
    };
  }
};
