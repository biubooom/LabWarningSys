const CORNER_NAMES = ["东北角", "西北角", "东南角", "西南角"];
const CORNER_POSITION_CLASSES = ["corner--ne", "corner--nw", "corner--se", "corner--sw"];
const DEFAULT_GROUPS = [1, 2, 3, 4].map((index) => ({
  id: index,
  title: `第${index}组`,
  cornerName: CORNER_NAMES[index - 1],
  positionClass: CORNER_POSITION_CLASSES[index - 1],
  online: false,
  alarm: false,
  temperature: 0,
  humidity: 0,
  smoke: 0,
  light: 0,
  severityScore: 0,
  severityPercent: 0,
  severityLabel: "平稳",
  heatOpacity: 0.18,
  overlayStyle: "",
}));
const AUTO_REFRESH_INTERVAL_MS = 3000;
const DOMINANT_GROUP_MIN_SCORE = 0.3;
const TEMP_THRESHOLD_C = 45;
const LOW_TEMP_THRESHOLD_C = 15;
const HUMI_THRESHOLD_RH = 80;
const SMOKE_THRESHOLD_PERCENT = 20;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

Page({
  data: {
    groups: DEFAULT_GROUPS,
    dominantGroup: null,
    dominantGroupDisplayName: "",
    linkOnline: false,
    systemAlarm: false,
    thresholdState: {
      stateLabel: "THRESHOLD_NORMAL",
      displayName: "正常",
      shortName: "NORMAL",
      primaryCornerId: 0,
      originDisplayName: "无明确源头",
      matchedGroupIds: [],
      priority: 0,
      mode: "clear",
    },
    sequenceReady: false,
    sequenceLength: 0,
    sequencePrediction: null,
    updatedAt: "--",
    loading: false,
    errorMessage: "",
  },

  onShow() {
    this.startAutoRefresh();
    this.loadSnapshot();
  },

  onHide() {
    this.stopAutoRefresh();
  },

  onUnload() {
    this.stopAutoRefresh();
  },

  onPullDownRefresh() {
    this.loadSnapshot({ silent: true });
  },

  startAutoRefresh() {
    this.stopAutoRefresh();
    this.autoRefreshTimer = setInterval(() => {
      this.loadSnapshot({ silent: true });
    }, AUTO_REFRESH_INTERVAL_MS);
  },

  stopAutoRefresh() {
    if (this.autoRefreshTimer) {
      clearInterval(this.autoRefreshTimer);
      this.autoRefreshTimer = null;
    }
  },

  loadSnapshot(options = {}) {
    const { silent = false } = options;
    const app = getApp();

    if (!silent) {
      this.setData({
        loading: true,
        errorMessage: "",
      });
    }

    if (!app.globalData.env) {
      this.setData({
        loading: false,
        errorMessage: "请先在 miniprogram/app.js 中填写云开发环境 ID。",
      });
      wx.stopPullDownRefresh();
      return;
    }

    wx.cloud.callFunction({
        name: app.globalData.cloudFunctionName || "labMonitorDb",
        data: {
          action: "getLatestSnapshot",
        },
      })
      .then((response) => {
        const result = response.result || {};

        if (!result.success || !result.data) {
          throw new Error(result.message || "云函数未返回有效数据");
        }

        const normalizedGroups = this.applySequenceLocation(
          this.normalizeGroups(result.data.groups),
          this.normalizeSequencePrediction(result.data.sequencePrediction),
        );
        const sequencePrediction = this.normalizeSequencePrediction(result.data.sequencePrediction);
        const thresholdState = this.normalizeThresholdState(result.data.thresholdState, normalizedGroups);
        const dominantGroup = this.findDominantGroup(normalizedGroups);
        const shouldShowDominantGroup = this.shouldShowDominantGroup(
          dominantGroup,
          normalizedGroups,
          sequencePrediction,
          thresholdState,
        );
        const nextState = {
          groups: normalizedGroups.map((group) => ({
            ...group,
            isDominant: shouldShowDominantGroup && !!dominantGroup && group.id === dominantGroup.id,
          })),
          dominantGroup: shouldShowDominantGroup ? dominantGroup : null,
          dominantGroupDisplayName: shouldShowDominantGroup
            ? this.buildDominantGroupDisplayName(dominantGroup, sequencePrediction, thresholdState)
            : "",
          linkOnline: !!result.data.linkOnline,
          systemAlarm: thresholdState ? thresholdState.stateLabel !== "THRESHOLD_NORMAL" : !!result.data.systemAlarm,
          thresholdState,
          sequenceReady: !!result.data.sequenceReady,
          sequenceLength: Number(result.data.sequenceLength || 0),
          sequencePrediction,
          updatedAt: result.data.updatedAt || "--",
          errorMessage: "",
        };

        const nextSignature = JSON.stringify(nextState);

        if (this.lastSnapshotSignature === nextSignature) {
          return;
        }

        this.lastSnapshotSignature = nextSignature;

        this.setData(nextState);
      })
      .catch((error) => {
        console.error("loadSnapshot failed", error);
        this.lastSnapshotSignature = "";
        this.setData({
          errorMessage: error.message || "获取设备数据失败，请稍后重试。",
        });
      })
      .finally(() => {
        this.setData({
          loading: false,
        });
        wx.stopPullDownRefresh();
      });
  },

  normalizeGroups(groups) {
    return DEFAULT_GROUPS.map((fallbackGroup, index) => {
      const source = Array.isArray(groups) ? groups[index] || {} : {};
      const online = !!source.online;
      const temperature = this.formatNumber(source.temperature);
      const humidity = this.formatNumber(source.humidity);
      const smoke = this.formatNumber(source.smoke);
      const light = this.formatNumber(source.light);
      const alarm = !!source.alarm;
      const severityScore = this.computeSeverityScore({
        online,
        alarm,
        temperature,
        humidity,
        smoke,
        light,
      });

      return {
        id: fallbackGroup.id,
        title: fallbackGroup.title,
        cornerName: fallbackGroup.cornerName,
        positionClass: fallbackGroup.positionClass,
        online,
        alarm,
        temperature,
        humidity,
        smoke,
        light,
        severityScore,
        severityPercent: Math.round(severityScore * 100),
        severityLabel: this.buildSeverityLabel(severityScore, online),
        heatOpacity: this.buildHeatOpacity(severityScore, online),
        overlayStyle: this.buildOverlayStyle(severityScore, online),
        isDominant: false,
      };
    });
  },

  applySequenceLocation(groups, sequencePrediction) {
    if (!sequencePrediction || !Array.isArray(sequencePrediction.cornerScores) || !sequencePrediction.cornerScores.length) {
      return groups;
    }

    const scoreMap = new Map(
      sequencePrediction.cornerScores.map((item) => [Number(item.groupId), item])
    );

    return groups.map((group) => {
      const locationScore = scoreMap.get(group.id);
      if (!locationScore) {
        return group;
      }

      const severityScore = this.formatSeverityScore(locationScore.score);
      return {
        ...group,
        severityScore,
        severityPercent: Math.round(severityScore * 100),
        severityLabel: this.buildSeverityLabel(severityScore, group.online),
        heatOpacity: this.buildHeatOpacity(severityScore, group.online),
        overlayStyle: this.buildOverlayStyle(severityScore, group.online),
      };
    });
  },

  computeSeverityScore(group) {
    if (!group.online) {
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
      1
    );
  },

  buildSeverityLabel(score, online) {
    if (!online) {
      return "离线";
    }
    if (score >= 0.78) {
      return "高危";
    }
    if (score >= 0.55) {
      return "显著异常";
    }
    if (score >= 0.3) {
      return "轻度异常";
    }
    return "平稳";
  },

  buildHeatOpacity(score, online) {
    if (!online) {
      return 0.12;
    }
    return Number((0.18 + score * 0.72).toFixed(2));
  },

  buildOverlayStyle(score, online) {
    const opacity = this.buildHeatOpacity(score, online);
    return [
      "background: radial-gradient(circle, rgba(203, 25, 18, 0.98) 0%, rgba(255, 94, 28, 0.82) 34%, rgba(255, 171, 61, 0.5) 58%, rgba(255, 233, 158, 0.22) 72%, rgba(255, 255, 255, 0) 100%)",
      `opacity: ${opacity}`,
    ].join(";");
  },

  formatSeverityScore(value) {
    const numericValue = Number(value);
    if (Number.isNaN(numericValue)) {
      return 0;
    }
    return Number(clamp(numericValue, 0, 1).toFixed(4));
  },

  findDominantGroup(groups) {
    if (!Array.isArray(groups) || !groups.length) {
      return null;
    }

    const onlineGroups = groups.filter((group) => group.online);
    if (!onlineGroups.length) {
      return null;
    }

    return onlineGroups.reduce((currentMax, group) => (
      group.severityScore > currentMax.severityScore ? group : currentMax
    ));
  },

  shouldShowDominantGroup(dominantGroup, groups, sequencePrediction, thresholdState) {
    if (!dominantGroup) {
      return false;
    }

    if (thresholdState && thresholdState.stateLabel && thresholdState.stateLabel !== "THRESHOLD_NORMAL") {
      return true;
    }

    const originMode = sequencePrediction && typeof sequencePrediction.originMode === "string"
      ? sequencePrediction.originMode
      : "";
    const stateLabel = sequencePrediction && typeof sequencePrediction.stateLabel === "string"
      ? sequencePrediction.stateLabel
      : "";
    const hasAlarmGroup = Array.isArray(groups) && groups.some((group) => group.alarm);

    if (originMode === "clear") {
      return false;
    }

    if (["single", "adjacent", "uncertain", "global"].includes(originMode)) {
      return true;
    }

    if (stateLabel === "STATE_NORMAL" && !hasAlarmGroup && dominantGroup.severityScore < DOMINANT_GROUP_MIN_SCORE) {
      return false;
    }

    return dominantGroup.severityScore >= DOMINANT_GROUP_MIN_SCORE || hasAlarmGroup;
  },

  buildDominantGroupDisplayName(dominantGroup, sequencePrediction, thresholdState) {
    if (!dominantGroup) {
      return "";
    }

    if (
      thresholdState &&
      thresholdState.stateLabel &&
      thresholdState.stateLabel !== "THRESHOLD_NORMAL" &&
      typeof thresholdState.originDisplayName === "string" &&
      thresholdState.originDisplayName
    ) {
      return thresholdState.originDisplayName;
    }

    if (
      sequencePrediction &&
      typeof sequencePrediction.originDisplayName === "string" &&
      sequencePrediction.originDisplayName &&
      sequencePrediction.originMode &&
      sequencePrediction.originMode !== "clear"
    ) {
      return sequencePrediction.originDisplayName;
    }

    return dominantGroup.cornerName;
  },

  normalizeThresholdState(thresholdState) {
    const fallbackGroups = arguments[1];
    const derivedState = this.deriveThresholdStateFromGroups(fallbackGroups);
    if (!thresholdState || typeof thresholdState !== "object") {
      return derivedState;
    }

    const normalized = {
      stateLabel: thresholdState.stateLabel || thresholdState.state_label || "THRESHOLD_NORMAL",
      displayName: thresholdState.displayName || thresholdState.display_name || "正常",
      shortName: thresholdState.shortName || thresholdState.short_name || "NORMAL",
      primaryCornerId: Number(thresholdState.primaryCornerId || thresholdState.primary_corner_id || 0),
      originDisplayName: thresholdState.originDisplayName || thresholdState.origin_display_name || "无明确源头",
      matchedGroupIds: Array.isArray(thresholdState.matchedGroupIds)
        ? thresholdState.matchedGroupIds
        : (Array.isArray(thresholdState.matched_group_ids) ? thresholdState.matched_group_ids : []),
      priority: Number(thresholdState.priority || 0),
      mode: thresholdState.mode || "clear",
    };

    if (
      derivedState.stateLabel !== "THRESHOLD_NORMAL" &&
      normalized.stateLabel === "THRESHOLD_NORMAL"
    ) {
      return derivedState;
    }

    return normalized;
  },

  deriveThresholdStateFromGroups(groups) {
    const safeGroups = Array.isArray(groups) ? groups : [];
    const definitions = [
      { stateLabel: "THRESHOLD_FIRE", displayName: "火灾", shortName: "FIRE", key: "fire", priority: 5 },
      { stateLabel: "THRESHOLD_GAS_LEAK", displayName: "气体泄漏", shortName: "GAS", key: "gasLeak", priority: 4 },
      { stateLabel: "THRESHOLD_HIGH_TEMP", displayName: "高温", shortName: "HOT", key: "highTemp", priority: 3 },
      { stateLabel: "THRESHOLD_LOW_TEMP", displayName: "低温", shortName: "COLD", key: "lowTemp", priority: 2 },
      { stateLabel: "THRESHOLD_HIGH_HUMID", displayName: "高湿", shortName: "HUMID", key: "highHumid", priority: 1 },
    ];

    const evaluate = (group) => {
      const highTemp = group.online && Number(group.temperature || 0) >= TEMP_THRESHOLD_C;
      const lowTemp = group.online && Number(group.temperature || 0) <= LOW_TEMP_THRESHOLD_C;
      const gasLeak = group.online && Number(group.smoke || 0) >= SMOKE_THRESHOLD_PERCENT;
      const highHumid = group.online && Number(group.humidity || 0) >= HUMI_THRESHOLD_RH;
      return {
        fire: highTemp && gasLeak,
        gasLeak,
        highTemp,
        lowTemp,
        highHumid,
      };
    };

    const buildOriginDisplayName = (ids) => {
      const corners = ids.map((groupId) => {
        const matched = safeGroups.find((group) => group.id === groupId);
        return matched ? matched.cornerName : `第${groupId}角`;
      });
      if (!corners.length) {
        return "无明确源头";
      }
      if (corners.length === 1) {
        return `${corners[0]}疑似源头`;
      }
      return `${corners.join(" / ")}多角区域`;
    };

    for (const definition of definitions) {
      const matchedGroupIds = safeGroups
        .filter((group) => evaluate(group)[definition.key])
        .map((group) => group.id);

      if (matchedGroupIds.length) {
        return {
          stateLabel: definition.stateLabel,
          displayName: definition.displayName,
          shortName: definition.shortName,
          primaryCornerId: matchedGroupIds[0] || 0,
          originDisplayName: buildOriginDisplayName(matchedGroupIds),
          matchedGroupIds,
          priority: definition.priority,
          mode: matchedGroupIds.length > 1 ? "multi" : "single",
        };
      }
    }

    return {
      stateLabel: "THRESHOLD_NORMAL",
      displayName: "正常",
      shortName: "NORMAL",
      primaryCornerId: 0,
      originDisplayName: "无明确源头",
      matchedGroupIds: [],
      priority: 0,
      mode: "clear",
    };
  },

  normalizeSequencePrediction(sequencePrediction) {
    if (!sequencePrediction || typeof sequencePrediction !== "object") {
      return null;
    }

    const confidence = Number(sequencePrediction.confidence || 0);
    const debug = sequencePrediction.debug && typeof sequencePrediction.debug === "object"
      ? sequencePrediction.debug
      : null;

    return {
      stateLabel: sequencePrediction.stateLabel || sequencePrediction.state_label || "",
      displayName: sequencePrediction.displayName || sequencePrediction.display_name || "未识别",
      confidence: Number.isNaN(confidence) ? 0 : Number(confidence.toFixed(4)),
      originLabel: sequencePrediction.originLabel || sequencePrediction.origin_label || "",
      originDisplayName: sequencePrediction.originDisplayName || sequencePrediction.origin_display_name || "无明确源头",
      originConfidence: Number.isNaN(Number(sequencePrediction.originConfidence || sequencePrediction.origin_confidence || 0))
        ? 0
        : Number(Number(sequencePrediction.originConfidence || sequencePrediction.origin_confidence || 0).toFixed(4)),
      originMode: sequencePrediction.originMode || sequencePrediction.origin_mode || "",
      primaryCornerId: Number(sequencePrediction.primaryCornerId || sequencePrediction.primary_corner_id || 0),
      secondaryCornerIds: Array.isArray(sequencePrediction.secondaryCornerIds)
        ? sequencePrediction.secondaryCornerIds
        : (Array.isArray(sequencePrediction.secondary_corner_ids) ? sequencePrediction.secondary_corner_ids : []),
      candidateCorners: Array.isArray(sequencePrediction.candidateCorners)
        ? sequencePrediction.candidateCorners
        : (Array.isArray(sequencePrediction.candidate_corners) ? sequencePrediction.candidate_corners : []),
      cornerScores: Array.isArray(sequencePrediction.cornerScores)
        ? sequencePrediction.cornerScores.map((item) => ({
          groupId: Number(item.groupId || item.group_id || 0),
          score: this.formatSeverityScore(item.score),
          cornerName: item.cornerName || item.corner_name || "",
        }))
        : [],
      debug: debug ? this.normalizePredictionDebug(debug) : null,
    };
  },

  normalizePredictionDebug(debug) {
    return {
      windowLength: Number(debug.windowLength || 0),
      probabilitiesSorted: Array.isArray(debug.probabilitiesSorted)
        ? debug.probabilitiesSorted.map((item) => ({
          label: item.label || "",
          probability: this.formatSeverityScore(item.probability),
        }))
        : [],
      groupTrends: Array.isArray(debug.groupTrends)
        ? debug.groupTrends.map((item) => ({
          groupId: Number(item.groupId || 0),
          cornerName: item.cornerName || "",
          online: !!item.online,
          alarm: !!item.alarm,
          firstFrame: this.normalizeDebugFrame(item.firstFrame),
          latestFrame: this.normalizeDebugFrame(item.latestFrame),
          avgTemperature: this.formatNumber(item.avgTemperature),
          avgHumidity: this.formatNumber(item.avgHumidity),
          avgSmoke: this.formatNumber(item.avgSmoke),
          avgLight: this.formatNumber(item.avgLight),
          maxTemperature: this.formatNumber(item.maxTemperature),
          maxHumidity: this.formatNumber(item.maxHumidity),
          maxSmoke: this.formatNumber(item.maxSmoke),
          maxLight: this.formatNumber(item.maxLight),
          temperatureRise: this.formatNumber(item.temperatureRise),
          humidityRise: this.formatNumber(item.humidityRise),
          smokeRise: this.formatNumber(item.smokeRise),
          lightRise: this.formatNumber(item.lightRise),
          instantSeverity: this.formatSeverityScore(item.instantSeverity),
        }))
        : [],
    };
  },

  normalizeDebugFrame(frame) {
    const source = frame && typeof frame === "object" ? frame : {};
    return {
      temperature: this.formatNumber(source.temperature),
      humidity: this.formatNumber(source.humidity),
      smoke: this.formatNumber(source.smoke),
      light: this.formatNumber(source.light),
    };
  },

  formatNumber(value) {
    const numericValue = Number(value);

    if (Number.isNaN(numericValue)) {
      return 0;
    }

    return Number(numericValue.toFixed(1));
  },

  showAbout() {
    const app = getApp();

    wx.showModal({
      title: "关于小程序",
      content: `实验室预警系统小程序 V1\n云环境：${app.globalData.env || "未配置"}\n数据来源：微信云数据库`,
      showCancel: false,
    });
  },

  openHistory() {
    wx.navigateTo({
      url: "/pages/history/history",
    });
  },
});
