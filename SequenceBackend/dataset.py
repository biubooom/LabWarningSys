from __future__ import annotations

import itertools
import math
import random
from typing import Dict, List, Sequence, Tuple

try:
    from .model_types import (
        CLUSTER_FEATURE_NAMES,
        Dataset,
        FEATURE_NAMES,
        RAW_SENSOR_FIELDS,
        SENSOR_GROUP_COUNT,
        SEQUENCE_LENGTH,
        SENSOR_FIELD_COUNT,
        SequenceDataset,
        SequenceSample,
        STATE_DISPLAY_NAMES,
        STATE_FIRE,
        STATE_GAS_LEAK,
        STATE_HIGH_HUMID,
        STATE_NORMAL,
        STATE_ORDER,
        ORIGIN_GROUP_1,
        ORIGIN_GROUP_2,
        ORIGIN_GROUP_3,
        ORIGIN_GROUP_4,
        ORIGIN_NONE,
        Sample,
    )
except ImportError:
    from model_types import (
        CLUSTER_FEATURE_NAMES,
        Dataset,
        FEATURE_NAMES,
        RAW_SENSOR_FIELDS,
        SENSOR_GROUP_COUNT,
        SEQUENCE_LENGTH,
        SENSOR_FIELD_COUNT,
        SequenceDataset,
        SequenceSample,
        STATE_DISPLAY_NAMES,
        STATE_FIRE,
        STATE_GAS_LEAK,
        STATE_HIGH_HUMID,
        STATE_NORMAL,
        STATE_ORDER,
        ORIGIN_GROUP_1,
        ORIGIN_GROUP_2,
        ORIGIN_GROUP_3,
        ORIGIN_GROUP_4,
        ORIGIN_NONE,
        Sample,
    )


SAMPLES_PER_STATE = 100
DEFAULT_SEQUENCE_SAMPLE_TOTAL = 4000

BASE_FEATURE_LIMITS: Dict[str, Tuple[float, float]] = {
    "temperature": (10.0, 100.0),
    "humidity": (5.0, 100.0),
    "smoke": (0.0, 100.0),
    "light": (0.0, 100.0),
}

FEATURE_LIMITS: Dict[str, Tuple[float, float]] = {
    "temperature_avg": BASE_FEATURE_LIMITS["temperature"],
    "humidity_avg": BASE_FEATURE_LIMITS["humidity"],
    "smoke_avg": BASE_FEATURE_LIMITS["smoke"],
    "light_avg": BASE_FEATURE_LIMITS["light"],
    "temperature_max": BASE_FEATURE_LIMITS["temperature"],
    "smoke_max": BASE_FEATURE_LIMITS["smoke"],
    "light_max": BASE_FEATURE_LIMITS["light"],
    "temperature_spread": (0.0, BASE_FEATURE_LIMITS["temperature"][1] - BASE_FEATURE_LIMITS["temperature"][0]),
    "smoke_spread": (0.0, BASE_FEATURE_LIMITS["smoke"][1] - BASE_FEATURE_LIMITS["smoke"][0]),
    "light_spread": (0.0, BASE_FEATURE_LIMITS["light"][1] - BASE_FEATURE_LIMITS["light"][0]),
}

RATE_LIMITS: Dict[str, Tuple[float, float]] = {
    "temperature_avg_rate": (-30.0, 30.0),
    "smoke_avg_rate": (-40.0, 40.0),
    "temperature_max_rate": (-30.0, 30.0),
    "smoke_max_rate": (-40.0, 40.0),
}

GROUP_ORIGIN_LABELS = (
    ORIGIN_GROUP_1,
    ORIGIN_GROUP_2,
    ORIGIN_GROUP_3,
    ORIGIN_GROUP_4,
)

NORMAL_TEMPLATES: Tuple[Tuple[List[float], ...], ...] = (
    (
        [26.0, 48.0, 12.0, 44.0],
        [27.5, 50.0, 11.0, 47.0],
        [25.0, 46.0, 14.0, 43.0],
        [26.5, 49.0, 12.5, 45.5],
    ),
    (
        [25.5, 58.5, 2.8, 76.7],
        [26.2, 59.3, 1.8, 85.1],
        [22.9, 60.8, 1.4, 73.9],
        [23.8, 60.4, 2.0, 70.6],
    ),
    (
        [24.8, 56.0, 3.6, 68.0],
        [25.7, 57.2, 2.9, 79.5],
        [22.4, 58.8, 2.1, 66.4],
        [23.3, 58.1, 2.8, 64.7],
    ),
    (
        [27.2, 54.0, 4.8, 61.5],
        [28.0, 55.6, 4.1, 69.2],
        [24.6, 56.3, 3.2, 58.8],
        [25.4, 55.8, 3.8, 57.1],
    ),
    (
        [31.2, 57.5, 3.4, 78.0],
        [32.4, 58.8, 2.8, 86.5],
        [29.3, 60.1, 2.1, 74.2],
        [30.1, 59.4, 2.7, 71.8],
    ),
    (
        [30.8, 66.5, 3.2, 72.0],
        [31.6, 67.8, 2.7, 79.5],
        [28.9, 68.4, 2.0, 69.4],
        [29.7, 67.1, 2.5, 68.2],
    ),
    (
        [30.2, 55.6, 2.9, 88.4],
        [31.0, 56.3, 2.4, 94.2],
        [28.4, 57.1, 1.9, 84.8],
        [29.1, 56.8, 2.2, 82.7],
    ),
)

NORMAL_TEMPLATE_SPREADS: Tuple[List[float], ...] = (
    [1.8, 4.5, 3.5, 5.5],
    [1.2, 2.2, 0.8, 4.0],
    [1.4, 2.5, 0.9, 4.5],
    [1.6, 3.0, 1.2, 4.8],
    [1.4, 2.6, 0.9, 4.8],
    [1.5, 2.8, 0.9, 4.2],
    [1.4, 2.4, 0.8, 3.8],
)

FIRE_TEMPLATES: Tuple[Dict[str, List[float]], ...] = (
    {
        "origin": [82.0, 22.0, 92.0, 66.0],
        "adjacent": [58.0, 28.0, 61.0, 56.0],
        "distant": [36.0, 36.0, 26.0, 49.0],
    },
    {
        "origin": [68.0, 26.0, 78.0, 62.0],
        "adjacent": [49.0, 33.0, 52.0, 54.0],
        "distant": [34.0, 40.0, 20.0, 47.0],
    },
    {
        "origin": [56.0, 31.0, 66.0, 58.0],
        "adjacent": [46.0, 36.0, 44.0, 50.0],
        "distant": [32.0, 42.0, 18.0, 45.0],
    },
)

FIRE_TEMPLATE_SPREADS: Tuple[Dict[str, List[float]], ...] = (
    {
        "origin": [3.0, 3.0, 4.0, 5.0],
        "adjacent": [3.0, 4.0, 5.0, 5.0],
        "distant": [2.5, 4.0, 4.0, 5.0],
    },
    {
        "origin": [2.8, 3.2, 4.5, 4.8],
        "adjacent": [2.5, 3.5, 4.0, 4.5],
        "distant": [2.2, 3.8, 3.5, 4.2],
    },
    {
        "origin": [2.6, 3.4, 4.0, 4.2],
        "adjacent": [2.3, 3.4, 3.6, 4.0],
        "distant": [2.0, 3.6, 3.0, 3.8],
    },
)

FIRE_PATTERN_TARGETS: Dict[str, Dict[str, float]] = {
    "fire_temp_smoke_watch": {"temperature": 52.0, "humidity_drop": 8.0, "smoke": 28.0, "light": 0.0},
    "fire_temp_smoke_alert": {"temperature": 68.0, "humidity_drop": 16.0, "smoke": 58.0, "light": 0.0},
    "fire_temp_only_watch": {"temperature": 49.0, "humidity_drop": 6.0, "smoke": 0.0, "light": 0.0},
    "fire_temp_only_alert": {"temperature": 61.0, "humidity_drop": 10.0, "smoke": 0.0, "light": 0.0},
    "fire_temp_light_watch": {"temperature": 54.0, "humidity_drop": 8.0, "smoke": 0.0, "light": 80.0},
    "fire_temp_light_alert": {"temperature": 68.0, "humidity_drop": 12.0, "smoke": 0.0, "light": 92.0},
    "fire_temp_light_smoke_watch": {"temperature": 55.0, "humidity_drop": 10.0, "smoke": 26.0, "light": 82.0},
    "fire_temp_light_smoke_alert": {"temperature": 70.0, "humidity_drop": 16.0, "smoke": 56.0, "light": 94.0},
}

FIRE_PATTERN_SPREADS: Dict[str, Dict[str, float]] = {
    "fire_temp_smoke_watch": {"temperature": 2.2, "humidity": 2.4, "smoke": 2.4, "light": 1.4},
    "fire_temp_smoke_alert": {"temperature": 2.8, "humidity": 2.8, "smoke": 4.2, "light": 1.8},
    "fire_temp_only_watch": {"temperature": 2.0, "humidity": 2.2, "smoke": 0.8, "light": 1.2},
    "fire_temp_only_alert": {"temperature": 2.6, "humidity": 2.4, "smoke": 0.8, "light": 1.2},
    "fire_temp_light_watch": {"temperature": 2.2, "humidity": 2.4, "smoke": 0.8, "light": 3.0},
    "fire_temp_light_alert": {"temperature": 2.8, "humidity": 2.8, "smoke": 1.0, "light": 4.2},
    "fire_temp_light_smoke_watch": {"temperature": 2.4, "humidity": 2.6, "smoke": 2.2, "light": 3.0},
    "fire_temp_light_smoke_alert": {"temperature": 3.0, "humidity": 2.8, "smoke": 4.0, "light": 4.4},
}

FIRE_PATTERN_ORDER = tuple(FIRE_PATTERN_TARGETS.keys())
FIRE_ACTIVE_CORNER_COUNTS = (1, 2, 3, 4)
FIRE_REMAINDER_MODES = ("normal", "affected")

GAS_LEAK_PATTERN_TARGETS: Dict[str, Dict[str, float]] = {
    "gas_smoke_watch": {"temperature": 29.0, "humidity": 60.0, "smoke": 34.0, "light": 64.0},
    "gas_smoke_alert": {"temperature": 31.0, "humidity": 58.0, "smoke": 48.0, "light": 60.0},
}

HIGH_HUMID_PATTERN_TARGETS: Dict[str, Dict[str, float]] = {
    "humid_watch": {"temperature": 28.0, "humidity": 82.0, "smoke": 4.0, "light": 72.0},
    "humid_alert": {"temperature": 29.0, "humidity": 90.0, "smoke": 5.0, "light": 70.0},
}

GAS_LEAK_PATTERN_ORDER = tuple(GAS_LEAK_PATTERN_TARGETS.keys())
HIGH_HUMID_PATTERN_ORDER = tuple(HIGH_HUMID_PATTERN_TARGETS.keys())


def _clip_value(field_name: str, value: float) -> float:
    low, high = BASE_FEATURE_LIMITS[field_name]
    return max(low, min(high, value))


def _sample_group(rng: random.Random, center: Sequence[float], spread: Sequence[float]) -> List[float]:
    return [_clip_value(field_name, rng.gauss(center_value, spread_value)) for field_name, center_value, spread_value in zip(RAW_SENSOR_FIELDS, center, spread)]


def _origin_label_for_group_index(group_index: int) -> str:
    return GROUP_ORIGIN_LABELS[group_index]


def _choose_normal_template(rng: random.Random) -> tuple[Tuple[List[float], ...], List[float]]:
    template_index = rng.randrange(len(NORMAL_TEMPLATES))
    return NORMAL_TEMPLATES[template_index], NORMAL_TEMPLATE_SPREADS[template_index]


def _get_normal_template(template_index: int) -> tuple[Tuple[List[float], ...], List[float]]:
    return NORMAL_TEMPLATES[template_index], NORMAL_TEMPLATE_SPREADS[template_index]


def _choose_fire_template(rng: random.Random) -> tuple[Dict[str, List[float]], Dict[str, List[float]]]:
    template_index = rng.randrange(len(FIRE_TEMPLATES))
    return FIRE_TEMPLATES[template_index], FIRE_TEMPLATE_SPREADS[template_index]


def _choose_fire_pattern(rng: random.Random) -> str:
    return FIRE_PATTERN_ORDER[rng.randrange(len(FIRE_PATTERN_ORDER))]


def _choose_fire_active_groups(rng: random.Random) -> List[int]:
    active_count = FIRE_ACTIVE_CORNER_COUNTS[rng.randrange(len(FIRE_ACTIVE_CORNER_COUNTS))]
    return sorted(rng.sample(range(SENSOR_GROUP_COUNT), k=active_count))


def _choose_fire_remainder_mode(rng: random.Random) -> str:
    return FIRE_REMAINDER_MODES[rng.randrange(len(FIRE_REMAINDER_MODES))]


def _choose_active_groups(rng: random.Random) -> List[int]:
    active_count = FIRE_ACTIVE_CORNER_COUNTS[rng.randrange(len(FIRE_ACTIVE_CORNER_COUNTS))]
    return sorted(rng.sample(range(SENSOR_GROUP_COUNT), k=active_count))


def _fire_weight_for_selected_index(selected_index: int) -> float:
    return max(0.72, 1.0 - selected_index * 0.08)


def _build_fire_target_group(
    start_group: Sequence[float],
    pattern_key: str,
    selected_index: int,
) -> List[float]:
    pattern = FIRE_PATTERN_TARGETS[pattern_key]
    weight = _fire_weight_for_selected_index(selected_index)
    start_temperature, start_humidity, start_smoke, start_light = start_group

    target_temperature = max(46.0, start_temperature + (pattern["temperature"] - start_temperature) * weight)
    target_humidity = max(5.0, start_humidity - pattern["humidity_drop"] * weight)
    target_smoke = start_smoke
    target_light = start_light

    if pattern["smoke"] > 0.0:
        target_smoke = start_smoke + (pattern["smoke"] - start_smoke) * weight
    if pattern["light"] > 0.0:
        target_light = start_light + (pattern["light"] - start_light) * weight

    return [
        _clip_value("temperature", target_temperature),
        _clip_value("humidity", target_humidity),
        _clip_value("smoke", target_smoke),
        _clip_value("light", target_light),
    ]


def _build_fire_spread_for_group(start_group: Sequence[float], pattern_key: str) -> List[float]:
    pattern_spreads = FIRE_PATTERN_SPREADS[pattern_key]
    smoke_spread = pattern_spreads["smoke"] if FIRE_PATTERN_TARGETS[pattern_key]["smoke"] > 0.0 else 0.7
    light_spread = pattern_spreads["light"] if FIRE_PATTERN_TARGETS[pattern_key]["light"] > 0.0 else 1.2
    return [
        pattern_spreads["temperature"],
        pattern_spreads["humidity"],
        smoke_spread,
        light_spread,
    ]


def _build_fire_affected_group(
    start_group: Sequence[float],
    pattern_key: str,
    abnormal_count: int,
) -> List[float]:
    pattern = FIRE_PATTERN_TARGETS[pattern_key]
    influence_weight = 0.24 + abnormal_count * 0.06
    start_temperature, start_humidity, start_smoke, start_light = start_group

    target_temperature = max(start_temperature + 4.5, start_temperature + (pattern["temperature"] - start_temperature) * influence_weight)
    target_humidity = max(5.0, start_humidity - pattern["humidity_drop"] * min(influence_weight, 0.35))
    target_smoke = start_smoke
    target_light = start_light

    if pattern["smoke"] > 0.0:
        target_smoke = start_smoke + (pattern["smoke"] - start_smoke) * min(influence_weight, 0.42)
    if pattern["light"] > 0.0:
        target_light = start_light + (pattern["light"] - start_light) * min(influence_weight, 0.46)

    return [
        _clip_value("temperature", target_temperature),
        _clip_value("humidity", target_humidity),
        _clip_value("smoke", target_smoke),
        _clip_value("light", target_light),
    ]


def _build_fire_affected_spread(pattern_key: str) -> List[float]:
    pattern_spreads = FIRE_PATTERN_SPREADS[pattern_key]
    return [
        max(1.6, pattern_spreads["temperature"] * 0.75),
        max(1.8, pattern_spreads["humidity"] * 0.75),
        0.9 if FIRE_PATTERN_TARGETS[pattern_key]["smoke"] <= 0.0 else max(1.2, pattern_spreads["smoke"] * 0.5),
        1.4 if FIRE_PATTERN_TARGETS[pattern_key]["light"] <= 0.0 else max(1.8, pattern_spreads["light"] * 0.5),
    ]


def _build_fire_layout(
    rng: random.Random,
    start_groups: Sequence[Sequence[float]],
) -> Dict[str, object]:
    abnormal_group_indices = _choose_fire_active_groups(rng)
    pattern_key = _choose_fire_pattern(rng)
    remainder_mode = _choose_fire_remainder_mode(rng)
    return _build_fire_layout_from_spec(start_groups, pattern_key, abnormal_group_indices, remainder_mode)


def _build_fire_layout_from_spec(
    start_groups: Sequence[Sequence[float]],
    pattern_key: str,
    abnormal_group_indices: Sequence[int],
    remainder_mode: str,
) -> Dict[str, object]:
    target_groups = _clone_group_template(start_groups)
    spreads: List[List[float]] = []
    normalized_abnormal_indices = [int(group_index) for group_index in abnormal_group_indices]
    abnormal_index_lookup = {group_index: order for order, group_index in enumerate(normalized_abnormal_indices)}

    for group_index, group in enumerate(start_groups):
        if group_index in abnormal_index_lookup:
            target_groups[group_index] = _build_fire_target_group(
                group,
                pattern_key,
                abnormal_index_lookup[group_index],
            )
            spreads.append(_build_fire_spread_for_group(group, pattern_key))
        else:
            if remainder_mode == "affected":
                target_groups[group_index] = _build_fire_affected_group(
                    group,
                    pattern_key,
                    len(abnormal_group_indices),
                )
                spreads.append(_build_fire_affected_spread(pattern_key))
            else:
                spreads.append([1.4, 2.6, 0.8, 2.8])

    return {
        "pattern_key": pattern_key,
        "abnormal_group_indices": normalized_abnormal_indices,
        "remainder_mode": remainder_mode,
        "target_groups": target_groups,
        "spreads": spreads,
        "origin_label": _origin_label_for_group_index(normalized_abnormal_indices[0]),
    }


def _build_generic_abnormal_group(
    start_group: Sequence[float],
    target_template: Dict[str, float],
    selected_index: int,
) -> List[float]:
    weight = _fire_weight_for_selected_index(selected_index)
    start_temperature, start_humidity, start_smoke, start_light = start_group
    return [
        _clip_value("temperature", start_temperature + (target_template["temperature"] - start_temperature) * weight),
        _clip_value("humidity", start_humidity + (target_template["humidity"] - start_humidity) * weight),
        _clip_value("smoke", start_smoke + (target_template["smoke"] - start_smoke) * weight),
        _clip_value("light", start_light + (target_template["light"] - start_light) * weight),
    ]


def _build_generic_affected_group(
    start_group: Sequence[float],
    target_template: Dict[str, float],
    abnormal_count: int,
) -> List[float]:
    influence_weight = 0.22 + abnormal_count * 0.06
    start_temperature, start_humidity, start_smoke, start_light = start_group
    return [
        _clip_value("temperature", start_temperature + (target_template["temperature"] - start_temperature) * min(influence_weight, 0.42)),
        _clip_value("humidity", start_humidity + (target_template["humidity"] - start_humidity) * min(influence_weight, 0.46)),
        _clip_value("smoke", start_smoke + (target_template["smoke"] - start_smoke) * min(influence_weight, 0.46)),
        _clip_value("light", start_light + (target_template["light"] - start_light) * min(influence_weight, 0.46)),
    ]


def _build_generic_spread(
    *,
    temperature: float,
    humidity: float,
    smoke: float,
    light: float,
) -> List[float]:
    return [temperature, humidity, smoke, light]


def _build_generic_layout(
    rng: random.Random,
    start_groups: Sequence[Sequence[float]],
    target_key: str,
    target_template: Dict[str, float],
    abnormal_spread: List[float],
    affected_spread: List[float],
    normal_spread: List[float],
) -> Dict[str, object]:
    abnormal_group_indices = _choose_active_groups(rng)
    remainder_mode = _choose_fire_remainder_mode(rng)
    return _build_generic_layout_from_spec(
        start_groups,
        target_key,
        target_template,
        abnormal_spread,
        affected_spread,
        normal_spread,
        abnormal_group_indices,
        remainder_mode,
    )


def _build_generic_layout_from_spec(
    start_groups: Sequence[Sequence[float]],
    target_key: str,
    target_template: Dict[str, float],
    abnormal_spread: List[float],
    affected_spread: List[float],
    normal_spread: List[float],
    abnormal_group_indices: Sequence[int],
    remainder_mode: str,
) -> Dict[str, object]:
    target_groups = _clone_group_template(start_groups)
    spreads: List[List[float]] = []
    normalized_abnormal_indices = [int(group_index) for group_index in abnormal_group_indices]
    abnormal_index_lookup = {group_index: order for order, group_index in enumerate(normalized_abnormal_indices)}

    for group_index, group in enumerate(start_groups):
        if group_index in abnormal_index_lookup:
            target_groups[group_index] = _build_generic_abnormal_group(
                group,
                target_template,
                abnormal_index_lookup[group_index],
            )
            spreads.append(list(abnormal_spread))
        elif remainder_mode == "affected":
            target_groups[group_index] = _build_generic_affected_group(
                group,
                target_template,
                len(abnormal_group_indices),
            )
            spreads.append(list(affected_spread))
        else:
            spreads.append(list(normal_spread))

    return {
        "target_key": target_key,
        "abnormal_group_indices": normalized_abnormal_indices,
        "remainder_mode": remainder_mode,
        "target_groups": target_groups,
        "spreads": spreads,
        "origin_label": _origin_label_for_group_index(normalized_abnormal_indices[0]),
    }


def _build_group_combinations() -> List[Tuple[int, ...]]:
    combinations: List[Tuple[int, ...]] = []
    for active_count in FIRE_ACTIVE_CORNER_COUNTS:
        combinations.extend(tuple(indices) for indices in itertools.combinations(range(SENSOR_GROUP_COUNT), active_count))
    return combinations


def _build_sequence_template_specs() -> List[Dict[str, object]]:
    specs: List[Dict[str, object]] = []
    for template_index in range(len(NORMAL_TEMPLATES)):
        specs.append({
            "state_label": STATE_NORMAL,
            "normal_template_index": template_index,
        })

    group_combinations = _build_group_combinations()

    for pattern_key in FIRE_PATTERN_ORDER:
        for abnormal_group_indices in group_combinations:
            for remainder_mode in FIRE_REMAINDER_MODES:
                specs.append({
                    "state_label": STATE_FIRE,
                    "fire_pattern_key": pattern_key,
                    "abnormal_group_indices": list(abnormal_group_indices),
                    "remainder_mode": remainder_mode,
                })

    for state_label, target_keys in (
        (STATE_GAS_LEAK, GAS_LEAK_PATTERN_ORDER),
        (STATE_HIGH_HUMID, HIGH_HUMID_PATTERN_ORDER),
    ):
        for target_template_key in target_keys:
            for abnormal_group_indices in group_combinations:
                for remainder_mode in FIRE_REMAINDER_MODES:
                    specs.append({
                        "state_label": state_label,
                        "generic_target_key": target_template_key,
                        "abnormal_group_indices": list(abnormal_group_indices),
                        "remainder_mode": remainder_mode,
                    })
    return specs


def summarize_raw_groups(raw_groups: Sequence[Sequence[float]]) -> Dict[str, float]:
    temperature_values = [group[0] for group in raw_groups]
    humidity_values = [group[1] for group in raw_groups]
    smoke_values = [group[2] for group in raw_groups]
    light_values = [group[3] for group in raw_groups]
    return {
        "temperature_avg": sum(temperature_values) / len(temperature_values),
        "humidity_avg": sum(humidity_values) / len(humidity_values),
        "smoke_avg": sum(smoke_values) / len(smoke_values),
        "light_avg": sum(light_values) / len(light_values),
        "temperature_max": max(temperature_values),
        "smoke_max": max(smoke_values),
        "light_max": max(light_values),
        "temperature_spread": max(temperature_values) - min(temperature_values),
        "smoke_spread": max(smoke_values) - min(smoke_values),
        "light_spread": max(light_values) - min(light_values),
    }


def build_feature_vector(raw_groups: Sequence[Sequence[float]]) -> List[float]:
    if len(raw_groups) != SENSOR_GROUP_COUNT:
        raise ValueError(f"raw_groups must contain {SENSOR_GROUP_COUNT} groups")
    summary = summarize_raw_groups(raw_groups)
    return [summary[name] for name in CLUSTER_FEATURE_NAMES]


def build_rate_features(raw_groups: Sequence[Sequence[float]], previous_raw_groups: Sequence[Sequence[float]] | None = None) -> Dict[str, float]:
    current_summary = summarize_raw_groups(raw_groups)
    if previous_raw_groups is None:
        previous_summary = current_summary
    else:
        previous_summary = summarize_raw_groups(previous_raw_groups)
    return {
        "temperature_avg_rate": current_summary["temperature_avg"] - previous_summary["temperature_avg"],
        "smoke_avg_rate": current_summary["smoke_avg"] - previous_summary["smoke_avg"],
        "temperature_max_rate": current_summary["temperature_max"] - previous_summary["temperature_max"],
        "smoke_max_rate": current_summary["smoke_max"] - previous_summary["smoke_max"],
    }


def validate_raw_groups(raw_groups: Sequence[Sequence[float]]) -> List[List[float]]:
    if len(raw_groups) != SENSOR_GROUP_COUNT:
        raise ValueError(f"raw_groups must contain {SENSOR_GROUP_COUNT} groups")

    normalized_groups: List[List[float]] = []
    for group in raw_groups:
        if len(group) != SENSOR_FIELD_COUNT:
            raise ValueError(f"each group must contain {SENSOR_FIELD_COUNT} sensor values")
        normalized_values: List[float] = []
        for field_name, value in zip(RAW_SENSOR_FIELDS, group):
            normalized_values.append(_clip_value(field_name, float(value)))
        normalized_groups.append(normalized_values)
    return normalized_groups


def validate_raw_sequence(sequence: Sequence[Sequence[Sequence[float]]], sequence_length: int = SEQUENCE_LENGTH) -> List[List[List[float]]]:
    if len(sequence) != sequence_length:
        raise ValueError(f"sequence must contain {sequence_length} frames")
    return [validate_raw_groups(frame) for frame in sequence]


def flatten_raw_groups(raw_groups: Sequence[Sequence[float]]) -> List[float]:
    normalized_groups = validate_raw_groups(raw_groups)
    return [value for group in normalized_groups for value in group]


def flatten_raw_sequence(sequence: Sequence[Sequence[Sequence[float]]], sequence_length: int = SEQUENCE_LENGTH) -> List[List[float]]:
    normalized_sequence = validate_raw_sequence(sequence, sequence_length=sequence_length)
    return [flatten_raw_groups(frame) for frame in normalized_sequence]


def _generate_normal_sample(rng: random.Random) -> tuple[List[List[float]], str]:
    centers, spread = _choose_normal_template(rng)
    return [_sample_group(rng, center, spread) for center in centers], ORIGIN_NONE


def _generate_fire_sample(rng: random.Random) -> tuple[List[List[float]], str]:
    start_groups = _clone_group_template(_choose_normal_template(rng)[0])
    fire_layout = _build_fire_layout(rng, start_groups)
    target_groups = fire_layout["target_groups"]
    spreads = fire_layout["spreads"]
    groups = [
        _sample_group(rng, center, spread)
        for center, spread in zip(target_groups, spreads)
    ]
    return groups, str(fire_layout["origin_label"])


def _generate_gas_leak_sample(rng: random.Random) -> tuple[List[List[float]], str]:
    start_groups = _clone_group_template(_choose_normal_template(rng)[0])
    target_key = GAS_LEAK_PATTERN_ORDER[rng.randrange(len(GAS_LEAK_PATTERN_ORDER))]
    gas_layout = _build_generic_layout(
        rng,
        start_groups,
        target_key,
        GAS_LEAK_PATTERN_TARGETS[target_key],
        _build_generic_spread(temperature=2.0, humidity=4.5, smoke=4.5, light=4.0),
        _build_generic_spread(temperature=1.6, humidity=3.4, smoke=2.2, light=2.0),
        _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
    )
    groups = [
        _sample_group(rng, center, spread)
        for center, spread in zip(gas_layout["target_groups"], gas_layout["spreads"])
    ]
    return groups, str(gas_layout["origin_label"])


def _generate_high_humid_sample(rng: random.Random) -> tuple[List[List[float]], str]:
    start_groups = _clone_group_template(_choose_normal_template(rng)[0])
    target_key = HIGH_HUMID_PATTERN_ORDER[rng.randrange(len(HIGH_HUMID_PATTERN_ORDER))]
    humid_layout = _build_generic_layout(
        rng,
        start_groups,
        target_key,
        HIGH_HUMID_PATTERN_TARGETS[target_key],
        _build_generic_spread(temperature=2.0, humidity=2.8, smoke=3.0, light=3.5),
        _build_generic_spread(temperature=1.6, humidity=2.2, smoke=1.8, light=2.0),
        _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
    )
    groups = [
        _sample_group(rng, center, spread)
        for center, spread in zip(humid_layout["target_groups"], humid_layout["spreads"])
    ]
    return groups, str(humid_layout["origin_label"])


def _generate_raw_groups(rng: random.Random, state_label: str) -> tuple[List[List[float]], str]:
    if state_label == STATE_NORMAL:
        return _generate_normal_sample(rng)
    if state_label == STATE_FIRE:
        return _generate_fire_sample(rng)
    if state_label == STATE_GAS_LEAK:
        return _generate_gas_leak_sample(rng)
    if state_label == STATE_HIGH_HUMID:
        return _generate_high_humid_sample(rng)
    return _generate_normal_sample(rng)


def _clone_group_template(groups: Sequence[Sequence[float]]) -> List[List[float]]:
    return [[float(value) for value in group] for group in groups]


def _normal_reference_centers() -> List[List[float]]:
    return _clone_group_template(NORMAL_TEMPLATES[0])


def _state_target_centers(rng: random.Random, state_label: str) -> tuple[List[List[float]], str]:
    if state_label == STATE_NORMAL:
        return _normal_reference_centers(), ORIGIN_NONE
    if state_label == STATE_FIRE:
        start_groups = _clone_group_template(_choose_normal_template(rng)[0])
        fire_layout = _build_fire_layout(rng, start_groups)
        return _clone_group_template(fire_layout["target_groups"]), str(fire_layout["origin_label"])
    if state_label == STATE_GAS_LEAK:
        start_groups = _clone_group_template(_choose_normal_template(rng)[0])
        target_key = GAS_LEAK_PATTERN_ORDER[rng.randrange(len(GAS_LEAK_PATTERN_ORDER))]
        gas_layout = _build_generic_layout(
            rng,
            start_groups,
            target_key,
            GAS_LEAK_PATTERN_TARGETS[target_key],
            _build_generic_spread(temperature=2.0, humidity=4.5, smoke=4.5, light=4.0),
            _build_generic_spread(temperature=1.6, humidity=3.4, smoke=2.2, light=2.0),
            _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
        )
        return _clone_group_template(gas_layout["target_groups"]), str(gas_layout["origin_label"])
    if state_label == STATE_HIGH_HUMID:
        start_groups = _clone_group_template(_choose_normal_template(rng)[0])
        target_key = HIGH_HUMID_PATTERN_ORDER[rng.randrange(len(HIGH_HUMID_PATTERN_ORDER))]
        humid_layout = _build_generic_layout(
            rng,
            start_groups,
            target_key,
            HIGH_HUMID_PATTERN_TARGETS[target_key],
            _build_generic_spread(temperature=2.0, humidity=2.8, smoke=3.0, light=3.5),
            _build_generic_spread(temperature=1.6, humidity=2.2, smoke=1.8, light=2.0),
            _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
        )
        return _clone_group_template(humid_layout["target_groups"]), str(humid_layout["origin_label"])

    return _normal_reference_centers(), ORIGIN_NONE


def _smooth_progress(step_index: int, sequence_length: int) -> float:
    if sequence_length <= 1:
        return 1.0
    linear_progress = step_index / float(sequence_length - 1)
    return linear_progress * linear_progress * (3.0 - 2.0 * linear_progress)


def _interpolate_groups(
    start_groups: Sequence[Sequence[float]],
    end_groups: Sequence[Sequence[float]],
    progress: float,
) -> List[List[float]]:
    groups: List[List[float]] = []
    for start_group, end_group in zip(start_groups, end_groups):
        groups.append([
            start_value + (end_value - start_value) * progress
            for start_value, end_value in zip(start_group, end_group)
        ])
    return groups


def _sample_temporal_groups(
    rng: random.Random,
    mean_groups: Sequence[Sequence[float]],
    jitter_scale: float,
) -> List[List[float]]:
    sampled_groups: List[List[float]] = []
    for group in mean_groups:
        sampled_group: List[float] = []
        for field_name, value in zip(RAW_SENSOR_FIELDS, group):
            low, high = BASE_FEATURE_LIMITS[field_name]
            sigma = max((high - low) * jitter_scale, 0.1)
            sampled_group.append(_clip_value(field_name, rng.gauss(value, sigma)))
        sampled_groups.append(sampled_group)
    return sampled_groups


def generate_labeled_sequence_for_state(
    rng: random.Random,
    state_label: str,
    sequence_length: int = SEQUENCE_LENGTH,
    template_spec: Dict[str, object] | None = None,
) -> tuple[List[List[List[float]]], str]:
    if template_spec is not None and "normal_template_index" in template_spec:
        start_groups = _clone_group_template(_get_normal_template(int(template_spec["normal_template_index"]))[0])
    else:
        start_groups = _clone_group_template(_choose_normal_template(rng)[0])
    fire_layout = None
    generic_layout = None
    if state_label == STATE_NORMAL:
        target_groups = _clone_group_template(start_groups)
        origin_label = ORIGIN_NONE
    elif state_label == STATE_FIRE:
        if template_spec is not None:
            fire_layout = _build_fire_layout_from_spec(
                start_groups,
                str(template_spec["fire_pattern_key"]),
                list(template_spec["abnormal_group_indices"]),
                str(template_spec["remainder_mode"]),
            )
        else:
            fire_layout = _build_fire_layout(rng, start_groups)
        target_groups = _clone_group_template(fire_layout["target_groups"])
        origin_label = str(fire_layout["origin_label"])
    elif state_label == STATE_GAS_LEAK:
        gas_target_key = str(template_spec["generic_target_key"]) if template_spec is not None else GAS_LEAK_PATTERN_ORDER[rng.randrange(len(GAS_LEAK_PATTERN_ORDER))]
        generic_layout = _build_generic_layout_from_spec(
            start_groups,
            gas_target_key,
            GAS_LEAK_PATTERN_TARGETS[gas_target_key],
            _build_generic_spread(temperature=2.0, humidity=4.5, smoke=4.5, light=4.0),
            _build_generic_spread(temperature=1.6, humidity=3.4, smoke=2.2, light=2.0),
            _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
            list(template_spec["abnormal_group_indices"]) if template_spec is not None else _choose_active_groups(rng),
            str(template_spec["remainder_mode"]) if template_spec is not None else _choose_fire_remainder_mode(rng),
        )
        target_groups = _clone_group_template(generic_layout["target_groups"])
        origin_label = str(generic_layout["origin_label"])
    elif state_label == STATE_HIGH_HUMID:
        humid_target_key = str(template_spec["generic_target_key"]) if template_spec is not None else HIGH_HUMID_PATTERN_ORDER[rng.randrange(len(HIGH_HUMID_PATTERN_ORDER))]
        generic_layout = _build_generic_layout_from_spec(
            start_groups,
            humid_target_key,
            HIGH_HUMID_PATTERN_TARGETS[humid_target_key],
            _build_generic_spread(temperature=2.0, humidity=2.8, smoke=3.0, light=3.5),
            _build_generic_spread(temperature=1.6, humidity=2.2, smoke=1.8, light=2.0),
            _build_generic_spread(temperature=1.4, humidity=2.6, smoke=0.8, light=2.8),
            list(template_spec["abnormal_group_indices"]) if template_spec is not None else _choose_active_groups(rng),
            str(template_spec["remainder_mode"]) if template_spec is not None else _choose_fire_remainder_mode(rng),
        )
        target_groups = _clone_group_template(generic_layout["target_groups"])
        origin_label = str(generic_layout["origin_label"])
    else:
        target_groups, origin_label = _state_target_centers(rng, state_label)
    sequence: List[List[List[float]]] = []

    for step_index in range(sequence_length):
        progress = _smooth_progress(step_index, sequence_length)
        if state_label == STATE_NORMAL:
            drifted_groups: List[List[float]] = []
            for group_index, group in enumerate(start_groups):
                drifted_group: List[float] = []
                for sensor_index, (field_name, value) in enumerate(zip(RAW_SENSOR_FIELDS, group)):
                    phase = 0.45 * step_index + group_index * 0.8 + sensor_index * 0.6
                    drift = math.sin(phase) * (0.9 if field_name != "smoke" else 0.6)
                    drifted_group.append(_clip_value(field_name, value + drift))
                drifted_groups.append(drifted_group)
            sequence.append(_sample_temporal_groups(rng, drifted_groups, jitter_scale=0.015))
            continue

        mean_groups = _interpolate_groups(start_groups, target_groups, progress)
        if state_label == STATE_FIRE:
            assert fire_layout is not None
            pattern_key = str(fire_layout["pattern_key"])
            abnormal_group_indices = set(int(group_index) for group_index in fire_layout["abnormal_group_indices"])
            remainder_mode = str(fire_layout["remainder_mode"])
            pattern_targets = FIRE_PATTERN_TARGETS[pattern_key]
            temperature_ramp = 6.0 * (progress ** 1.35)
            smoke_ramp = 5.0 * (progress ** 1.25) if pattern_targets["smoke"] > 0.0 else 0.0
            light_ramp = 8.0 * (progress ** 1.28) if pattern_targets["light"] > 0.0 else 0.0
            affected_temperature_ramp = 2.2 * (progress ** 1.22)
            affected_smoke_ramp = 1.6 * (progress ** 1.18) if pattern_targets["smoke"] > 0.0 else 0.0
            affected_light_ramp = 2.8 * (progress ** 1.2) if pattern_targets["light"] > 0.0 else 0.0

            for group_index, group in enumerate(mean_groups):
                if group_index in abnormal_group_indices:
                    group[0] = _clip_value("temperature", max(group[0] + temperature_ramp, 46.0 if progress >= 0.72 else group[0]))
                    if smoke_ramp > 0.0:
                        group[2] = _clip_value("smoke", group[2] + smoke_ramp)
                    if light_ramp > 0.0:
                        group[3] = _clip_value("light", group[3] + light_ramp)
                elif remainder_mode == "affected":
                    group[0] = _clip_value("temperature", group[0] + affected_temperature_ramp)
                    if affected_smoke_ramp > 0.0:
                        group[2] = _clip_value("smoke", group[2] + affected_smoke_ramp)
                    if affected_light_ramp > 0.0:
                        group[3] = _clip_value("light", group[3] + affected_light_ramp)
        elif state_label in (STATE_GAS_LEAK, STATE_HIGH_HUMID):
            assert generic_layout is not None
            generic_target_key = str(generic_layout.get("target_key", ""))
            abnormal_group_indices = set(int(group_index) for group_index in generic_layout["abnormal_group_indices"])
            remainder_mode = str(generic_layout["remainder_mode"])
            if state_label == STATE_GAS_LEAK:
                abnormal_smoke_ramp = (4.2 if generic_target_key.endswith("watch") else 6.2) * (progress ** 1.24)
                affected_smoke_ramp = (1.4 if generic_target_key.endswith("watch") else 2.0) * (progress ** 1.18)
            else:
                abnormal_smoke_ramp = 0.0
                affected_smoke_ramp = 0.0
            abnormal_humidity_ramp = (
                (3.2 if generic_target_key.endswith("watch") else 5.2) * (progress ** 1.22)
                if state_label == STATE_HIGH_HUMID else 0.0
            )
            affected_humidity_ramp = (
                (1.0 if generic_target_key.endswith("watch") else 2.0) * (progress ** 1.18)
                if state_label == STATE_HIGH_HUMID else 0.0
            )

            for group_index, group in enumerate(mean_groups):
                if group_index in abnormal_group_indices:
                    if abnormal_smoke_ramp > 0.0:
                        group[2] = _clip_value("smoke", group[2] + abnormal_smoke_ramp)
                    if abnormal_humidity_ramp > 0.0:
                        group[1] = _clip_value("humidity", group[1] + abnormal_humidity_ramp)
                elif remainder_mode == "affected":
                    if affected_smoke_ramp > 0.0:
                        group[2] = _clip_value("smoke", group[2] + affected_smoke_ramp)
                    if affected_humidity_ramp > 0.0:
                        group[1] = _clip_value("humidity", group[1] + affected_humidity_ramp)
        sequence.append(_sample_temporal_groups(rng, mean_groups, jitter_scale=0.02))

    return sequence, origin_label


def generate_sequence_for_state(
    rng: random.Random,
    state_label: str,
    sequence_length: int = SEQUENCE_LENGTH,
) -> List[List[List[float]]]:
    sequence, _origin_label = generate_labeled_sequence_for_state(rng, state_label, sequence_length=sequence_length)
    return sequence


def generate_samples(seed: int = 42) -> Dataset:
    rng = random.Random(seed)
    samples: List[Sample] = []

    for state_label in STATE_ORDER:
        for _ in range(SAMPLES_PER_STATE):
            current_groups, _origin_label = _generate_raw_groups(rng, state_label)
            samples.append(Sample(features=build_feature_vector(current_groups), expected_label=state_label))

    rng.shuffle(samples)
    return Dataset(samples=samples, feature_names=list(FEATURE_NAMES))


def generate_sequence_samples(
    seed: int = 42,
    sequence_length: int = SEQUENCE_LENGTH,
    samples_per_state: int | None = None,
    total_samples: int = DEFAULT_SEQUENCE_SAMPLE_TOTAL,
) -> SequenceDataset:
    rng = random.Random(seed)
    samples: List[SequenceSample] = []

    if samples_per_state is not None:
        for state_label in STATE_ORDER:
            for _ in range(samples_per_state):
                sequence, origin_label = generate_labeled_sequence_for_state(rng, state_label, sequence_length=sequence_length)
                samples.append(SequenceSample(sequence=sequence, expected_label=state_label, expected_origin_label=origin_label))
    else:
        template_specs = _build_sequence_template_specs()
        if not template_specs:
            raise ValueError("template_specs must not be empty")

        per_template_base = max(1, total_samples // len(template_specs))
        remainder = max(0, total_samples - per_template_base * len(template_specs))
        rng.shuffle(template_specs)

        for template_index, template_spec in enumerate(template_specs):
            sample_count = per_template_base + (1 if template_index < remainder else 0)
            state_label = str(template_spec["state_label"])
            for _ in range(sample_count):
                sequence, origin_label = generate_labeled_sequence_for_state(
                    rng,
                    state_label,
                    sequence_length=sequence_length,
                    template_spec=template_spec,
                )
                samples.append(SequenceSample(sequence=sequence, expected_label=state_label, expected_origin_label=origin_label))

    rng.shuffle(samples)
    return SequenceDataset(
        samples=samples,
        sequence_length=sequence_length,
        sensor_group_count=SENSOR_GROUP_COUNT,
        sensor_fields=list(RAW_SENSOR_FIELDS),
    )


def summarize_expected_counts(dataset: Dataset) -> Dict[str, int]:
    summary = {label: 0 for label in STATE_ORDER}
    for sample in dataset.samples:
        summary[sample.expected_label] += 1
    return summary


def summarize_expected_sequence_counts(dataset: SequenceDataset) -> Dict[str, int]:
    summary = {label: 0 for label in STATE_ORDER}
    for sample in dataset.samples:
        summary[sample.expected_label] += 1
    return summary


def state_display_name(state_label: str) -> str:
    return STATE_DISPLAY_NAMES[state_label]
