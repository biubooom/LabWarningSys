from __future__ import annotations

import math
import random
from collections import Counter
from typing import Dict, Iterable, List, Sequence

try:
    from .model_types import (
        PredictionResult,
        StaticStateModel,
        STATE_DISPLAY_NAMES,
        STATE_ORDER,
        Sample,
    )
except ImportError:
    from model_types import (
        PredictionResult,
        StaticStateModel,
        STATE_DISPLAY_NAMES,
        STATE_ORDER,
        Sample,
    )


DEFAULT_K = 6
DEFAULT_MAX_ITERATIONS = 100
DEFAULT_TOLERANCE = 1e-4


def _extract_features(samples: Sequence[Sample]) -> List[List[float]]:
    return [list(sample.features) for sample in samples]


def _compute_feature_bounds(rows: Sequence[Sequence[float]]) -> tuple[List[float], List[float]]:
    feature_count = len(rows[0])
    feature_min = [min(row[i] for row in rows) for i in range(feature_count)]
    feature_max = [max(row[i] for row in rows) for i in range(feature_count)]
    return feature_min, feature_max


def _normalize_rows(
    rows: Sequence[Sequence[float]],
    feature_min: Sequence[float],
    feature_max: Sequence[float],
) -> List[List[float]]:
    normalized: List[List[float]] = []
    for row in rows:
        current = []
        for value, low, high in zip(row, feature_min, feature_max):
            scale = high - low
            if scale <= 0.0:
                current.append(0.0)
            else:
                current.append((value - low) / scale)
        normalized.append(current)
    return normalized


def _denormalize_center(
    center: Sequence[float],
    feature_min: Sequence[float],
    feature_max: Sequence[float],
) -> List[float]:
    values: List[float] = []
    for value, low, high in zip(center, feature_min, feature_max):
        values.append(low + (high - low) * value)
    return values


def _squared_distance(left: Sequence[float], right: Sequence[float]) -> float:
    return sum((a - b) * (a - b) for a, b in zip(left, right))


def _assign_clusters(rows: Sequence[Sequence[float]], centers: Sequence[Sequence[float]]) -> List[int]:
    assignments: List[int] = []
    for row in rows:
        best_cluster = min(range(len(centers)), key=lambda idx: _squared_distance(row, centers[idx]))
        assignments.append(best_cluster)
    return assignments


def _compute_centers(
    rows: Sequence[Sequence[float]],
    assignments: Sequence[int],
    k: int,
    previous_centers: Sequence[Sequence[float]],
    rng: random.Random,
) -> List[List[float]]:
    feature_count = len(rows[0])
    sums = [[0.0] * feature_count for _ in range(k)]
    counts = [0] * k

    for row, cluster_id in zip(rows, assignments):
        counts[cluster_id] += 1
        for i in range(feature_count):
            sums[cluster_id][i] += row[i]

    centers: List[List[float]] = []
    for cluster_id in range(k):
        if counts[cluster_id] == 0:
            centers.append(list(rng.choice(rows)))
            continue
        centers.append([value / counts[cluster_id] for value in sums[cluster_id]])
    return centers


def _init_centers_plus_plus(rows: Sequence[Sequence[float]], k: int, rng: random.Random) -> List[List[float]]:
    centers = [list(rng.choice(rows))]
    while len(centers) < k:
        distances = []
        for row in rows:
            nearest_distance = min(_squared_distance(row, center) for center in centers)
            distances.append(nearest_distance)

        total_distance = sum(distances)
        if total_distance <= 0.0:
            centers.append(list(rng.choice(rows)))
            continue

        target = rng.random() * total_distance
        cumulative = 0.0
        for row, distance in zip(rows, distances):
            cumulative += distance
            if cumulative >= target:
                centers.append(list(row))
                break
        else:
            centers.append(list(rows[-1]))

    return centers


def _map_clusters_to_labels(assignments: Sequence[int], samples: Sequence[Sample], k: int) -> Dict[int, str]:
    cluster_votes: Dict[int, Counter[str]] = {idx: Counter() for idx in range(k)}
    used_labels = set()
    cluster_label_map: Dict[int, str] = {}

    for cluster_id, sample in zip(assignments, samples):
        cluster_votes[cluster_id][sample.expected_label] += 1

    ranked_clusters = sorted(
        range(k),
        key=lambda cluster_id: cluster_votes[cluster_id].most_common(1)[0][1] if cluster_votes[cluster_id] else 0,
        reverse=True,
    )

    for cluster_id in ranked_clusters:
        votes = cluster_votes[cluster_id]
        chosen_label = None
        for label, _ in votes.most_common():
            if label not in used_labels:
                chosen_label = label
                break
        if chosen_label is None:
            for label in STATE_ORDER:
                if label not in used_labels:
                    chosen_label = label
                    break
        if chosen_label is None:
            chosen_label = STATE_ORDER[cluster_id % len(STATE_ORDER)]
        cluster_label_map[cluster_id] = chosen_label
        used_labels.add(chosen_label)

    for cluster_id in range(k):
        if cluster_id not in cluster_label_map:
            cluster_label_map[cluster_id] = STATE_ORDER[cluster_id % len(STATE_ORDER)]

    return cluster_label_map


def _training_counts(assignments: Sequence[int], samples: Sequence[Sample], cluster_label_map: Dict[int, str]) -> Dict[str, int]:
    counts = {label: 0 for label in STATE_ORDER}
    for cluster_id, _sample in zip(assignments, samples):
        counts[cluster_label_map[cluster_id]] += 1
    return counts


def train_static_state_model(
    samples: Sequence[Sample],
    k: int = DEFAULT_K,
    max_iterations: int = DEFAULT_MAX_ITERATIONS,
    tolerance: float = DEFAULT_TOLERANCE,
    seed: int = 42,
) -> StaticStateModel:
    if not samples:
        raise ValueError("samples must not be empty")
    if k <= 0:
        raise ValueError("k must be positive")

    raw_rows = _extract_features(samples)
    feature_min, feature_max = _compute_feature_bounds(raw_rows)
    rows = _normalize_rows(raw_rows, feature_min, feature_max)
    rng = random.Random(seed)
    centers = _init_centers_plus_plus(rows, k, rng)

    assignments = [0] * len(rows)
    iterations = 0
    for iteration in range(1, max_iterations + 1):
        assignments = _assign_clusters(rows, centers)
        new_centers = _compute_centers(rows, assignments, k, centers, rng)
        shift = sum(_squared_distance(old, new) for old, new in zip(centers, new_centers))
        centers = new_centers
        iterations = iteration
        if shift <= tolerance:
            break

    inertia = sum(_squared_distance(row, centers[cluster_id]) for row, cluster_id in zip(rows, assignments))
    cluster_label_map = _map_clusters_to_labels(assignments, samples, k)
    training_counts = _training_counts(assignments, samples, cluster_label_map)
    denormalized_centers = [_denormalize_center(center, feature_min, feature_max) for center in centers]

    return StaticStateModel(
        centers=denormalized_centers,
        normalized_centers=[list(center) for center in centers],
        feature_min=feature_min,
        feature_max=feature_max,
        assignments=list(assignments),
        cluster_label_map=cluster_label_map,
        training_counts=training_counts,
        inertia=inertia,
        iterations=iterations,
        seed=seed,
    )


def normalize_feature_vector(
    feature_vector: Sequence[float],
    feature_min: Sequence[float],
    feature_max: Sequence[float],
) -> List[float]:
    normalized: List[float] = []
    for value, low, high in zip(feature_vector, feature_min, feature_max):
        scale = high - low
        if scale <= 0.0:
            normalized.append(0.0)
        else:
            normalized.append((value - low) / scale)
    return normalized


def predict_static_state(model: StaticStateModel, feature_vector: Sequence[float]) -> PredictionResult:
    normalized_features = normalize_feature_vector(feature_vector, model.feature_min, model.feature_max)
    cluster_id = min(
        range(len(model.normalized_centers)),
        key=lambda idx: _squared_distance(normalized_features, model.normalized_centers[idx]),
    )
    distance = math.sqrt(_squared_distance(normalized_features, model.normalized_centers[cluster_id]))
    state_label = model.cluster_label_map[cluster_id]
    return PredictionResult(
        cluster_id=cluster_id,
        state_label=state_label,
        display_name=STATE_DISPLAY_NAMES[state_label],
        distance=distance,
        normalized_features=normalized_features,
    )


def build_static_confusion_summary(model: StaticStateModel, samples: Iterable[Sample]) -> Dict[str, Dict[str, int]]:
    summary: Dict[str, Dict[str, int]] = {
        expected: {predicted: 0 for predicted in STATE_ORDER}
        for expected in STATE_ORDER
    }
    for cluster_id, sample in zip(model.assignments, samples):
        predicted_label = model.cluster_label_map[cluster_id]
        summary[sample.expected_label][predicted_label] += 1
    return summary
