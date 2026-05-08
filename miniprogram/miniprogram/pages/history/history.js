const HISTORY_COLLECTION = "lab_history";
const HISTORY_LIMIT = 30;

const GROUP_OPTIONS = [1, 2, 3, 4].map((index) => ({
  id: index,
  label: `第${index}组`,
  key: `g${index}`,
}));

const METRIC_OPTIONS = [
  { key: "temperature", label: "温度", unit: "℃", color: "#cf5f3c" },
  { key: "humidity", label: "湿度", unit: "%RH", color: "#2f7ea1" },
  { key: "smoke", label: "烟雾", unit: "%", color: "#8e5c3d" },
  { key: "light", label: "光照", unit: "%", color: "#d5a11e" },
];

Page({
  data: {
    loading: false,
    errorMessage: "",
    realtimeStatus: "",
    groupOptions: GROUP_OPTIONS,
    metricOptions: METRIC_OPTIONS,
    activeGroupId: 1,
    activeMetricKey: "temperature",
    chartSummary: "--",
    chartUnit: "℃",
    records: [],
  },

  chartSize: null,

  onShow() {
    this.startHistoryWatch();
  },

  onHide() {
    this.stopHistoryWatch();
  },

  onUnload() {
    this.stopHistoryWatch();
  },

  onPullDownRefresh() {
    this.loadHistory(true);
  },

  startHistoryWatch() {
    if (this.historyWatcher) {
      return;
    }

    this.setData({
      loading: true,
      errorMessage: "",
      realtimeStatus: "实时同步中",
    });

    const db = wx.cloud.database();

    this.historyWatcher = db
      .collection(HISTORY_COLLECTION)
      .orderBy("snapshotTimeMs", "desc")
      .limit(HISTORY_LIMIT)
      .watch({
        onChange: (snapshot) => {
          const docs = Array.isArray(snapshot.docs) ? snapshot.docs : [];
          const records = docs
            .map((record) => this.normalizeHistoryRecord(record))
            .sort((lhs, rhs) => lhs.updatedAtMs - rhs.updatedAtMs);

          this.applyRecords(records, {
            emptyMessage: "暂无历史记录，请等待设备上报后再查看。",
            realtimeStatus: "实时同步中",
            clearError: true,
            stopLoading: true,
          });
          wx.stopPullDownRefresh();
        },
        onError: (error) => {
          console.error("history watch failed", error);
          this.stopHistoryWatch();
          this.setData({
            realtimeStatus: "监听断开，已回退手动刷新",
          });
          this.loadHistory(false, {
            fallbackMessage: "实时监听失败，已回退到手动刷新。",
          });
        },
      });
  },

  stopHistoryWatch() {
    if (this.historyWatcher) {
      this.historyWatcher.close();
      this.historyWatcher = null;
    }
  },

  loadHistory(silent = false, options = {}) {
    const { fallbackMessage = "" } = options;
    const app = getApp();

    if (!silent) {
      this.setData({
        loading: true,
        errorMessage: "",
      });
    }

    wx.cloud.callFunction({
      name: app.globalData.cloudFunctionName || "labMonitorDb",
      data: {
        action: "getRecentHistory",
      },
    }).then((response) => {
      const result = response.result || {};

      if (!result.success || !result.data) {
        throw new Error(result.message || "云函数未返回历史数据");
      }

      const records = Array.isArray(result.data.records)
        ? result.data.records.map((record) => this.normalizeHistoryRecord(record))
        : [];

      this.applyRecords(records, {
        emptyMessage: "暂无历史记录，请等待设备上报后再查看。",
        realtimeStatus: this.historyWatcher ? "实时同步中" : "监听断开，已回退手动刷新",
        fallbackMessage,
        clearError: !fallbackMessage,
        stopLoading: true,
      });
    }).catch((error) => {
      console.error("loadHistory failed", error);
      this.lastHistorySignature = "";
      this.setData({
        errorMessage: error.message || "读取历史记录失败",
      });
    }).finally(() => {
      this.setData({
        loading: false,
      });
      wx.stopPullDownRefresh();
    });
  },

  applyRecords(records, options = {}) {
    const {
      emptyMessage = "",
      realtimeStatus = this.data.realtimeStatus,
      fallbackMessage = "",
      clearError = false,
      stopLoading = false,
    } = options;
    const nextRecords = Array.isArray(records) ? records : [];
    const nextSignature = JSON.stringify(nextRecords);
    const nextErrorMessage = fallbackMessage || (nextRecords.length ? "" : emptyMessage);

    if (this.lastHistorySignature === nextSignature) {
      const nextState = {
        realtimeStatus,
        errorMessage: clearError ? nextErrorMessage : (nextErrorMessage || this.data.errorMessage),
      };

      if (stopLoading) {
        nextState.loading = false;
      }

      this.setData(nextState);
      return;
    }

    this.lastHistorySignature = nextSignature;

    const nextState = {
      records: nextRecords,
      realtimeStatus,
      errorMessage: clearError ? nextErrorMessage : (nextErrorMessage || this.data.errorMessage),
    };

    if (stopLoading) {
      nextState.loading = false;
    }

    this.setData(nextState, () => {
      this.renderChart();
    });
  },

  normalizeHistoryRecord(record) {
    const groups = this.normalizeGroups(record && record.groups);

    return {
      groups,
      updatedAt: record && (record.snapshotTime || record.updatedAt) ? (record.snapshotTime || record.updatedAt) : "--",
      updatedAtMs: Number(
        (record && (record.snapshotTimeMs || record.updatedAtMs)) || 0
      ),
    };
  },

  normalizeGroups(groups) {
    return [1, 2, 3, 4].map((index) => {
      const source = Array.isArray(groups)
        ? groups[index - 1] || {}
        : (groups && groups[`g${index}`]) || {};

      return {
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

  getChartBounds(values) {
    const minValue = Math.min(...values);
    const maxValue = Math.max(...values);
    const rawRange = maxValue - minValue;

    if (rawRange === 0) {
      const padding = Math.max(Math.abs(maxValue) * 0.05, 1);

      return {
        min: minValue - padding,
        max: maxValue + padding,
      };
    }

    const padding = Math.max(rawRange * 0.2, 0.5);

    return {
      min: minValue - padding,
      max: maxValue + padding,
    };
  },

  selectGroup(event) {
    const groupId = Number(event.currentTarget.dataset.id || 1);

    this.setData({
      activeGroupId: groupId,
    }, () => {
      this.renderChart();
    });
  },

  selectMetric(event) {
    const metricKey = event.currentTarget.dataset.key || "temperature";

    this.setData({
      activeMetricKey: metricKey,
    }, () => {
      this.renderChart();
    });
  },

  renderChart() {
    const { records, activeGroupId, activeMetricKey, metricOptions } = this.data;
    const metricOption = metricOptions.find((item) => item.key === activeMetricKey) || metricOptions[0];
    const points = records.map((record) => {
      const group = Array.isArray(record.groups) ? record.groups[activeGroupId - 1] || {} : {};

      return {
        label: String(record.updatedAt || "--").slice(11, 16),
        value: Number(group[activeMetricKey] || 0),
      };
    });

    if (!points.length) {
      this.setData({
        chartSummary: "--",
        chartUnit: metricOption.unit,
      });
      this.clearCanvas();
      return;
    }

    this.setData({
      chartSummary: `${points[points.length - 1].value}`,
      chartUnit: metricOption.unit,
    });

    this.getChartSize().then(({ width, height }) => {
      const ctx = wx.createCanvasContext("historyChart", this);
      const paddingLeft = Math.max(42, width * 0.07);
      const paddingRight = Math.max(16, width * 0.03);
      const paddingTop = 22;
      const paddingBottom = 42;
      const chartWidth = width - paddingLeft - paddingRight;
      const chartHeight = height - paddingTop - paddingBottom;
      const values = points.map((item) => item.value);
      const bounds = this.getChartBounds(values);
      const minValue = bounds.min;
      const maxValue = bounds.max;
      const range = maxValue - minValue;
      const actualMin = Math.min(...values);
      const actualMax = Math.max(...values);

      ctx.clearRect(0, 0, width, height);
      ctx.setFillStyle("#ffffff");
      ctx.fillRect(0, 0, width, height);

      ctx.setStrokeStyle("rgba(23, 49, 59, 0.08)");
      ctx.setLineWidth(1);
      for (let i = 0; i < 4; i += 1) {
        const y = paddingTop + (chartHeight / 3) * i;
        ctx.beginPath();
        ctx.moveTo(paddingLeft, y);
        ctx.lineTo(width - paddingRight, y);
        ctx.stroke();
      }

      const toX = (index) => {
        if (points.length === 1) {
          return paddingLeft + chartWidth / 2;
        }

        return paddingLeft + (chartWidth / (points.length - 1)) * index;
      };

      const toY = (value) => {
        const ratio = (value - minValue) / range;
        return paddingTop + chartHeight - ratio * chartHeight;
      };

      ctx.setStrokeStyle(metricOption.color);
      ctx.setLineWidth(3);
      ctx.setLineJoin("round");
      ctx.setLineCap("round");
      ctx.beginPath();
      points.forEach((point, index) => {
        const x = toX(index);
        const y = toY(point.value);

        if (index === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      ctx.stroke();

      ctx.setFillStyle(metricOption.color);
      points.forEach((point, index) => {
        const x = toX(index);
        const y = toY(point.value);
        ctx.beginPath();
        ctx.arc(x, y, 3.5, 0, Math.PI * 2);
        ctx.fill();
      });

      ctx.setFillStyle("#6d7f86");
      ctx.setFontSize(13);
      ctx.setTextAlign("left");
      ctx.fillText(`${actualMax.toFixed(1)}${metricOption.unit}`, 6, paddingTop + 6);
      ctx.fillText(`${actualMin.toFixed(1)}${metricOption.unit}`, 6, paddingTop + chartHeight + 2);

      ctx.setTextAlign("center");
      const labelIndexes = points.length <= 6
        ? points.map((_, index) => index)
        : [0, Math.floor((points.length - 1) / 2), points.length - 1];

      labelIndexes.forEach((index) => {
        const point = points[index];
        ctx.fillText(point.label, toX(index), height - 12);
      });

      ctx.draw();
    });
  },

  clearCanvas() {
    this.getChartSize().then(({ width, height }) => {
      const ctx = wx.createCanvasContext("historyChart", this);
      ctx.clearRect(0, 0, width, height);
      ctx.draw();
    });
  },

  getChartSize() {
    if (this.chartSize) {
      return Promise.resolve(this.chartSize);
    }

    return new Promise((resolve) => {
      const query = wx.createSelectorQuery().in(this);
      query.select(".history-canvas").boundingClientRect((rect) => {
        const fallbackWidth = 702;
        const fallbackHeight = 360;
        const width = rect && rect.width ? rect.width : fallbackWidth;
        const height = rect && rect.height ? rect.height : fallbackHeight;

        this.chartSize = { width, height };
        resolve(this.chartSize);
      }).exec();
    });
  },
});
