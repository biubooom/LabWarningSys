# GRU Sequence Backend

这个目录现在同时保留两套能力：

- 旧的静态状态识别 `tkinter` 演示程序，作为历史基线和对照工具
- 新的 `PyTorch GRU` 有监督时序分类后端，用于 4 类状态识别

## 当前后端方案

- 输入：`16` 帧时间窗口
- 每帧：`4` 组方位传感器
- 每组：`temperature / humidity / smoke / light`
- 模型：GRU 多分类器
- 输出：`STATE_*` 状态标签、中文显示名、置信度、各类概率

风险等级仍由现有独立规则模块负责，本轮没有并入 GRU 训练目标。

## 安装依赖

```powershell
pip install -r SequenceBackend\requirements.txt
```

## 训练 GRU 模型

```powershell
python SequenceBackend\train_gru.py
```

训练完成后会在 `SequenceBackend\artifacts\gru_backend\` 下生成：

- `gru_state_dict.pt`
- `metadata.json`

## 本地推理入口

当前目录保留的本地推理入口是 Python 函数：

- `sequence_classifier.py` 中的 `predict_sequence(bundle, sequence_window)`

输入 `sequence_window` 时仍要求提供完整 `16` 帧序列。

## 启动图形界面

新的 GRU 图形界面支持：

- 选择模拟场景
- 自动生成 `16` 帧模拟输入
- 本地模型预测
- 调用后端服务预测
- 查看逐帧概览、详细值和概率分布

启动方式：

```powershell
python SequenceBackend\app.py
```

旧的静态基线界面仍保留在：

```powershell
python SequenceBackend\static_demo_app.py
```

## 运行模拟示例程序

这个目录还提供了一个可直接运行的示例程序，会自动构造模拟输入序列并打印预测结果。

本地直接加载模型预测：

```powershell
python SequenceBackend\demo_simulation.py --scenario fire --mode local
```

调用已经启动的后端服务预测：

```powershell
python SequenceBackend\demo_simulation.py --scenario gas_leak --mode http
```

可选场景：

- `normal`
- `fire`
- `gas_leak`

如果本地模型产物不存在，`local` 模式会自动训练一次并生成模型文件。

## 测试

```powershell
python -m unittest discover -s SequenceBackend -p "test_*.py"
```

如果本机未安装 `torch`，GRU 训练与推理相关测试会自动跳过；旧静态基线测试和数据层测试仍可运行。
