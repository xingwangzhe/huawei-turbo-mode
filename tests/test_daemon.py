#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Unit tests for huawei-turbod's state/apply logic (no hardware needed)."""

import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(tempfile.mkdtemp(prefix="huawei-turbo-test-"))
FAKE_STATE = TMP / "fake-state"
FAKE_PERF = TMP / "fake-perf"
FAKE_FAIL = TMP / "fake-fail"
FAKE_UNKNOWN = TMP / "fake-unknown"

os.environ["HUAWEI_TURBO_STATE_DIR"] = str(TMP / "state")
os.environ["HUAWEI_TURBO_STATUS_DIR"] = str(TMP / "run")
os.environ["HUAWEI_PERF"] = str(FAKE_PERF)
sys.path.insert(0, str(ROOT / "bridge"))

import huawei_turbod as daemon  # noqa: E402


def write_script(path, body):
    path.write_text(body, encoding="utf-8")
    path.chmod(0o755)


def fake_huawei(ok=True):
    if ok:
        body = f"""#!/bin/sh
STATE={FAKE_STATE}
case "$1 $2" in
  "unlock get") [ -f "$STATE" ] && cat "$STATE" || echo 0 ;;
  "unlock on") echo 1 >"$STATE" ;;
  "unlock off") echo 0 >"$STATE" ;;
  *) exit 2 ;;
esac
"""
    else:
        body = """#!/bin/sh
case "$1 $2" in
  "unlock get") echo 0 ;;
  "unlock on") echo "firmware refused (battery < 21%)" >&2; exit 1 ;;
  "unlock off") exit 0 ;;
  *) exit 2 ;;
esac
"""
    return body


class DaemonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        write_script(FAKE_PERF, fake_huawei(True))
        write_script(FAKE_FAIL, fake_huawei(False))
        write_script(
            FAKE_UNKNOWN,
            "#!/bin/sh\ncase \"$1 $2\" in\n"
            "  \"unlock get\") echo garbage ;;\n"
            "  *) exit 2 ;;\nesac\n",
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(TMP, ignore_errors=True)

    def setUp(self):
        FAKE_STATE.write_text("0\n", encoding="utf-8")
        daemon.write_desired("off")

    def status(self):
        return json.loads(Path(daemon.STATUS_FILE).read_text(encoding="utf-8"))

    def test_read_write_desired(self):
        daemon.write_desired("on")
        self.assertEqual(daemon.read_desired(), "on")
        daemon.write_desired("off")
        self.assertEqual(daemon.read_desired(), "off")

        Path(daemon.STATE_FILE).write_text("garbage\n", encoding="utf-8")
        self.assertEqual(daemon.read_desired(), "off")

    def test_apply_on_and_off(self):
        ok = daemon.apply_state("on", "test", perf=str(FAKE_PERF), sync=False)
        self.assertTrue(ok)
        self.assertEqual(FAKE_STATE.read_text().strip(), "1")
        data = self.status()
        self.assertEqual(data["desired"], "on")
        self.assertEqual(data["actual"], "on")
        self.assertTrue(data["ok"])

        ok = daemon.apply_state("off", "test", perf=str(FAKE_PERF), sync=False)
        self.assertTrue(ok)
        self.assertEqual(FAKE_STATE.read_text().strip(), "0")
        self.assertEqual(self.status()["actual"], "off")

    def test_idempotent_apply(self):
        FAKE_STATE.write_text("1\n", encoding="utf-8")
        ok = daemon.apply_state("on", "already", perf=str(FAKE_PERF), sync=False)
        self.assertTrue(ok)
        self.assertIn("already", self.status()["message"])

    def test_apply_failure(self):
        ok = daemon.apply_state("on", "test", perf=str(FAKE_FAIL), sync=False)
        self.assertFalse(ok)
        data = self.status()
        self.assertFalse(data["ok"])
        self.assertEqual(data["actual"], "off")
        self.assertIn("refused", data["message"])

    def test_invalid_state(self):
        with self.assertRaises(ValueError):
            daemon.write_desired("bogus")
        self.assertFalse(daemon.apply_state("bogus", "test", perf=str(FAKE_PERF)))

    def test_unknown_actual(self):
        self.assertEqual(daemon.get_actual(str(FAKE_UNKNOWN)), "unknown")


if __name__ == "__main__":
    unittest.main()
