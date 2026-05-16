from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from .dataset import generate_sequence_samples
    from .sequence_classifier import summarize_training, train_gru
except ImportError:
    from dataset import generate_sequence_samples
    from sequence_classifier import summarize_training, train_gru


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Train the GRU sequence classifier backend.")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--epochs", type=int, default=18)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--hidden-size", type=int, default=64)
    parser.add_argument("--num-layers", type=int, default=1)
    parser.add_argument("--learning-rate", type=float, default=1e-3)
    parser.add_argument("--artifact-dir", type=Path, default=Path(__file__).resolve().parent / "artifacts" / "gru_backend")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    dataset = generate_sequence_samples(seed=args.seed)
    bundle = train_gru(
        dataset=dataset,
        seed=args.seed,
        epochs=args.epochs,
        batch_size=args.batch_size,
        hidden_size=args.hidden_size,
        num_layers=args.num_layers,
        learning_rate=args.learning_rate,
        artifact_dir=args.artifact_dir,
    )
    print(json.dumps(summarize_training(bundle), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
