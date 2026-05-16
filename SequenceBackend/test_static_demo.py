from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

try:
    from .dataset import (
        FEATURE_LIMITS,
        SAMPLES_PER_STATE,
        build_feature_vector,
        generate_samples,
        summarize_expected_counts,
    )
    from .exporter import export_c_header
    from .static_state_model import predict_static_state, train_static_state_model
    from .model_types import FEATURE_COUNT, FEATURE_INDEX, FEATURE_NAMES, RISK_WARNING, STATE_FIRE, STATE_ORDER
    from .risk import assess_risk
except ImportError:
    from dataset import (
        FEATURE_LIMITS,
        SAMPLES_PER_STATE,
        build_feature_vector,
        generate_samples,
        summarize_expected_counts,
    )
    from exporter import export_c_header
    from static_state_model import predict_static_state, train_static_state_model
    from model_types import FEATURE_COUNT, FEATURE_INDEX, FEATURE_NAMES, RISK_WARNING, STATE_FIRE, STATE_ORDER
    from risk import assess_risk


class StaticStateDemoTests(unittest.TestCase):
    def test_generate_samples_counts_and_ranges(self) -> None:
        dataset = generate_samples(seed=42)
        self.assertEqual(len(dataset.samples), SAMPLES_PER_STATE * len(STATE_ORDER))
        counts = summarize_expected_counts(dataset)
        for label in STATE_ORDER:
            self.assertEqual(counts[label], SAMPLES_PER_STATE)

        for sample in dataset.samples:
            self.assertEqual(len(sample.features), FEATURE_COUNT)
            for feature_name, value in zip(FEATURE_NAMES, sample.features):
                low, high = FEATURE_LIMITS[feature_name]
                self.assertGreaterEqual(value, low)
                self.assertLessEqual(value, high)

    def test_build_feature_vector_shape(self) -> None:
        feature_vector = build_feature_vector(
            [
                [78.0, 22.0, 90.0, 66.0],
                [57.0, 29.0, 61.0, 58.0],
                [37.0, 35.0, 25.0, 50.0],
                [34.0, 38.0, 22.0, 46.0],
            ]
        )
        self.assertEqual(len(feature_vector), FEATURE_COUNT)
        self.assertAlmostEqual(feature_vector[FEATURE_INDEX["temperature_avg"]], 51.5)
        self.assertAlmostEqual(feature_vector[FEATURE_INDEX["temperature_max"]], 78.0)
        self.assertAlmostEqual(feature_vector[FEATURE_INDEX["temperature_spread"]], 44.0)

    def test_training_is_reproducible(self) -> None:
        dataset = generate_samples(seed=42)
        model_a = train_static_state_model(dataset.samples, seed=42)
        model_b = train_static_state_model(dataset.samples, seed=42)

        self.assertEqual(model_a.cluster_label_map, model_b.cluster_label_map)
        self.assertEqual(model_a.assignments, model_b.assignments)
        self.assertEqual(model_a.iterations, model_b.iterations)

    def test_predict_typical_fire_sample(self) -> None:
        dataset = generate_samples(seed=42)
        model = train_static_state_model(dataset.samples, seed=42)
        feature_vector = build_feature_vector(
            [
                [81.0, 21.0, 92.0, 68.0],
                [58.0, 28.0, 62.0, 57.0],
                [36.0, 35.0, 24.0, 48.0],
                [33.0, 37.0, 21.0, 45.0],
            ],
        )
        result = predict_static_state(model, feature_vector)
        self.assertEqual(result.state_label, STATE_FIRE)

    def test_risk_assessment_is_separate(self) -> None:
        dataset = generate_samples(seed=42)
        model = train_static_state_model(dataset.samples, seed=42)
        current_groups = [
            [81.0, 21.0, 92.0, 68.0],
            [58.0, 28.0, 62.0, 57.0],
            [36.0, 35.0, 24.0, 48.0],
            [33.0, 37.0, 21.0, 45.0],
        ]
        previous_groups = [
            [73.0, 22.0, 80.0, 66.0],
            [52.0, 29.0, 52.0, 56.0],
            [34.0, 35.0, 20.0, 47.0],
            [31.0, 37.0, 18.0, 44.0],
        ]
        feature_vector = build_feature_vector(current_groups)
        result = predict_static_state(model, feature_vector)
        risk = assess_risk(model, feature_vector, result.state_label, current_groups, previous_groups)
        self.assertEqual(result.state_label, STATE_FIRE)
        self.assertIn(risk.level, {RISK_WARNING, "LEVEL_DANGER"})

    def test_export_header_contains_required_symbols(self) -> None:
        dataset = generate_samples(seed=42)
        model = train_static_state_model(dataset.samples, seed=42)
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = export_c_header(model, Path(temp_dir) / "generated_static_state_model.h")
            content = output_path.read_text(encoding="ascii")

        self.assertIn("#define STATIC_STATE_CLUSTER_COUNT 6", content)
        self.assertIn(f"#define STATIC_STATE_FEATURE_COUNT {FEATURE_COUNT}", content)
        self.assertIn("g_static_state_centers", content)
        self.assertIn("g_static_state_feature_min", content)
        self.assertIn("g_static_state_feature_max", content)
        self.assertIn("g_static_state_cluster_labels", content)


if __name__ == "__main__":
    unittest.main()
