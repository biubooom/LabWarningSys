from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import List

try:
    from .dataset import BASE_FEATURE_LIMITS, build_feature_vector, build_rate_features, generate_samples, state_display_name, summarize_expected_counts
    from .exporter import export_c_header
    from .static_state_model import build_static_confusion_summary, predict_static_state, train_static_state_model
    from .model_types import FEATURE_INDEX, RAW_SENSOR_FIELDS, SENSOR_GROUP_COUNT, STATE_DISPLAY_NAMES, STATE_ORDER
    from .risk import assess_risk
except ImportError:
    from dataset import BASE_FEATURE_LIMITS, build_feature_vector, build_rate_features, generate_samples, state_display_name, summarize_expected_counts
    from exporter import export_c_header
    from static_state_model import build_static_confusion_summary, predict_static_state, train_static_state_model
    from model_types import FEATURE_INDEX, RAW_SENSOR_FIELDS, SENSOR_GROUP_COUNT, STATE_DISPLAY_NAMES, STATE_ORDER
    from risk import assess_risk


DEFAULT_SEED = 42
DEFAULT_EXPORT_NAME = "generated_static_state_model.h"
STATE_COLORS = {
    "STATE_NORMAL": "#2e8b57",
    "STATE_FIRE": "#d1495b",
    "STATE_GAS_LEAK": "#f4a261",
    "STATE_HIGH_HUMID": "#457b9d",
}
SCATTER_WIDTH = 920
SCATTER_HEIGHT = 260


class StaticStateDemoApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("静态状态识别演示")
        self._configure_window_size()

        self.dataset = None
        self.model = None
        self.manual_entries: dict[tuple[int, str], ttk.Entry] = {}
        self.main_canvas: tk.Canvas | None = None
        self.scrollable_frame: ttk.Frame | None = None
        self.previous_manual_groups: List[List[float]] | None = None

        self.seed_var = tk.StringVar(value=str(DEFAULT_SEED))
        self.status_var = tk.StringVar(value="请先生成4组传感器样本并训练模型。")
        self.prediction_var = tk.StringVar(value="当前未预测。")
        self.derived_features_var = tk.StringVar(value="自动计算特征：\n请先输入或修改4组原始传感器数据。")

        self._build_ui()
        self._bind_redraw_events()

    def _configure_window_size(self) -> None:
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()
        window_width = min(1360, screen_width - 80)
        window_height = min(920, screen_height - 120)
        pos_x = max(20, (screen_width - window_width) // 2)
        pos_y = max(20, (screen_height - window_height) // 2)
        self.root.geometry(f"{window_width}x{window_height}+{pos_x}+{pos_y}")
        self.root.minsize(1120, 760)

    def _build_ui(self) -> None:
        outer_frame = ttk.Frame(self.root, padding=0)
        outer_frame.pack(fill=tk.BOTH, expand=True)
        outer_frame.columnconfigure(0, weight=1)
        outer_frame.rowconfigure(0, weight=1)

        self.main_canvas = tk.Canvas(outer_frame, highlightthickness=0)
        vertical_scrollbar = ttk.Scrollbar(outer_frame, orient=tk.VERTICAL, command=self.main_canvas.yview)
        self.main_canvas.configure(yscrollcommand=vertical_scrollbar.set)

        self.main_canvas.grid(row=0, column=0, sticky="nsew")
        vertical_scrollbar.grid(row=0, column=1, sticky="ns")

        self.scrollable_frame = ttk.Frame(self.main_canvas, padding=12)
        self.scrollable_frame.bind(
            "<Configure>",
            lambda _event: self.main_canvas.configure(scrollregion=self.main_canvas.bbox("all")),
        )
        self.main_canvas.bind(
            "<Configure>",
            lambda event: self.main_canvas.itemconfigure("scrollable_window", width=event.width),
        )
        self.main_canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw", tags="scrollable_window")

        container = self.scrollable_frame
        container.columnconfigure(0, weight=3)
        container.columnconfigure(1, weight=2)
        container.rowconfigure(1, weight=1)
        container.rowconfigure(2, weight=1)

        control_frame = ttk.LabelFrame(container, text="样本生成与训练", padding=12)
        control_frame.grid(row=0, column=0, columnspan=2, sticky="nsew", pady=(0, 10))

        ttk.Label(control_frame, text="随机种子").grid(row=0, column=0, sticky="w")
        ttk.Entry(control_frame, textvariable=self.seed_var, width=12).grid(row=0, column=1, sticky="w", padx=(8, 16))
        ttk.Button(control_frame, text="生成样本并训练", command=self._train_model).grid(row=0, column=2, sticky="w")
        ttk.Button(control_frame, text="导出 MCU 头文件", command=self._export_model).grid(row=0, column=3, sticky="w", padx=(8, 0))
        ttk.Label(control_frame, textvariable=self.status_var).grid(row=1, column=0, columnspan=4, sticky="w", pady=(10, 0))

        results_frame = ttk.LabelFrame(container, text="聚类结果", padding=12)
        results_frame.grid(row=1, column=0, sticky="nsew", padx=(0, 10))
        results_frame.columnconfigure(0, weight=1)
        results_frame.rowconfigure(1, weight=1)
        results_frame.rowconfigure(3, weight=1)

        ttk.Label(results_frame, text="簇中心摘要（显示 avg / max / spread / rate 关键特征）").grid(row=0, column=0, sticky="w")
        self.centers_text = tk.Text(results_frame, height=10, wrap=tk.NONE)
        self.centers_text.grid(row=1, column=0, sticky="nsew", pady=(6, 10))

        ttk.Label(results_frame, text="训练集分类统计 / 混淆摘要").grid(row=2, column=0, sticky="w")
        self.summary_text = tk.Text(results_frame, height=9, wrap=tk.NONE)
        self.summary_text.grid(row=3, column=0, sticky="nsew")

        predict_frame = ttk.LabelFrame(container, text="4组传感器手动输入预测", padding=12)
        predict_frame.grid(row=1, column=1, sticky="nsew")
        predict_frame.columnconfigure(0, weight=1)
        predict_frame.rowconfigure(2, weight=1)

        groups_frame = ttk.Frame(predict_frame)
        groups_frame.grid(row=0, column=0, sticky="nsew")
        for group_index in range(SENSOR_GROUP_COUNT):
            groups_frame.columnconfigure(group_index, weight=1)

        defaults = {
            1: {"temperature": "78", "humidity": "22", "smoke": "90", "light": "66"},
            2: {"temperature": "57", "humidity": "29", "smoke": "61", "light": "58"},
            3: {"temperature": "37", "humidity": "35", "smoke": "25", "light": "50"},
            4: {"temperature": "34", "humidity": "38", "smoke": "22", "light": "46"},
        }

        for group_index in range(1, SENSOR_GROUP_COUNT + 1):
            group_frame = ttk.LabelFrame(groups_frame, text=f"方位 {group_index}", padding=8)
            group_frame.grid(row=0, column=group_index - 1, sticky="nsew", padx=(0, 6) if group_index < SENSOR_GROUP_COUNT else 0)
            group_frame.columnconfigure(1, weight=1)
            for row_index, field_name in enumerate(RAW_SENSOR_FIELDS):
                low, high = BASE_FEATURE_LIMITS[field_name]
                ttk.Label(group_frame, text=f"{field_name} ({low:.0f}-{high:.0f})").grid(row=row_index, column=0, sticky="w", pady=3)
                entry = ttk.Entry(group_frame, width=12)
                entry.grid(row=row_index, column=1, sticky="ew", pady=3)
                entry.insert(0, defaults[group_index][field_name])
                self.manual_entries[(group_index, field_name)] = entry

        action_frame = ttk.Frame(predict_frame)
        action_frame.grid(row=1, column=0, sticky="ew", pady=(10, 10))
        action_frame.columnconfigure(0, weight=1)
        action_frame.columnconfigure(1, weight=1)
        ttk.Button(action_frame, text="预测状态", command=self._predict_manual_input).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(action_frame, text="清除上一帧", command=self._clear_previous_manual_groups).grid(row=0, column=1, sticky="ew")
        ttk.Label(predict_frame, textvariable=self.prediction_var, justify=tk.LEFT).grid(row=2, column=0, sticky="nw", pady=(0, 8))

        derived_frame = ttk.LabelFrame(predict_frame, text="自动计算特征", padding=8)
        derived_frame.grid(row=3, column=0, sticky="nsew", pady=(0, 8))
        ttk.Label(derived_frame, textvariable=self.derived_features_var, justify=tk.LEFT).pack(fill=tk.BOTH, expand=True)

        legend_frame = ttk.LabelFrame(predict_frame, text="状态说明", padding=8)
        legend_frame.grid(row=4, column=0, sticky="nsew")
        legend_text = tk.Text(legend_frame, height=8, wrap=tk.WORD)
        legend_text.pack(fill=tk.BOTH, expand=True)
        legend_text.insert("1.0", "\n".join(f"{label}: {STATE_DISPLAY_NAMES[label]}" for label in STATE_ORDER))
        legend_text.configure(state=tk.DISABLED)

        visualization_frame = ttk.LabelFrame(container, text="聚类效果可视化", padding=12)
        visualization_frame.grid(row=2, column=0, columnspan=2, sticky="nsew", pady=(10, 0))
        visualization_frame.columnconfigure(0, weight=1)
        visualization_frame.rowconfigure(1, weight=1)

        ttk.Label(visualization_frame, text="T_max - S_max 散点图（按预测状态着色）").grid(row=0, column=0, sticky="w")
        self.scatter_canvas = tk.Canvas(
            visualization_frame,
            width=SCATTER_WIDTH,
            height=SCATTER_HEIGHT,
            bg="white",
            highlightthickness=1,
            highlightbackground="#c8c8c8",
        )
        self.scatter_canvas.grid(row=1, column=0, sticky="nsew", pady=(6, 0))

        self._draw_placeholder_chart()
        self._refresh_derived_feature_preview()

    def _bind_redraw_events(self) -> None:
        self.scatter_canvas.bind("<Configure>", lambda _event: self._redraw_visuals())
        self.root.bind_all("<MouseWheel>", self._on_mousewheel)
        self.root.bind_all("<Button-4>", self._on_mousewheel)
        self.root.bind_all("<Button-5>", self._on_mousewheel)
        for entry in self.manual_entries.values():
            entry.bind("<KeyRelease>", lambda _event: self._refresh_derived_feature_preview())
            entry.bind("<FocusOut>", lambda _event: self._refresh_derived_feature_preview())

    def _on_mousewheel(self, event: tk.Event) -> None:
        if self.main_canvas is None:
            return
        if getattr(event, "delta", 0):
            self.main_canvas.yview_scroll(int(-event.delta / 120), "units")
        elif getattr(event, "num", None) == 4:
            self.main_canvas.yview_scroll(-1, "units")
        elif getattr(event, "num", None) == 5:
            self.main_canvas.yview_scroll(1, "units")

    def _parse_seed(self) -> int:
        try:
            return int(self.seed_var.get().strip())
        except ValueError as exc:
            raise ValueError("随机种子必须是整数。") from exc

    def _train_model(self) -> None:
        try:
            seed = self._parse_seed()
            self.dataset = generate_samples(seed=seed)
            self.model = train_static_state_model(self.dataset.samples, seed=seed)
            self._refresh_training_view()
            self.previous_manual_groups = None
            self._refresh_derived_feature_preview()
            self.status_var.set(
                f"训练完成：样本数 {len(self.dataset.samples)}，特征维度 {len(self.dataset.feature_names)}，迭代 {self.model.iterations} 次，惯性 {self.model.inertia:.4f}。"
            )
        except Exception as exc:
            messagebox.showerror("训练失败", str(exc))

    def _refresh_training_view(self) -> None:
        if self.dataset is None or self.model is None:
            return

        expected_counts = summarize_expected_counts(self.dataset)
        confusion = build_static_confusion_summary(self.model, self.dataset.samples)

        center_lines = []
        for cluster_id, center in enumerate(self.model.centers):
            state_label = self.model.cluster_label_map[cluster_id]
            display_name = state_display_name(state_label)
            summary = (
                f"T_avg={center[FEATURE_INDEX['temperature_avg']]:.1f}, "
                f"S_avg={center[FEATURE_INDEX['smoke_avg']]:.1f}, "
                f"T_max={center[FEATURE_INDEX['temperature_max']]:.1f}, "
                f"S_max={center[FEATURE_INDEX['smoke_max']]:.1f}, "
                f"T_spread={center[FEATURE_INDEX['temperature_spread']]:.1f}, "
                f"S_spread={center[FEATURE_INDEX['smoke_spread']]:.1f}"
            )
            center_lines.append(f"Cluster {cluster_id}: {state_label} / {display_name}")
            center_lines.append(f"  {summary}")
        self._set_text(self.centers_text, "\n".join(center_lines))

        summary_lines = ["训练集样本数："]
        for state_label in STATE_ORDER:
            summary_lines.append(
                f"  {state_label} / {state_display_name(state_label)}: expected={expected_counts[state_label]}, predicted={self.model.training_counts[state_label]}"
            )
        summary_lines.append("")
        summary_lines.append("混淆摘要：expected -> predicted=count")
        for expected_label in STATE_ORDER:
            pieces = [f"{predicted_label}={confusion[expected_label][predicted_label]}" for predicted_label in STATE_ORDER]
            summary_lines.append(f"  {expected_label}: " + ", ".join(pieces))
        self._set_text(self.summary_text, "\n".join(summary_lines))
        self._redraw_visuals()

    def _collect_manual_groups(self) -> List[List[float]]:
        raw_groups: List[List[float]] = []
        for group_index in range(1, SENSOR_GROUP_COUNT + 1):
            current_group: List[float] = []
            for field_name in RAW_SENSOR_FIELDS:
                raw_value = self.manual_entries[(group_index, field_name)].get().strip()
                if not raw_value:
                    raise ValueError(f"方位 {group_index} 的 {field_name} 不能为空。")
                value = float(raw_value)
                low, high = BASE_FEATURE_LIMITS[field_name]
                if value < low or value > high:
                    raise ValueError(f"方位 {group_index} 的 {field_name} 超出范围，应在 {low:.0f}-{high:.0f} 之间。")
                current_group.append(value)
            raw_groups.append(current_group)
        return raw_groups

    def _refresh_derived_feature_preview(self) -> None:
        try:
            raw_groups = self._collect_manual_groups()
        except ValueError as exc:
            self.derived_features_var.set(f"自动计算特征：\n等待有效输入。\n{exc}")
            return

        feature_vector = build_feature_vector(raw_groups)
        rate_features = build_rate_features(raw_groups, self.previous_manual_groups)
        if self.previous_manual_groups is None:
            rate_hint = "变化率基于上一帧：当前暂无上一帧，rate 按 0 处理"
        else:
            rate_hint = "变化率基于上一帧：已使用上一次成功预测的输入"
        self.derived_features_var.set(
            "自动计算特征：\n"
            f"T_avg={feature_vector[FEATURE_INDEX['temperature_avg']]:.1f}, H_avg={feature_vector[FEATURE_INDEX['humidity_avg']]:.1f}, "
            f"S_avg={feature_vector[FEATURE_INDEX['smoke_avg']]:.1f}, L_avg={feature_vector[FEATURE_INDEX['light_avg']]:.1f}\n"
            f"T_max={feature_vector[FEATURE_INDEX['temperature_max']]:.1f}, S_max={feature_vector[FEATURE_INDEX['smoke_max']]:.1f}, "
            f"L_max={feature_vector[FEATURE_INDEX['light_max']]:.1f}\n"
            f"T_spread={feature_vector[FEATURE_INDEX['temperature_spread']]:.1f}, "
            f"S_spread={feature_vector[FEATURE_INDEX['smoke_spread']]:.1f}, "
            f"L_spread={feature_vector[FEATURE_INDEX['light_spread']]:.1f}\n"
            f"{rate_hint}\n"
            f" dT_avg={rate_features['temperature_avg_rate']:.1f},"
            f" dS_avg={rate_features['smoke_avg_rate']:.1f},"
            f" dT_max={rate_features['temperature_max_rate']:.1f},"
            f" dS_max={rate_features['smoke_max_rate']:.1f}"
        )

    def _clear_previous_manual_groups(self) -> None:
        self.previous_manual_groups = None
        self._refresh_derived_feature_preview()
        self.prediction_var.set("当前未预测。\n上一帧已清除，下一次预测的变化率将按 0 处理。")

    def _predict_manual_input(self) -> None:
        if self.model is None:
            messagebox.showwarning("未训练", "请先生成样本并训练模型。")
            return

        try:
            raw_groups = self._collect_manual_groups()
            self._refresh_derived_feature_preview()
            feature_vector = build_feature_vector(raw_groups)
            result = predict_static_state(self.model, feature_vector)
            risk = assess_risk(self.model, feature_vector, result.state_label, raw_groups, self.previous_manual_groups)
            self.prediction_var.set(
                f"聚类结果：簇 {result.cluster_id}\n"
                f"异常类型：{result.state_label} / {result.display_name}\n"
                f"风险等级：{risk.level} / {risk.display_name} (score={risk.score:.1f})\n"
                f"距离：{result.distance:.4f}\n"
                f"T_max={feature_vector[FEATURE_INDEX['temperature_max']]:.1f}, "
                f"S_max={feature_vector[FEATURE_INDEX['smoke_max']]:.1f}, "
                f"T_spread={feature_vector[FEATURE_INDEX['temperature_spread']]:.1f}, "
                f"S_spread={feature_vector[FEATURE_INDEX['smoke_spread']]:.1f}\n"
                f"触发原因：{'；'.join(risk.reasons)}"
            )
            self.previous_manual_groups = [list(group) for group in raw_groups]
            self._refresh_derived_feature_preview()
            self._redraw_visuals(highlight_groups=raw_groups)
        except ValueError as exc:
            messagebox.showerror("输入错误", str(exc))

    def _export_model(self) -> None:
        if self.model is None:
            messagebox.showwarning("未训练", "请先生成样本并训练模型。")
            return

        default_path = Path(__file__).resolve().parent / DEFAULT_EXPORT_NAME
        target_path = filedialog.asksaveasfilename(
            title="导出 MCU 头文件",
            initialdir=default_path.parent,
            initialfile=default_path.name,
            defaultextension=".h",
            filetypes=[("C Header", "*.h"), ("All Files", "*.*")],
        )
        if not target_path:
            return

        export_c_header(self.model, target_path)
        messagebox.showinfo("导出成功", f"已导出到:\n{target_path}")

    @staticmethod
    def _set_text(widget: tk.Text, value: str) -> None:
        widget.delete("1.0", tk.END)
        widget.insert("1.0", value)

    def _draw_placeholder_chart(self) -> None:
        self.scatter_canvas.delete("all")
        width = max(int(self.scatter_canvas.winfo_width()), int(self.scatter_canvas.cget("width")))
        height = max(int(self.scatter_canvas.winfo_height()), int(self.scatter_canvas.cget("height")))
        self.scatter_canvas.create_text(width / 2, height / 2, text="训练后显示 T_max - S_max 聚类分布", fill="#666666", font=("Microsoft YaHei UI", 11))

    def _redraw_visuals(self, highlight_groups: List[List[float]] | None = None) -> None:
        if self.dataset is None or self.model is None:
            self._draw_placeholder_chart()
            return
        self._draw_scatter_plot(highlight_groups)

    def _draw_scatter_plot(self, highlight_groups: List[List[float]] | None) -> None:
        canvas = self.scatter_canvas
        canvas.delete("all")
        width = max(int(canvas.winfo_width()), int(canvas.cget("width")))
        height = max(int(canvas.winfo_height()), int(canvas.cget("height")))
        margin_left = 54
        margin_right = 160
        margin_top = 18
        margin_bottom = 40

        x_low, x_high = BASE_FEATURE_LIMITS["temperature"]
        y_low, y_high = BASE_FEATURE_LIMITS["smoke"]

        def map_x(value: float) -> float:
            return margin_left + (value - x_low) / (x_high - x_low) * (width - margin_left - margin_right)

        def map_y(value: float) -> float:
            return height - margin_bottom - (value - y_low) / (y_high - y_low) * (height - margin_top - margin_bottom)

        canvas.create_line(margin_left, height - margin_bottom, width - margin_right, height - margin_bottom, fill="#444444", width=2)
        canvas.create_line(margin_left, margin_top, margin_left, height - margin_bottom, fill="#444444", width=2)

        for tick in range(0, 101, 20):
            x = map_x(float(tick))
            y = map_y(float(tick))
            canvas.create_line(x, height - margin_bottom, x, height - margin_bottom + 5, fill="#666666")
            canvas.create_line(margin_left - 5, y, margin_left, y, fill="#666666")
            canvas.create_text(x, height - margin_bottom + 16, text=str(tick), fill="#555555", font=("Microsoft YaHei UI", 9))
            canvas.create_text(margin_left - 20, y, text=str(tick), fill="#555555", font=("Microsoft YaHei UI", 9))

        canvas.create_text((width - margin_right + margin_left) / 2, height - 12, text="T_max", fill="#333333", font=("Microsoft YaHei UI", 10))
        canvas.create_text(20, height / 2, text="S_max", fill="#333333", angle=90, font=("Microsoft YaHei UI", 10))

        for sample, cluster_id in zip(self.dataset.samples, self.model.assignments):
            state_label = self.model.cluster_label_map[cluster_id]
            color = STATE_COLORS[state_label]
            x = map_x(sample.features[FEATURE_INDEX["temperature_max"]])
            y = map_y(sample.features[FEATURE_INDEX["smoke_max"]])
            canvas.create_oval(x - 2, y - 2, x + 2, y + 2, fill=color, outline="")

        for cluster_id, center in enumerate(self.model.centers):
            state_label = self.model.cluster_label_map[cluster_id]
            color = STATE_COLORS[state_label]
            x = map_x(center[FEATURE_INDEX["temperature_max"]])
            y = map_y(center[FEATURE_INDEX["smoke_max"]])
            canvas.create_line(x - 7, y - 7, x + 7, y + 7, fill=color, width=2)
            canvas.create_line(x - 7, y + 7, x + 7, y - 7, fill=color, width=2)
            canvas.create_text(x + 16, y - 10, text=f"C{cluster_id}", fill=color, font=("Microsoft YaHei UI", 9, "bold"))

        if highlight_groups is not None:
            highlight_feature_vector = build_feature_vector(highlight_groups)
            hx = map_x(highlight_feature_vector[FEATURE_INDEX["temperature_max"]])
            hy = map_y(highlight_feature_vector[FEATURE_INDEX["smoke_max"]])
            canvas.create_oval(hx - 6, hy - 6, hx + 6, hy + 6, outline="#111111", width=2)
            canvas.create_text(hx + 18, hy + 12, text="当前输入", fill="#111111", font=("Microsoft YaHei UI", 9, "bold"))

        legend_x = width - 140
        legend_y = 22
        for index, state_label in enumerate(STATE_ORDER):
            y = legend_y + index * 20
            color = STATE_COLORS[state_label]
            canvas.create_rectangle(legend_x, y, legend_x + 10, y + 10, fill=color, outline="")
            canvas.create_text(legend_x + 16, y + 5, text=STATE_DISPLAY_NAMES[state_label], anchor="w", fill="#333333", font=("Microsoft YaHei UI", 9))


def main() -> None:
    root = tk.Tk()
    style = ttk.Style()
    if "vista" in style.theme_names():
        style.theme_use("vista")
    app = StaticStateDemoApp(root)
    app.root.mainloop()


if __name__ == "__main__":
    main()
