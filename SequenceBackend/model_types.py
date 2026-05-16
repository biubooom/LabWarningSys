from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List


RAW_SENSOR_FIELDS = ("temperature", "humidity", "smoke", "light")
SENSOR_GROUP_COUNT = 4

CLUSTER_FEATURE_NAMES = (
    "temperature_avg",
    "humidity_avg",
    "smoke_avg",
    "light_avg",
    "temperature_max",
    "smoke_max",
    "light_max",
    "temperature_spread",
    "smoke_spread",
    "light_spread",
)
RATE_FEATURE_NAMES = (
    "temperature_avg_rate",
    "smoke_avg_rate",
    "temperature_max_rate",
    "smoke_max_rate",
)

FEATURE_NAMES = CLUSTER_FEATURE_NAMES
FEATURE_COUNT = len(FEATURE_NAMES)
SEQUENCE_LENGTH = 16
SENSOR_FIELD_COUNT = len(RAW_SENSOR_FIELDS)
FRAME_FEATURE_COUNT = SENSOR_GROUP_COUNT * SENSOR_FIELD_COUNT

FEATURE_INDEX = {name: index for index, name in enumerate(FEATURE_NAMES)}
RATE_INDEX = {name: index for index, name in enumerate(RATE_FEATURE_NAMES)}

STATE_NORMAL = "STATE_NORMAL"
STATE_FIRE = "STATE_FIRE"
STATE_GAS_LEAK = "STATE_GAS_LEAK"
STATE_HIGH_HUMID = "STATE_HIGH_HUMID"

STATE_DISPLAY_NAMES: Dict[str, str] = {
    STATE_NORMAL: "正常",
    STATE_FIRE: "火灾预警",
    STATE_GAS_LEAK: "烟雾预警",
    STATE_HIGH_HUMID: "高湿预警",
}

STATE_ORDER = (
    STATE_NORMAL,
    STATE_FIRE,
    STATE_GAS_LEAK,
    STATE_HIGH_HUMID,
)

ORIGIN_NONE = "ORIGIN_NONE"
ORIGIN_GROUP_1 = "ORIGIN_GROUP_1"
ORIGIN_GROUP_2 = "ORIGIN_GROUP_2"
ORIGIN_GROUP_3 = "ORIGIN_GROUP_3"
ORIGIN_GROUP_4 = "ORIGIN_GROUP_4"

ORIGIN_DISPLAY_NAMES: Dict[str, str] = {
    ORIGIN_NONE: "无明确源头",
    ORIGIN_GROUP_1: "第1组 / 东北角",
    ORIGIN_GROUP_2: "第2组 / 西北角",
    ORIGIN_GROUP_3: "第3组 / 东南角",
    ORIGIN_GROUP_4: "第4组 / 西南角",
}

ORIGIN_ORDER = (
    ORIGIN_NONE,
    ORIGIN_GROUP_1,
    ORIGIN_GROUP_2,
    ORIGIN_GROUP_3,
    ORIGIN_GROUP_4,
)

RISK_NORMAL = "LEVEL_NORMAL"
RISK_ATTENTION = "LEVEL_ATTENTION"
RISK_WARNING = "LEVEL_WARNING"
RISK_DANGER = "LEVEL_DANGER"

RISK_DISPLAY_NAMES: Dict[str, str] = {
    RISK_NORMAL: "正常",
    RISK_ATTENTION: "关注",
    RISK_WARNING: "预警",
    RISK_DANGER: "危险",
}


@dataclass(frozen=True)
class Sample:
    features: List[float]
    expected_label: str


@dataclass(frozen=True)
class Dataset:
    samples: List[Sample]
    feature_names: List[str]


@dataclass(frozen=True)
class SequenceSample:
    sequence: List[List[List[float]]]
    expected_label: str
    expected_origin_label: str


@dataclass(frozen=True)
class SequenceDataset:
    samples: List[SequenceSample]
    sequence_length: int
    sensor_group_count: int
    sensor_fields: List[str]


@dataclass(frozen=True)
class StaticStateModel:
    centers: List[List[float]]
    normalized_centers: List[List[float]]
    feature_min: List[float]
    feature_max: List[float]
    assignments: List[int]
    cluster_label_map: Dict[int, str]
    training_counts: Dict[str, int]
    inertia: float
    iterations: int
    seed: int


@dataclass(frozen=True)
class PredictionResult:
    cluster_id: int
    state_label: str
    display_name: str
    distance: float
    normalized_features: List[float]


@dataclass(frozen=True)
class RiskAssessment:
    level: str
    display_name: str
    score: float
    reasons: List[str]


@dataclass
class SequenceClassifierBundle:
    model: Any
    state_labels: List[str]
    origin_labels: List[str]
    feature_mean: List[float]
    feature_std: List[float]
    hidden_size: int
    num_layers: int
    input_size: int
    sequence_length: int
    state_validation_accuracy: float
    origin_validation_accuracy: float
    state_confusion_summary: Dict[str, Dict[str, int]]
    origin_confusion_summary: Dict[str, Dict[str, int]]
    artifact_dir: str
    model_path: str
    metadata_path: str


@dataclass(frozen=True)
class SequencePredictionResult:
    state_label: str
    display_name: str
    confidence: float
    probabilities: Dict[str, float]
    origin_label: str
    origin_display_name: str
    origin_confidence: float
    origin_probabilities: Dict[str, float]
