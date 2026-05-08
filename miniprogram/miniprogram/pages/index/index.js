const DEFAULT_GROUPS = [1, 2, 3, 4].map((index) => ({
  id: index,
  title: `第${index}组`,
  online: false,
  alarm: false,
  temperature: 0,
  humidity: 0,
  smoke: 0,
  light: 0,
}));
const AUTO_REFRESH_INTERVAL_MS = 3000;

Page({
  data: {
    groups: DEFAULT_GROUPS,
    linkOnline: false,
    systemAlarm: false,
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

        const nextState = {
          groups: this.normalizeGroups(result.data.groups),
          linkOnline: !!result.data.linkOnline,
          systemAlarm: !!result.data.systemAlarm,
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

      return {
        id: fallbackGroup.id,
        title: fallbackGroup.title,
        online: !!source.online,
        alarm: !!source.alarm,
        temperature: this.formatNumber(source.temperature),
        humidity: this.formatNumber(source.humidity),
        smoke: this.formatNumber(source.smoke),
        light: this.formatNumber(source.light),
      };
    });
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
