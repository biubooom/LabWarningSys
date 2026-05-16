from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

try:
    import torch
except ImportError:
    torch = None

try:
    from .dataset import (
        BASE_FEATURE_LIMITS,
        flatten_raw_sequence,
        generate_sequence_samples,
        summarize_expected_sequence_counts,
        validate_raw_sequence,
    )
    from .model_types import FRAME_FEATURE_COUNT, SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from .sequence_classifier import load_sequence_bundle, predict_sequence, train_gru
except ImportError:
    from dataset import (
        BASE_FEATURE_LIMITS,
        flatten_raw_sequence,
        generate_sequence_samples,
        summarize_expected_sequence_counts,
        validate_raw_sequence,
    )
    from model_types import FRAME_FEATURE_COUNT, SEQUENCE_LENGTH, STATE_DISPLAY_NAMES, STATE_ORDER
    from sequence_classifier import load_sequence_bundle, predict_sequence, train_gru


class SequenceDatasetTests(unittest.TestCase):
    def test_generate_sequence_samples_shape_counts_and_ranges(self) -> None:
        dataset = generate_sequence_samples(seed=42)
        counts = summarize_expected_sequence_counts(dataset)

        self.assertEqual(dataset.sequence_length, SEQUENCE_LENGTH)
        self.assertEqual(len(dataset.samples), 100 * len(STATE_ORDER))
        for label in STATE_ORDER:
            self.assertEqual(counts[label], 100)

        for sample in dataset.samples[:20]:
            self.assertEqual(len(sample.sequence), SEQUENCE_LENGTH)
            for frame in sample.sequence:
                self.assertEqual(len(frame), 4)
                for group in frame:
                    self.assertEqual(len(group), 4)
                    for field_name, value in zip(("temperature", "humidity", "smoke", "light"), group):
                        low, high = BASE_FEATURE_LIMITS[field_name]
                        self.assertGreaterEqual(value, low)
                        self.assertLessEqual(value, high)

    def test_flatten_and_validate_sequence_shape(self) -> None:
        dataset = generate_sequence_samples(seed=7, samples_per_state=1)
        sequence = validate_raw_sequence(dataset.samples[0].sequence)
        flattened = flatten_raw_sequence(sequence)

        self.assertEqual(len(flattened), SEQUENCE_LENGTH)
        self.assertEqual(len(flattened[0]), FRAME_FEATURE_COUNT)


@unittest.skipIf(torch is None, "PyTorch is not installed")
class SequenceClassifierTests(unittest.TestCase):
    def test_train_save_load_and_predict(self) -> None:
        dataset = generate_sequence_samples(seed=42, samples_per_state=12)
        sample_sequence = dataset.samples[0].sequence

        with tempfile.TemporaryDirectory() as temp_dir:
            bundle = train_gru(
                dataset=dataset,
                seed=42,
                epochs=4,
                batch_size=16,
                hidden_size=48,
                artifact_dir=Path(temp_dir),
            )
            self.assertTrue(Path(bundle.model_path).exists())
            self.assertTrue(Path(bundle.metadata_path).exists())
            self.assertGreaterEqual(bundle.state_validation_accuracy, 0.5)
            self.assertGreaterEqual(bundle.origin_validation_accuracy, 0.5)

            loaded_bundle = load_sequence_bundle(Path(temp_dir))
            result_a = predict_sequence(bundle, sample_sequence)
            result_b = predict_sequence(loaded_bundle, sample_sequence)

            self.assertIn(result_a.state_label, STATE_ORDER)
            self.assertEqual(result_a.state_label, result_b.state_label)
            self.assertAlmostEqual(result_a.confidence, result_b.confidence, places=5)
            self.assertEqual(set(result_a.probabilities.keys()), set(STATE_ORDER))
            self.assertEqual(result_a.display_name, STATE_DISPLAY_NAMES[result_a.state_label])
            self.assertEqual(result_a.origin_label, result_b.origin_label)
            self.assertAlmostEqual(result_a.origin_confidence, result_b.origin_confidence, places=5)

    def test_service_payload_validation_and_prediction(self) -> None:
        dataset = generate_sequence_samples(seed=24, samples_per_state=10)
        sample_sequence = dataset.samples[0].sequence
        invalid_sequence = sample_sequence[:-1]

        with tempfile.TemporaryDirectory() as temp_dir:
            train_gru(
                dataset=dataset,
                seed=24,
                epochs=3,
                batch_size=16,
                hidden_size=32,
                artifact_dir=Path(temp_dir),
            )
            bundle = load_sequence_bundle(Path(temp_dir))
            result = predict_sequence(bundle, sample_sequence)

            self.assertIn(result.state_label, STATE_ORDER)
            self.assertTrue(result.origin_label.startswith("ORIGIN_"))
            self.assertIn("ORIGIN_NONE", set(result.origin_probabilities.keys()))

            with self.assertRaises(ValueError):
                predict_sequence(bundle, invalid_sequence)


if __name__ == "__main__":
    unittest.main()
