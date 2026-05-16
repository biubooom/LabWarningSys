from __future__ import annotations

import copy
import json
import random
from pathlib import Path
from typing import Dict, List, Sequence

try:
    import torch
    from torch import nn
    from torch.utils.data import DataLoader, TensorDataset
except ImportError:
    torch = None
    nn = None
    DataLoader = None
    TensorDataset = None

try:
    from .dataset import flatten_raw_sequence, generate_sequence_samples, validate_raw_sequence
    from .model_types import (
        FRAME_FEATURE_COUNT,
        ORIGIN_DISPLAY_NAMES,
        ORIGIN_ORDER,
        STATE_DISPLAY_NAMES,
        STATE_ORDER,
        SequenceClassifierBundle,
        SequenceDataset,
        SequencePredictionResult,
        SequenceSample,
    )
except ImportError:
    from dataset import flatten_raw_sequence, generate_sequence_samples, validate_raw_sequence
    from model_types import (
        FRAME_FEATURE_COUNT,
        ORIGIN_DISPLAY_NAMES,
        ORIGIN_ORDER,
        STATE_DISPLAY_NAMES,
        STATE_ORDER,
        SequenceClassifierBundle,
        SequenceDataset,
        SequencePredictionResult,
        SequenceSample,
    )


DEFAULT_HIDDEN_SIZE = 64
DEFAULT_NUM_LAYERS = 1
DEFAULT_EPOCHS = 18
DEFAULT_BATCH_SIZE = 32
DEFAULT_LEARNING_RATE = 1e-3
DEFAULT_VALIDATION_RATIO = 0.2
DEFAULT_ARTIFACT_DIR = Path(__file__).resolve().parent / "artifacts" / "gru_backend"
DEFAULT_MODEL_FILENAME = "gru_state_dict.pt"
DEFAULT_METADATA_FILENAME = "metadata.json"


def _require_torch() -> None:
    if torch is None or nn is None or DataLoader is None or TensorDataset is None:
        raise RuntimeError("PyTorch is required for GRU training and inference. Install the 'torch' package first.")


def _set_random_seed(seed: int) -> None:
    random.seed(seed)
    if torch is not None:
        torch.manual_seed(seed)


def _stratified_split(
    samples: Sequence[SequenceSample],
    validation_ratio: float,
    seed: int,
) -> tuple[List[SequenceSample], List[SequenceSample]]:
    grouped: Dict[str, List[SequenceSample]] = {label: [] for label in STATE_ORDER}
    for sample in samples:
        grouped[sample.expected_label].append(sample)

    rng = random.Random(seed)
    train_samples: List[SequenceSample] = []
    validation_samples: List[SequenceSample] = []
    for label in STATE_ORDER:
        current = list(grouped[label])
        rng.shuffle(current)
        validation_count = max(1, int(len(current) * validation_ratio))
        validation_samples.extend(current[:validation_count])
        train_samples.extend(current[validation_count:])

    rng.shuffle(train_samples)
    rng.shuffle(validation_samples)
    return train_samples, validation_samples


def _extract_sequences(samples: Sequence[SequenceSample], sequence_length: int) -> List[List[List[float]]]:
    return [flatten_raw_sequence(sample.sequence, sequence_length=sequence_length) for sample in samples]


def _extract_state_labels(samples: Sequence[SequenceSample]) -> List[int]:
    label_to_index = {label: index for index, label in enumerate(STATE_ORDER)}
    return [label_to_index[sample.expected_label] for sample in samples]


def _extract_origin_labels(samples: Sequence[SequenceSample]) -> List[int]:
    label_to_index = {label: index for index, label in enumerate(ORIGIN_ORDER)}
    return [label_to_index[sample.expected_origin_label] for sample in samples]


def _compute_normalization(sequences: Sequence[Sequence[Sequence[float]]]) -> tuple[List[float], List[float]]:
    if not sequences:
        raise ValueError("sequences must not be empty")

    feature_sum = [0.0] * FRAME_FEATURE_COUNT
    feature_squared_sum = [0.0] * FRAME_FEATURE_COUNT
    count = 0
    for sequence in sequences:
        for frame in sequence:
            count += 1
            for feature_index, value in enumerate(frame):
                feature_sum[feature_index] += value
                feature_squared_sum[feature_index] += value * value

    feature_mean: List[float] = []
    feature_std: List[float] = []
    for feature_index in range(FRAME_FEATURE_COUNT):
        mean = feature_sum[feature_index] / count
        variance = max(feature_squared_sum[feature_index] / count - mean * mean, 1e-6)
        feature_mean.append(mean)
        feature_std.append(variance ** 0.5)
    return feature_mean, feature_std


def _normalize_sequences(
    sequences: Sequence[Sequence[Sequence[float]]],
    feature_mean: Sequence[float],
    feature_std: Sequence[float],
) -> List[List[List[float]]]:
    normalized_sequences: List[List[List[float]]] = []
    for sequence in sequences:
        normalized_frames: List[List[float]] = []
        for frame in sequence:
            normalized_frames.append([
                (value - mean) / std if std > 0.0 else 0.0
                for value, mean, std in zip(frame, feature_mean, feature_std)
            ])
        normalized_sequences.append(normalized_frames)
    return normalized_sequences


def _build_confusion_summary(
    expected_indices: Sequence[int],
    predicted_indices: Sequence[int],
    labels: Sequence[str],
) -> Dict[str, Dict[str, int]]:
    summary: Dict[str, Dict[str, int]] = {
        expected_label: {predicted_label: 0 for predicted_label in labels}
        for expected_label in labels
    }
    for expected_index, predicted_index in zip(expected_indices, predicted_indices):
        summary[labels[expected_index]][labels[predicted_index]] += 1
    return summary


def _accuracy(expected_indices: Sequence[int], predicted_indices: Sequence[int]) -> float:
    if not expected_indices:
        return 0.0
    correct = sum(1 for expected, predicted in zip(expected_indices, predicted_indices) if expected == predicted)
    return correct / len(expected_indices)


if nn is not None:
    class GRUSequenceClassifier(nn.Module):
        def __init__(self, input_size: int, hidden_size: int, num_layers: int, num_state_classes: int, num_origin_classes: int) -> None:
            super().__init__()
            self.gru = nn.GRU(
                input_size=input_size,
                hidden_size=hidden_size,
                num_layers=num_layers,
                batch_first=True,
            )
            self.state_classifier = nn.Linear(hidden_size, num_state_classes)
            self.origin_classifier = nn.Linear(hidden_size, num_origin_classes)

        def forward(self, inputs: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
            outputs, hidden_state = self.gru(inputs)
            if hidden_state.ndim == 3:
                final_hidden = hidden_state[-1]
            else:
                final_hidden = outputs[:, -1, :]
            return self.state_classifier(final_hidden), self.origin_classifier(final_hidden)
else:
    class GRUSequenceClassifier:  # type: ignore[no-redef]
        pass


def _build_model(hidden_size: int, num_layers: int) -> GRUSequenceClassifier:
    _require_torch()
    return GRUSequenceClassifier(
        input_size=FRAME_FEATURE_COUNT,
        hidden_size=hidden_size,
        num_layers=num_layers,
        num_state_classes=len(STATE_ORDER),
        num_origin_classes=len(ORIGIN_ORDER),
    )


def _tensorize_sequences(
    sequences: Sequence[Sequence[Sequence[float]]],
    state_labels: Sequence[int],
    origin_labels: Sequence[int],
) -> TensorDataset:
    _require_torch()
    feature_tensor = torch.tensor(sequences, dtype=torch.float32)
    state_tensor = torch.tensor(state_labels, dtype=torch.long)
    origin_tensor = torch.tensor(origin_labels, dtype=torch.long)
    return TensorDataset(feature_tensor, state_tensor, origin_tensor)


def _evaluate_model(
    model: GRUSequenceClassifier,
    sequences: Sequence[Sequence[Sequence[float]]],
    state_labels: Sequence[int],
    origin_labels: Sequence[int],
) -> tuple[float, Dict[str, Dict[str, int]], float, Dict[str, Dict[str, int]]]:
    _require_torch()
    if not sequences:
        return (
            0.0,
            _build_confusion_summary([], [], STATE_ORDER),
            0.0,
            _build_confusion_summary([], [], ORIGIN_ORDER),
        )

    model.eval()
    with torch.no_grad():
        state_logits, origin_logits = model(torch.tensor(sequences, dtype=torch.float32))
        predicted_state_indices = state_logits.argmax(dim=1).tolist()
        predicted_origin_indices = origin_logits.argmax(dim=1).tolist()
    return (
        _accuracy(state_labels, predicted_state_indices),
        _build_confusion_summary(state_labels, predicted_state_indices, STATE_ORDER),
        _accuracy(origin_labels, predicted_origin_indices),
        _build_confusion_summary(origin_labels, predicted_origin_indices, ORIGIN_ORDER),
    )


def train_gru(
    dataset: SequenceDataset | None = None,
    *,
    seed: int = 42,
    validation_ratio: float = DEFAULT_VALIDATION_RATIO,
    epochs: int = DEFAULT_EPOCHS,
    batch_size: int = DEFAULT_BATCH_SIZE,
    hidden_size: int = DEFAULT_HIDDEN_SIZE,
    num_layers: int = DEFAULT_NUM_LAYERS,
    learning_rate: float = DEFAULT_LEARNING_RATE,
    artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR,
) -> SequenceClassifierBundle:
    _require_torch()
    if dataset is None:
        dataset = generate_sequence_samples(seed=seed)

    _set_random_seed(seed)
    artifact_path = Path(artifact_dir)
    artifact_path.mkdir(parents=True, exist_ok=True)

    train_samples, validation_samples = _stratified_split(dataset.samples, validation_ratio=validation_ratio, seed=seed)
    train_sequences = _extract_sequences(train_samples, dataset.sequence_length)
    validation_sequences = _extract_sequences(validation_samples, dataset.sequence_length)
    train_state_labels = _extract_state_labels(train_samples)
    validation_state_labels = _extract_state_labels(validation_samples)
    train_origin_labels = _extract_origin_labels(train_samples)
    validation_origin_labels = _extract_origin_labels(validation_samples)

    feature_mean, feature_std = _compute_normalization(train_sequences)
    normalized_train_sequences = _normalize_sequences(train_sequences, feature_mean, feature_std)
    normalized_validation_sequences = _normalize_sequences(validation_sequences, feature_mean, feature_std)

    train_dataset = _tensorize_sequences(normalized_train_sequences, train_state_labels, train_origin_labels)
    train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True)

    model = _build_model(hidden_size=hidden_size, num_layers=num_layers)
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)
    state_loss_function = nn.CrossEntropyLoss()
    origin_loss_function = nn.CrossEntropyLoss()

    best_state_dict = copy.deepcopy(model.state_dict())
    best_state_accuracy = -1.0
    best_origin_accuracy = -1.0
    best_state_confusion = _build_confusion_summary([], [], STATE_ORDER)
    best_origin_confusion = _build_confusion_summary([], [], ORIGIN_ORDER)

    for _epoch in range(epochs):
        model.train()
        for batch_inputs, batch_state_labels, batch_origin_labels in train_loader:
            optimizer.zero_grad()
            state_logits, origin_logits = model(batch_inputs)
            state_loss = state_loss_function(state_logits, batch_state_labels)
            origin_loss = origin_loss_function(origin_logits, batch_origin_labels)
            loss = state_loss + origin_loss
            loss.backward()
            optimizer.step()

        (
            validation_state_accuracy,
            validation_state_confusion,
            validation_origin_accuracy,
            validation_origin_confusion,
        ) = _evaluate_model(
            model,
            normalized_validation_sequences,
            validation_state_labels,
            validation_origin_labels,
        )
        if (
            validation_state_accuracy > best_state_accuracy or
            (
                validation_state_accuracy == best_state_accuracy and
                validation_origin_accuracy >= best_origin_accuracy
            )
        ):
            best_state_accuracy = validation_state_accuracy
            best_origin_accuracy = validation_origin_accuracy
            best_state_confusion = validation_state_confusion
            best_origin_confusion = validation_origin_confusion
            best_state_dict = copy.deepcopy(model.state_dict())

    model.load_state_dict(best_state_dict)

    model_path = artifact_path / DEFAULT_MODEL_FILENAME
    metadata_path = artifact_path / DEFAULT_METADATA_FILENAME
    torch.save(best_state_dict, model_path)

    metadata = {
        "state_labels": list(STATE_ORDER),
        "origin_labels": list(ORIGIN_ORDER),
        "feature_mean": feature_mean,
        "feature_std": feature_std,
        "hidden_size": hidden_size,
        "num_layers": num_layers,
        "input_size": FRAME_FEATURE_COUNT,
        "sequence_length": dataset.sequence_length,
        "state_validation_accuracy": best_state_accuracy,
        "origin_validation_accuracy": best_origin_accuracy,
        "state_confusion_summary": best_state_confusion,
        "origin_confusion_summary": best_origin_confusion,
    }
    metadata_path.write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")

    return SequenceClassifierBundle(
        model=model,
        state_labels=list(STATE_ORDER),
        origin_labels=list(ORIGIN_ORDER),
        feature_mean=feature_mean,
        feature_std=feature_std,
        hidden_size=hidden_size,
        num_layers=num_layers,
        input_size=FRAME_FEATURE_COUNT,
        sequence_length=dataset.sequence_length,
        state_validation_accuracy=best_state_accuracy,
        origin_validation_accuracy=best_origin_accuracy,
        state_confusion_summary=best_state_confusion,
        origin_confusion_summary=best_origin_confusion,
        artifact_dir=str(artifact_path),
        model_path=str(model_path),
        metadata_path=str(metadata_path),
    )


def load_sequence_bundle(artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR) -> SequenceClassifierBundle:
    _require_torch()
    artifact_path = Path(artifact_dir)
    metadata_path = artifact_path / DEFAULT_METADATA_FILENAME
    model_path = artifact_path / DEFAULT_MODEL_FILENAME

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    model = _build_model(hidden_size=int(metadata["hidden_size"]), num_layers=int(metadata["num_layers"]))
    state_dict = torch.load(model_path, map_location="cpu")
    model.load_state_dict(state_dict)
    model.eval()

    return SequenceClassifierBundle(
        model=model,
        state_labels=list(metadata["state_labels"]),
        origin_labels=list(metadata["origin_labels"]),
        feature_mean=list(metadata["feature_mean"]),
        feature_std=list(metadata["feature_std"]),
        hidden_size=int(metadata["hidden_size"]),
        num_layers=int(metadata["num_layers"]),
        input_size=int(metadata["input_size"]),
        sequence_length=int(metadata["sequence_length"]),
        state_validation_accuracy=float(metadata["state_validation_accuracy"]),
        origin_validation_accuracy=float(metadata["origin_validation_accuracy"]),
        state_confusion_summary=dict(metadata["state_confusion_summary"]),
        origin_confusion_summary=dict(metadata["origin_confusion_summary"]),
        artifact_dir=str(artifact_path),
        model_path=str(model_path),
        metadata_path=str(metadata_path),
    )


def predict_sequence(
    bundle: SequenceClassifierBundle,
    sequence_window: Sequence[Sequence[Sequence[float]]],
) -> SequencePredictionResult:
    _require_torch()
    validated_sequence = validate_raw_sequence(sequence_window, sequence_length=bundle.sequence_length)
    flattened_sequence = flatten_raw_sequence(validated_sequence, sequence_length=bundle.sequence_length)
    normalized_sequence = _normalize_sequences([flattened_sequence], bundle.feature_mean, bundle.feature_std)[0]
    input_tensor = torch.tensor([normalized_sequence], dtype=torch.float32)

    assert bundle.model is not None
    bundle.model.eval()
    with torch.no_grad():
        state_logits, origin_logits = bundle.model(input_tensor)
        state_probabilities = torch.softmax(state_logits, dim=1)[0].tolist()
        origin_probabilities = torch.softmax(origin_logits, dim=1)[0].tolist()

    best_state_index = max(range(len(state_probabilities)), key=lambda index: state_probabilities[index])
    best_origin_index = max(range(len(origin_probabilities)), key=lambda index: origin_probabilities[index])
    state_label = bundle.state_labels[best_state_index]
    origin_label = bundle.origin_labels[best_origin_index]

    state_probability_map = {
        label: float(probability)
        for label, probability in zip(bundle.state_labels, state_probabilities)
    }
    origin_probability_map = {
        label: float(probability)
        for label, probability in zip(bundle.origin_labels, origin_probabilities)
    }
    return SequencePredictionResult(
        state_label=state_label,
        display_name=STATE_DISPLAY_NAMES[state_label],
        confidence=float(state_probability_map[state_label]),
        probabilities=state_probability_map,
        origin_label=origin_label,
        origin_display_name=ORIGIN_DISPLAY_NAMES[origin_label],
        origin_confidence=float(origin_probability_map[origin_label]),
        origin_probabilities=origin_probability_map,
    )


def format_confusion_summary(confusion_summary: Dict[str, Dict[str, int]], labels: Sequence[str]) -> List[str]:
    lines = []
    for expected_label in labels:
        predicted_counts = ", ".join(
            f"{predicted_label}={confusion_summary.get(expected_label, {}).get(predicted_label, 0)}"
            for predicted_label in labels
        )
        lines.append(f"{expected_label}: {predicted_counts}")
    return lines


def summarize_training(bundle: SequenceClassifierBundle) -> Dict[str, object]:
    return {
        "state_validation_accuracy": bundle.state_validation_accuracy,
        "origin_validation_accuracy": bundle.origin_validation_accuracy,
        "artifact_dir": bundle.artifact_dir,
        "model_path": bundle.model_path,
        "metadata_path": bundle.metadata_path,
        "state_confusion_summary": bundle.state_confusion_summary,
        "origin_confusion_summary": bundle.origin_confusion_summary,
    }
