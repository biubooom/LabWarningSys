from __future__ import annotations

from typing import Dict, List, Sequence

try:
    from .dataset import build_rate_features, summarize_raw_groups
    from .static_state_model import normalize_feature_vector
    from .model_types import (
        RISK_ATTENTION,
        RISK_DANGER,
        RISK_DISPLAY_NAMES,
        RISK_NORMAL,
        RISK_WARNING,
        RiskAssessment,
        StaticStateModel,
        STATE_FIRE,
        STATE_GAS_LEAK,
    )
except ImportError:
    from dataset import build_rate_features, summarize_raw_groups
    from static_state_model import normalize_feature_vector
    from model_types import (
        RISK_ATTENTION,
        RISK_DANGER,
        RISK_DISPLAY_NAMES,
        RISK_NORMAL,
        RISK_WARNING,
        RiskAssessment,
        StaticStateModel,
        STATE_FIRE,
        STATE_GAS_LEAK,
    )


def _distance_to_state(model: StaticStateModel, feature_vector: Sequence[float], state_label: str) -> float | None:
    normalized = normalize_feature_vector(feature_vector, model.feature_min, model.feature_max)
    distances = []
    for cluster_id, center in enumerate(model.normalized_centers):
        if model.cluster_label_map[cluster_id] == state_label:
            distance = sum((left - right) * (left - right) for left, right in zip(normalized, center)) ** 0.5
            distances.append(distance)
    return min(distances) if distances else None


def assess_risk(
    model: StaticStateModel,
    feature_vector: Sequence[float],
    predicted_state: str,
    raw_groups: Sequence[Sequence[float]],
    previous_raw_groups: Sequence[Sequence[float]] | None,
) -> RiskAssessment:
    summary = summarize_raw_groups(raw_groups)
    rates = build_rate_features(raw_groups, previous_raw_groups)
    fire_distance = _distance_to_state(model, feature_vector, STATE_FIRE)
    gas_distance = _distance_to_state(model, feature_vector, STATE_GAS_LEAK)

    score = 0.0
    reasons: List[str] = []

    if summary["temperature_max"] >= 75.0:
        score += 4.0
        reasons.append("T_max 超过 75C")
    elif summary["temperature_max"] >= 65.0:
        score += 2.0
        reasons.append("T_max 持续偏高")

    if summary["smoke_max"] >= 85.0:
        score += 4.0
        reasons.append("S_max 超过 85")
    elif summary["smoke_max"] >= 65.0:
        score += 2.0
        reasons.append("S_max 偏高")

    if rates["temperature_max_rate"] >= 4.0:
        score += 2.0
        reasons.append("dT_max 快速上升")
    elif rates["temperature_avg_rate"] >= 2.0:
        score += 1.0
        reasons.append("dT_avg 上升")

    if rates["smoke_max_rate"] >= 6.0:
        score += 2.0
        reasons.append("dS_max 快速上升")
    elif rates["smoke_avg_rate"] >= 3.0:
        score += 1.0
        reasons.append("dS_avg 上升")

    if summary["temperature_spread"] >= 18.0:
        score += 1.5
        reasons.append("T_spread 较大")
    if summary["smoke_spread"] >= 18.0:
        score += 1.5
        reasons.append("S_spread 较大")

    if predicted_state == STATE_FIRE:
        score += 2.5
        reasons.append("聚类类型接近火灾")
    elif predicted_state == STATE_GAS_LEAK:
        score += 2.0
        reasons.append("聚类类型接近气体泄漏")

    if fire_distance is not None and fire_distance <= 0.55:
        score += 2.0
        reasons.append("距离火灾簇较近")
    elif fire_distance is not None and fire_distance <= 0.8:
        score += 1.0
        reasons.append("正在靠近火灾簇")

    if gas_distance is not None and gas_distance <= 0.55:
        score += 1.5
        reasons.append("距离气体泄漏簇较近")

    if summary["temperature_max"] >= 80.0 and summary["smoke_max"] >= 90.0:
        level = RISK_DANGER
    elif score >= 7.0:
        level = RISK_DANGER
    elif score >= 4.0:
        level = RISK_WARNING
    elif score >= 2.0:
        level = RISK_ATTENTION
    else:
        level = RISK_NORMAL

    if not reasons:
        reasons.append("环境参数整体平稳")

    return RiskAssessment(
        level=level,
        display_name=RISK_DISPLAY_NAMES[level],
        score=score,
        reasons=reasons,
    )
