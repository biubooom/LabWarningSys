from __future__ import annotations

import json
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk
from typing import Dict, List

try:
    from .dataset import summarize_raw_groups, validate_raw_sequence
    from .demo_simulation import build_simulated_sequence, ensure_model_bundle, resolve_scenario
    from .model_types import SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from .sequence_classifier import DEFAULT_ARTIFACT_DIR, predict_sequence
except ImportError:
    from dataset import summarize_raw_groups, validate_raw_sequence
    from demo_simulation import build_simulated_sequence, ensure_model_bundle, resolve_scenario
    from model_types import SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from sequence_classifier import DEFAULT_ARTIFACT_DIR, predict_sequence


STATE_COLORS: Dict[str, str] = {
    "STATE_NORMAL": "#2e8b57",
    "STATE_FIRE": "#d1495b",
    "STATE_GAS_LEAK": "#f4a261",
    "STATE_HIGH_HUMID": "#457b9d",
}

SCENARIO_OPTIONS = (
    ("normal", "正常"),
    ("fire", "火灾"),
    ("gas_leak", "气体泄漏"),
    ("high_humid", "高湿"),
)

DEFAULT_MANUAL_SEQUENCE = [
    [[25, 55, 6, 62], [25, 56, 6, 61], [24, 55, 5, 63], [25, 54, 6, 62]]
    for _ in range(SEQUENCE_LENGTH)
]


class SequenceBackendDemoApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("GRU 时序状态识别界面")
        self._configure_window()

        self.current_sequence: List[List[List[float]]] | None = None
        self.current_state_label: str | None = None

        self.seed_var = tk.StringVar(value="42")
        self.scenario_var = tk.StringVar(value="fire")
        self.artifact_dir_var = tk.StringVar(value=str(DEFAULT_ARTIFACT_DIR))
        self.status_var = tk.StringVar(value="请选择场景并生成模拟输入序列。")
        self.summary_var = tk.StringVar(value="当前还没有模拟序列。")
        self.prediction_var = tk.StringVar(value="当前未预测。")

        self.sequence_tree: ttk.Treeview | None = None
        self.detail_text: tk.Text | None = None
        self.probability_text: tk.Text | None = None
        self.chart_canvas: tk.Canvas | None = None
        self.manual_input_text: tk.Text | None = None

        self._build_ui()

    def _configure_window(self) -> None:
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()
        window_width = min(1420, screen_width - 80)
        window_height = min(940, screen_height - 120)
        pos_x = max(20, (screen_width - window_width) // 2)
        pos_y = max(20, (screen_height - window_height) // 2)
        self.root.geometry(f"{window_width}x{window_height}+{pos_x}+{pos_y}")
        self.root.minsize(1180, 760)

    def _build_ui(self) -> None:
        style = ttk.Style()
        if "vista" in style.theme_names():
            style.theme_use("vista")

        container = ttk.Frame(self.root, padding=14)
        container.pack(fill=tk.BOTH, expand=True)
        container.columnconfigure(0, weight=5)
        container.columnconfigure(1, weight=4)
        container.rowconfigure(1, weight=1)
        container.rowconfigure(2, weight=1)

        header_frame = ttk.LabelFrame(container, text="模拟输入与预测控制", padding=12)
        header_frame.grid(row=0, column=0, columnspan=2, sticky="nsew", pady=(0, 10))
        for column in range(6):
            header_frame.columnconfigure(column, weight=1 if column in (1, 3) else 0)

        ttk.Label(header_frame, text="模拟场景").grid(row=0, column=0, sticky="w")
        scenario_combo = ttk.Combobox(
            header_frame,
            textvariable=self.scenario_var,
            values=[key for key, _display in SCENARIO_OPTIONS],
            state="readonly",
            width=16,
        )
        scenario_combo.grid(row=0, column=1, sticky="ew", padx=(8, 16))

        ttk.Label(header_frame, text="随机种子").grid(row=0, column=2, sticky="w")
        ttk.Entry(header_frame, textvariable=self.seed_var, width=12).grid(row=0, column=3, sticky="ew", padx=(8, 16))
        ttk.Button(header_frame, text="生成模拟序列", command=self._generate_sequence).grid(row=0, column=4, sticky="ew", padx=(0, 16))
        ttk.Button(header_frame, text="载入手动输入", command=self._load_manual_sequence).grid(row=0, column=5, sticky="ew")

        ttk.Label(header_frame, text="本地模型目录").grid(row=1, column=0, sticky="w", pady=(10, 0))
        ttk.Entry(header_frame, textvariable=self.artifact_dir_var).grid(row=1, column=1, columnspan=3, sticky="ew", padx=(8, 16), pady=(10, 0))
        ttk.Button(header_frame, text="本地预测", command=self._predict_local).grid(row=1, column=4, sticky="ew", pady=(10, 0))
        ttk.Button(header_frame, text="打开旧静态演示", command=self._show_static_hint).grid(row=1, column=5, sticky="ew", pady=(10, 0))

        ttk.Label(header_frame, textvariable=self.status_var).grid(row=2, column=0, columnspan=6, sticky="w", pady=(12, 0))

        left_frame = ttk.LabelFrame(container, text="16 帧模拟序列", padding=12)
        left_frame.grid(row=1, column=0, rowspan=2, sticky="nsew", padx=(0, 10))
        left_frame.columnconfigure(0, weight=1)
        left_frame.rowconfigure(1, weight=3)
        left_frame.rowconfigure(3, weight=2)

        ttk.Label(left_frame, textvariable=self.summary_var, justify=tk.LEFT).grid(row=0, column=0, sticky="w", pady=(0, 8))

        columns = ("frame", "temperature_avg", "humidity_avg", "smoke_avg", "light_avg", "temperature_max", "smoke_max")
        tree = ttk.Treeview(left_frame, columns=columns, show="headings", height=14)
        self.sequence_tree = tree
        headings = {
            "frame": "帧",
            "temperature_avg": "T_avg",
            "humidity_avg": "H_avg",
            "smoke_avg": "S_avg",
            "light_avg": "L_avg",
            "temperature_max": "T_max",
            "smoke_max": "S_max",
        }
        widths = {
            "frame": 60,
            "temperature_avg": 92,
            "humidity_avg": 92,
            "smoke_avg": 92,
            "light_avg": 92,
            "temperature_max": 92,
            "smoke_max": 92,
        }
        for column in columns:
            tree.heading(column, text=headings[column])
            tree.column(column, width=widths[column], anchor=tk.CENTER, stretch=True)
        tree.grid(row=1, column=0, sticky="nsew")
        tree.bind("<<TreeviewSelect>>", self._on_select_frame)

        tree_scrollbar = ttk.Scrollbar(left_frame, orient=tk.VERTICAL, command=tree.yview)
        tree.configure(yscrollcommand=tree_scrollbar.set)
        tree_scrollbar.place(relx=1.0, rely=0.09, relheight=0.51, anchor="ne")

        ttk.Label(left_frame, text="所选帧详细值").grid(row=2, column=0, sticky="w", pady=(10, 6))
        self.detail_text = tk.Text(left_frame, height=10, wrap=tk.WORD)
        self.detail_text.grid(row=3, column=0, sticky="nsew")
        self._set_text(self.detail_text, "生成序列后，点击某一帧查看 4 组传感器详细值。")

        right_top = ttk.LabelFrame(container, text="预测结果", padding=12)
        right_top.grid(row=1, column=1, sticky="nsew")
        right_top.columnconfigure(0, weight=1)
        right_top.rowconfigure(3, weight=1)

        ttk.Label(right_top, textvariable=self.prediction_var, justify=tk.LEFT).grid(row=0, column=0, sticky="nw")
        ttk.Label(
            right_top,
            text=(
                "手动输入序列（JSON）\n"
                f"要求：共 {SEQUENCE_LENGTH} 帧，每帧 4 组，每组 4 个值，顺序为 "
                "[temperature, humidity, smoke, light]"
            ),
            justify=tk.LEFT,
        ).grid(row=1, column=0, sticky="nw", pady=(10, 6))
        manual_toolbar = ttk.Frame(right_top)
        manual_toolbar.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        for column in range(5):
            manual_toolbar.columnconfigure(column, weight=1)
        ttk.Button(manual_toolbar, text="载入到表格", command=self._load_manual_sequence).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(manual_toolbar, text="格式化 JSON", command=self._format_manual_input).grid(row=0, column=1, sticky="ew", padx=3)
        ttk.Button(manual_toolbar, text="填充示例", command=self._fill_default_manual_input).grid(row=0, column=2, sticky="ew", padx=3)
        ttk.Button(manual_toolbar, text="回填当前序列", command=self._sync_manual_input).grid(row=0, column=3, sticky="ew", padx=3)
        ttk.Button(manual_toolbar, text="清空输入", command=self._clear_manual_input).grid(row=0, column=4, sticky="ew", padx=(6, 0))

        self.manual_input_text = tk.Text(right_top, height=12, wrap=tk.WORD)
        self.manual_input_text.grid(row=3, column=0, sticky="nsew")
        self._fill_default_manual_input()

        ttk.Label(right_top, text="概率分布").grid(row=4, column=0, sticky="nw", pady=(10, 6))
        self.probability_text = tk.Text(right_top, height=8, wrap=tk.WORD)
        self.probability_text.grid(row=5, column=0, sticky="nsew")
        self._set_text(self.probability_text, "预测后显示各状态概率。")

        right_bottom = ttk.LabelFrame(container, text="趋势概览", padding=12)
        right_bottom.grid(row=2, column=1, sticky="nsew")
        right_bottom.columnconfigure(0, weight=1)
        right_bottom.rowconfigure(1, weight=1)

        ttk.Label(right_bottom, text="T_max / S_max 随时间变化").grid(row=0, column=0, sticky="w")
        self.chart_canvas = tk.Canvas(
            right_bottom,
            bg="#fffdf8",
            highlightthickness=1,
            highlightbackground="#d8d0c0",
            width=520,
            height=320,
        )
        self.chart_canvas.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        self._draw_placeholder_chart()

    def _parse_seed(self) -> int:
        try:
            return int(self.seed_var.get().strip())
        except ValueError as exc:
            raise ValueError("随机种子必须是整数。") from exc

    def _generate_sequence(self) -> None:
        try:
            state_label = resolve_scenario(self.scenario_var.get())
            seed = self._parse_seed()
            self.current_state_label = state_label
            self.current_sequence = build_simulated_sequence(state_label, seed)
            self._populate_sequence_tree()
            self._draw_trend_chart()
            self._sync_manual_input()
            self.prediction_var.set("当前未预测。\n已生成新的模拟输入序列。")
            self._set_text(self.probability_text, "预测后显示各状态概率。")
            display_name = STATE_DISPLAY_NAMES[state_label]
            self.status_var.set(f"已生成场景 {state_label} / {display_name} 的 {SEQUENCE_LENGTH} 帧模拟序列。")
        except Exception as exc:
            messagebox.showerror("生成失败", str(exc))

    def _load_manual_sequence(self) -> None:
        if self.manual_input_text is None:
            return
        try:
            raw_text = self.manual_input_text.get("1.0", tk.END).strip()
            sequence = validate_raw_sequence(json.loads(raw_text))
            self.current_sequence = sequence
            self.current_state_label = None
            self._populate_sequence_tree()
            self._draw_trend_chart()
            self.prediction_var.set("当前未预测。\n已载入手动输入序列。")
            self._set_text(self.probability_text, "预测后显示各状态概率。")
            self.status_var.set(f"已载入手动输入序列，共 {SEQUENCE_LENGTH} 帧。")
        except json.JSONDecodeError as exc:
            messagebox.showerror("载入失败", f"手动输入不是合法 JSON：{exc}")
        except Exception as exc:
            messagebox.showerror("载入失败", str(exc))

    def _sync_manual_input(self) -> None:
        if self.manual_input_text is None or self.current_sequence is None:
            return
        self._set_text(self.manual_input_text, json.dumps(self.current_sequence, ensure_ascii=False, indent=2))
        self.status_var.set("已将当前序列回填到手动输入区。")

    def _fill_default_manual_input(self) -> None:
        if self.manual_input_text is None:
            return
        self._set_text(self.manual_input_text, json.dumps(DEFAULT_MANUAL_SEQUENCE, ensure_ascii=False, indent=2))
        self.status_var.set("已填充默认示例序列，可直接修改后载入。")

    def _clear_manual_input(self) -> None:
        if self.manual_input_text is None:
            return
        self._set_text(self.manual_input_text, "")
        self.status_var.set("已清空手动输入区。")

    def _format_manual_input(self) -> None:
        if self.manual_input_text is None:
            return
        try:
            raw_text = self.manual_input_text.get("1.0", tk.END).strip()
            if not raw_text:
                raise ValueError("手动输入区为空，无法格式化。")
            parsed = json.loads(raw_text)
            self._set_text(self.manual_input_text, json.dumps(parsed, ensure_ascii=False, indent=2))
            self.status_var.set("手动输入 JSON 已格式化。")
        except json.JSONDecodeError as exc:
            messagebox.showerror("格式化失败", f"当前内容不是合法 JSON：{exc}")
        except Exception as exc:
            messagebox.showerror("格式化失败", str(exc))

    def _populate_sequence_tree(self) -> None:
        assert self.sequence_tree is not None
        self.sequence_tree.delete(*self.sequence_tree.get_children())
        if self.current_sequence is None:
            return

        final_summary = summarize_raw_groups(self.current_sequence[-1])
        if self.current_state_label is None:
            target_line = "目标场景：手动输入 / 未指定"
        else:
            target_line = f"目标场景：{self.current_state_label} / {STATE_DISPLAY_NAMES[self.current_state_label]}"
        self.summary_var.set(
            f"{target_line}\n"
            f"末帧概览：T_avg={final_summary['temperature_avg']:.1f}, "
            f"S_avg={final_summary['smoke_avg']:.1f}, "
            f"T_max={final_summary['temperature_max']:.1f}, "
            f"S_max={final_summary['smoke_max']:.1f}"
        )

        for frame_index, frame in enumerate(self.current_sequence, start=1):
            summary = summarize_raw_groups(frame)
            self.sequence_tree.insert(
                "",
                tk.END,
                iid=str(frame_index - 1),
                values=(
                    f"{frame_index:02d}",
                    f"{summary['temperature_avg']:.1f}",
                    f"{summary['humidity_avg']:.1f}",
                    f"{summary['smoke_avg']:.1f}",
                    f"{summary['light_avg']:.1f}",
                    f"{summary['temperature_max']:.1f}",
                    f"{summary['smoke_max']:.1f}",
                ),
            )
        self.sequence_tree.selection_set("0")
        self._show_frame_details(0)

    def _on_select_frame(self, _event: tk.Event) -> None:
        if self.sequence_tree is None:
            return
        selection = self.sequence_tree.selection()
        if not selection:
            return
        self._show_frame_details(int(selection[0]))

    def _show_frame_details(self, frame_index: int) -> None:
        if self.current_sequence is None:
            return
        frame = self.current_sequence[frame_index]
        lines = [f"Frame {frame_index + 1:02d} 详细值："]
        for group_index, group in enumerate(frame, start=1):
            lines.append(
                f"方位 {group_index}: "
                f"T={group[0]:.1f}, H={group[1]:.1f}, S={group[2]:.1f}, L={group[3]:.1f}"
            )
        self._set_text(self.detail_text, "\n".join(lines))

    def _predict_local(self) -> None:
        if self.current_sequence is None:
            self._load_manual_sequence()
        if self.current_sequence is None:
            self._generate_sequence()
        if self.current_sequence is None:
            return
        try:
            self.status_var.set("正在准备本地模型并执行预测，请稍候...")
            self.root.update_idletasks()
            bundle = ensure_model_bundle(Path(self.artifact_dir_var.get().strip()))
            result = predict_sequence(bundle, self.current_sequence)
            self._render_prediction_result(
                result.state_label,
                result.display_name,
                result.confidence,
                result.probabilities,
                result.origin_display_name,
                result.origin_confidence,
                source="本地模型",
            )
            self.status_var.set("本地预测完成。")
        except Exception as exc:
            messagebox.showerror("本地预测失败", str(exc))
            self.status_var.set("本地预测失败。")

    def _render_prediction_result(
        self,
        state_label: str,
        display_name: str,
        confidence: float,
        probabilities: Dict[str, float],
        origin_display_name: str,
        origin_confidence: float,
        *,
        source: str,
    ) -> None:
        expected_line = "未指定模拟场景。"
        if self.current_state_label is not None:
            expected_line = f"{self.current_state_label} / {STATE_DISPLAY_NAMES[self.current_state_label]}"
        self.prediction_var.set(
            f"预测来源：{source}\n"
            f"模拟目标：{expected_line}\n"
            f"预测结果：{state_label} / {display_name}\n"
            f"置信度：{confidence:.4f}\n"
            f"源头角：{origin_display_name} ({origin_confidence:.4f})"
        )

        sorted_rows = sorted(probabilities.items(), key=lambda item: item[1], reverse=True)
        probability_lines = []
        for label, probability in sorted_rows:
            marker = " <- Top1" if label == state_label else ""
            probability_lines.append(f"{label} / {STATE_DISPLAY_NAMES[label]}: {probability:.4f}{marker}")
        self._set_text(self.probability_text, "\n".join(probability_lines))

    def _draw_placeholder_chart(self) -> None:
        assert self.chart_canvas is not None
        canvas = self.chart_canvas
        canvas.delete("all")
        width = max(int(canvas.winfo_width()), int(canvas.cget("width")))
        height = max(int(canvas.winfo_height()), int(canvas.cget("height")))
        canvas.create_text(width / 2, height / 2, text="生成模拟序列后显示 T_max / S_max 趋势", fill="#666666", font=("Microsoft YaHei UI", 11))

    def _draw_trend_chart(self) -> None:
        if self.current_sequence is None or self.chart_canvas is None:
            self._draw_placeholder_chart()
            return

        canvas = self.chart_canvas
        canvas.delete("all")
        width = max(int(canvas.winfo_width()), int(canvas.cget("width")))
        height = max(int(canvas.winfo_height()), int(canvas.cget("height")))
        margin_left = 48
        margin_right = 24
        margin_top = 18
        margin_bottom = 34
        plot_width = width - margin_left - margin_right
        plot_height = height - margin_top - margin_bottom

        t_max_values = [summarize_raw_groups(frame)["temperature_max"] for frame in self.current_sequence]
        s_max_values = [summarize_raw_groups(frame)["smoke_max"] for frame in self.current_sequence]

        def map_x(frame_index: int) -> float:
            if len(self.current_sequence) == 1:
                return margin_left + plot_width / 2
            return margin_left + frame_index / (len(self.current_sequence) - 1) * plot_width

        def map_y(value: float) -> float:
            return height - margin_bottom - value / 100.0 * plot_height

        canvas.create_line(margin_left, height - margin_bottom, width - margin_right, height - margin_bottom, fill="#404040", width=2)
        canvas.create_line(margin_left, margin_top, margin_left, height - margin_bottom, fill="#404040", width=2)

        for tick in range(0, 101, 20):
            y = map_y(float(tick))
            canvas.create_line(margin_left - 4, y, margin_left, y, fill="#606060")
            canvas.create_text(margin_left - 18, y, text=str(tick), fill="#666666", font=("Microsoft YaHei UI", 9))

        for frame_index in range(len(self.current_sequence)):
            x = map_x(frame_index)
            canvas.create_line(x, height - margin_bottom, x, height - margin_bottom + 4, fill="#606060")
            canvas.create_text(x, height - margin_bottom + 16, text=str(frame_index + 1), fill="#666666", font=("Microsoft YaHei UI", 8))

        self._draw_series(canvas, t_max_values, map_x, map_y, color="#d1495b", label="T_max")
        self._draw_series(canvas, s_max_values, map_x, map_y, color="#f4a261", label="S_max")

        if self.current_state_label is not None:
            color = STATE_COLORS[self.current_state_label]
            canvas.create_text(width - 110, 18, text=STATE_DISPLAY_NAMES[self.current_state_label], fill=color, font=("Microsoft YaHei UI", 11, "bold"))

    def _draw_series(self, canvas: tk.Canvas, values: List[float], map_x, map_y, *, color: str, label: str) -> None:
        points: List[float] = []
        for index, value in enumerate(values):
            points.extend((map_x(index), map_y(value)))
        canvas.create_line(*points, fill=color, width=2.5, smooth=True)
        for index, value in enumerate(values):
            x = map_x(index)
            y = map_y(value)
            canvas.create_oval(x - 3, y - 3, x + 3, y + 3, fill=color, outline="")
        canvas.create_text(canvas.winfo_reqwidth() - 90, 40 if label == "T_max" else 60, text=label, fill=color, font=("Microsoft YaHei UI", 10, "bold"))

    def _show_static_hint(self) -> None:
        messagebox.showinfo(
            "旧静态演示",
            "旧的静态状态识别界面仍然保留在:\nSequenceBackend\\static_demo_app.py\n\n"
            "运行方式:\npython SequenceBackend\\static_demo_app.py",
        )

    @staticmethod
    def _set_text(widget: tk.Text | None, value: str) -> None:
        if widget is None:
            return
        widget.delete("1.0", tk.END)
        widget.insert("1.0", value)


def main() -> None:
    root = tk.Tk()
    app = SequenceBackendDemoApp(root)
    app.root.mainloop()


if __name__ == "__main__":
    main()
