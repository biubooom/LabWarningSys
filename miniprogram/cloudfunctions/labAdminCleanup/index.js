const cloud = require("wx-server-sdk");

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV,
});

const db = cloud.database();
const _ = db.command;
const DEFAULT_SNAPSHOT_COLLECTION = "lab_snapshot";
const DELETE_CONFIRM_TOKEN = "DELETE_LAB_SNAPSHOT";
const BATCH_LIMIT = 100;

async function countDocuments(collectionName) {
  const countResult = await db.collection(collectionName).count();
  return countResult.total || 0;
}

async function deleteBatchByIds(collectionName, ids) {
  if (!Array.isArray(ids) || ids.length === 0) {
    return 0;
  }

  const removeResult = await db.collection(collectionName).where({
    _id: _.in(ids),
  }).remove();

  return removeResult.stats ? (removeResult.stats.removed || 0) : 0;
}

exports.main = async (event) => {
  const action = event && event.action ? String(event.action) : "clearSnapshot";
  const confirm = event && event.confirm ? String(event.confirm) : "";
  const collectionName = event && event.collectionName
    ? String(event.collectionName)
    : DEFAULT_SNAPSHOT_COLLECTION;

  if (action !== "clearSnapshot") {
    return {
      success: false,
      message: `unsupported action: ${action}`,
    };
  }

  const beforeCount = await countDocuments(collectionName);

  if (confirm !== DELETE_CONFIRM_TOKEN) {
    return {
      success: true,
      dryRun: true,
      collectionName,
      beforeCount,
      deletedCount: 0,
      message: `dry run only, pass confirm=${DELETE_CONFIRM_TOKEN} to delete all snapshot docs`,
    };
  }

  let deletedCount = 0;

  while (1) {
    const batch = await db.collection(collectionName)
      .field({ _id: true })
      .limit(BATCH_LIMIT)
      .get();

    const ids = (batch.data || []).map((item) => item._id).filter(Boolean);
    if (ids.length === 0) {
      break;
    }

    deletedCount += await deleteBatchByIds(collectionName, ids);
  }

  const afterCount = await countDocuments(collectionName);

  return {
    success: true,
    dryRun: false,
    collectionName,
    beforeCount,
    deletedCount,
    afterCount,
    message: "snapshot collection cleared",
  };
};
