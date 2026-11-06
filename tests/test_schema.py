"""Validate public YAML through ESPHome, including legacy and invalid input."""
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
BASE = (ROOT / "tests/compile-pro.yaml").read_text().replace(
    "path: ../components", f"path: {ROOT / 'components'}"
)


@pytest.mark.parametrize("change,valid,expected", [
    (lambda s: s, True, "preserve_monitoring_settings: true"),
    (lambda s: s.replace("pro_mode: true", "pro_mode: false"), True, "preserve_monitoring_settings: false"),
    (lambda s: s.replace("pro_mode: true", 'pro_mode: true\n    baby_code: "abcdef123456"'), True, "Configuration is valid"),
    (lambda s: s.replace("pro_mode: true", 'pro_mode: true\n    baby_code: "not-a-token!"'), False, "12 hexadecimal"),
    (lambda s: s.replace("update_interval: 2s", "update_interval: 0s"), False, None),
    (lambda s: s.replace("update_interval: 2s", "update_interval: 100ms"), False, None),
    (lambda s: s.replace("data_timeout: 30s", "data_timeout: 2s"), False, "greater than update_interval"),
])
def test_schema(tmp_path, change, valid, expected):
    config = tmp_path / "test.yaml"
    config.write_text(change(BASE))
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(config)], capture_output=True, text=True)
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == valid, output
    if expected:
        assert expected in output, output


def test_shipping_example(tmp_path):
    text = (ROOT / "example-sense-u-pro-esp32c3.yaml").read_text()
    text = text.replace("source: github://chiachunli08/esphome-sense-u@master",
                        f"source:\n      type: local\n      path: {ROOT / 'components'}")
    text = text.replace("!secret wifi_ssid", "test-network").replace("!secret wifi_password", "test-password")
    text = text.replace("!secret senseu_mac_address", "AA:BB:CC:DD:EE:FF")
    config = tmp_path / "example.yaml"
    config.write_text(text)
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(config)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
