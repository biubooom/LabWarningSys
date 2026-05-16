from __future__ import annotations

import argparse
import json
import random
from pathlib import Path
from typing import Dict, List
from urllib import error, request

try:
    from .dataset import generate_sequence_for_state, summarize_raw_groups
    from .model_types import SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from .sequence_classifier import DEFAULT_ARTIFACT_DIR, load_sequence_bundle, predict_sequence, train_gru
except ImportError:
    from dataset import generate_sequence_for_state, summarize_raw_groups
    from model_types import SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from sequence_classifier import DEFAULT_ARTIFACT_DIR, load_sequence_bundle, predict_sequence, train_gru


SCENARIO_ALIASES: Dict[str, str] = {
    "normal": "STATE_NORMAL",
    "fire": "STATE_FIRE",
    "gas": "STATE_GAS_LEAK",
    "gas_leak": "STATE_GAS_LEAK",
    "high_humid": "STATE_HIGH_HUMID",
    "humid": "STATE_HIGH_HUMID",
}


def resolve_scenario(name: str) -> str:
    normalized = name.strip().lower()
    if normalized in SCENARIO_ALIASES:
        return SCENARIO_ALIASES[normalized]

    upper_name = name.strip().upper()
    for state_label in STATE_ORDER:
        if upper_name == state_label:
            return state_label
    raise ValueError(f"unknown scenario: {name}")


def build_simulated_sequence(state_label: str, seed: int, sequence_length: int = SEQUENCE_LENGTH) -> List[List[List[float]]]:
    rng = random.Random(seed)
    return generate_sequence_for_state(rng, state_label, sequence_length=sequence_length)


def _artifact_paths(artifact_dir: str | Path) -> tuple[Path, Path]:
    artifact_path = Path(artifact_dir)
    return artifact_path / "gru_state_dict.pt", artifact_path / "metadata.json"


def ensure_model_bundle(artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR):
    model_path, metadata_path = _artifact_paths(artifact_dir)
    if not model_path.exists() or not metadata_path.exists():
        train_gru(artifact_dir=artifact_dir)
    return load_sequence_bundle(artifact_dir)


def summarize_sequence(sequence: List[List[List[float]]]) -> List[str]:
    lines: List[str] = []
    for frame_index, frame in enumerate(sequence, start=1):
        summary = summarize_raw_groups(frame)
        lines.append(
            f"Frame {frame_index:02d}: "
            f"T_avg={summary['temperature_avg']:.1f}, "
            f"H_avg={summary['humidity_avg']:.1f}, "
            f"S_avg={summary['smoke_avg']:.1f}, "
            f"L_avg={summary['light_avg']:.1f}, "
            f"T_max={summary['temperature_max']:.1f}, "
            f"S_max={summary['smoke_max']:.1f}"
        )
    return lines


def run_local_demo(state_label: str, seed: int, artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR) -> Dict[str, object]:
    bundle = ensure_model_bundle(artifact_dir=artifact_dir)
    sequence = build_simulated_sequence(state_label, seed, sequence_length=bundle.sequence_length)
    result = predict_sequence(bundle, sequence)
    return {
        "mode": "local",
        "input_state_label": state_label,
        "input_display_name": STATE_DISPLAY_NAMES[state_label],
        "prediction": {
            "state_label": result.state_label,
            "display_name": result.display_name,
            "confidence": result.confidence,
            "probabilities": result.probabilities,
            "origin_label": result.origin_label,
            "origin_display_name": result.origin_display_name,
            "origin_confidence": result.origin_confidence,
            "origin_probabilities": result.origin_probabilities,
        },
        "sequence_preview": summarize_sequence(sequence),
    }


def run_http_demo(state_label: str, seed: int, base_url: str) -> Dict[str, object]:
    sequence = build_simulated_sequence(state_label, seed)
    payload = json.dumps({"sequence": sequence}).encode("utf-8")
    response_request = request.Request(
        url=f"{base_url.rstrip('/')}/predict",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with request.urlopen(response_request, timeout=15) as response:
            body = json.loads(response.read().decode("utf-8"))
    except error.URLError as exc:
        raise RuntimeError(f"failed to call prediction service: {exc}") from exc

    return {
        "mode": "http",
        "input_state_label": state_label,
        "input_display_name": STATE_DISPLAY_NAMES[state_label],
        "prediction": body,
        "sequence_preview": summarize_sequence(sequence),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run a simulated sensor-sequence demo against the GRU classifier.")
    parser.add_argument(
        "--scenario",
        default="fire",
        help="Scenario to simulate: normal, fire, gas_leak, high_humid",
    )
    parser.add_argument("--seed", type=int, default=42, help="Random seed for the simulated sequence")
    parser.add_argument("--mode", choices=("local", "http"), default="local", help="Prediction mode")
    parser.add_argument("--base-url", default="http://127.0.0.1:8000", help="Service base URL when using http mode")
    parser.add_argument("--artifact-dir", type=Path, default=DEFAULT_ARTIFACT_DIR, help="Local artifact directory")
    parser.add_argument("--preview-frames", type=int, default=4, help="How many tail frames to print")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    state_label = resolve_scenario(args.scenario)

    if args.mode == "local":
        output = run_local_demo(state_label, seed=args.seed, artifact_dir=args.artifact_dir)
    else:
        output = run_http_demo(state_label, seed=args.seed, base_url=args.base_url)

    print(f"Scenario: {output['input_state_label']} / {output['input_display_name']}")
    print(f"Mode: {output['mode']}")
    print("Sequence Preview:")
    preview_lines = output["sequence_preview"][-max(1, args.preview_frames):]
    for line in preview_lines:
        print(f"  {line}")

    prediction = output["prediction"]
    print("Prediction:")
    print(f"  state_label: {prediction['state_label']}")
    print(f"  display_name: {prediction['display_name']}")
    print(f"  confidence: {prediction['confidence']:.4f}")
    print(f"  origin_label: {prediction['origin_label']}")
    print(f"  origin_display_name: {prediction['origin_display_name']}")
    print(f"  origin_confidence: {prediction['origin_confidence']:.4f}")
    print("  top_probabilities:")
    top_probabilities = sorted(prediction["probabilities"].items(), key=lambda item: item[1], reverse=True)[:3]
    for label, probability in top_probabilities:
        print(f"    {label}: {probability:.4f}")


if __name__ == "__main__":
    main()
