"""Integration checks use the actual executable and Python control plane."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from simulator_client import EngineClient
ENGINE = sys.argv.pop(1)


class ProtocolTests(unittest.TestCase):
    def test_requests_and_recovery(self):
        with EngineClient(ENGINE) as engine:
            first = engine.poll()
            self.assertEqual(first["sequence"], 0)
            self.assertEqual(len(first["samples"]), 2)
            self.assertEqual(engine.calibrate(1, 10)["type"], "calibrated")
            second = engine.poll()
            self.assertGreater(second["samples"][0]["value"], 34)
            self.assertEqual(second["samples"][0]["window_count"], 1)
            for command in ("bogus", "poll extra", "calibrate -1 2", "calibrate 1 nan",
                            "calibrate 999 1", "calibrate 1 1 extra", "calibrate 1 1e999"):
                with self.assertRaises(ValueError):
                    engine.request(command)
            self.assertEqual(engine.poll()["type"], "telemetry")
        self.assertIsNotNone(engine._process.poll())

    def test_oversize_and_eof(self):
        result = subprocess.run([ENGINE], input="x" * 5000 + "\npoll\n", text=True,
                                capture_output=True, timeout=5, check=True)
        frames = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([f["type"] for f in frames], ["error", "telemetry"])
        result = subprocess.run([ENGINE], input="poll", text=True,
                                capture_output=True, timeout=5, check=True)
        self.assertEqual(json.loads(result.stdout)["type"], "telemetry")

    def test_dashboard_logging(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "telemetry.jsonl"
            result = subprocess.run([sys.executable, str(ROOT / "python/dashboard.py"),
                "--engine", ENGINE, "--interval", "0.001", "--count", "3",
                "--no-interactive", "--calibrate", "1", "5", "--log", str(log)],
                capture_output=True, text=True, timeout=10, check=True)
            frames = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(len(frames), 3)
            self.assertGreater(frames[0]["samples"][0]["value"], 29)
            self.assertIn("sensor=2", result.stdout)


if __name__ == "__main__":
    unittest.main()
