from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

try:
    import torch
except ImportError:
    torch = None

try:
    from .demo_simulation import build_simulated_sequence, resolve_scenario, run_local_demo
    from .model_types import SEQUENCE_LENGTH, STATE_FIRE, STATE_ORDER
    from .sequence_classifier import train_gru
except ImportError:
    from demo_simulation import build_simulated_sequence, resolve_scenario, run_local_demo
    from model_types import SEQUENCE_LENGTH, STATE_FIRE, STATE_ORDER
    from sequence_classifier import train_gru


class DemoSimulationTests(unittest.TestCase):
    def test_resolve_scenario_aliases(self) -> None:
        self.assertEqual(resolve_scenario("fire"), STATE_FIRE)
        self.assertEqual(resolve_scenario("STATE_FIRE"), STATE_FIRE)
        self.assertEqual(resolve_scenario("gas_leak"), "STATE_GAS_LEAK")

    def test_build_simulated_sequence_shape(self) -> None:
        sequence = build_simulated_sequence(STATE_FIRE, seed=42)
        self.assertEqual(len(sequence), SEQUENCE_LENGTH)
        self.assertEqual(len(sequence[0]), 4)
        self.assertEqual(len(sequence[0][0]), 4)


@unittest.skipIf(torch is None, "PyTorch is not installed")
class DemoSimulationLocalPredictionTests(unittest.TestCase):
    def test_run_local_demo_returns_valid_prediction(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            train_gru(epochs=3, artifact_dir=Path(temp_dir))
            output = run_local_demo(STATE_FIRE, seed=42, artifact_dir=Path(temp_dir))

        self.assertEqual(output["mode"], "local")
        self.assertEqual(output["input_state_label"], STATE_FIRE)
        self.assertIn(output["prediction"]["state_label"], STATE_ORDER)
        self.assertGreater(len(output["sequence_preview"]), 0)


if __name__ == "__main__":
    unittest.main()
