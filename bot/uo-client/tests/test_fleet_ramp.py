import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


BOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "fleet_ramp", BOT / "tools" / "fleet_ramp.py")
fleet_ramp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fleet_ramp)


class FleetRampRegressionTests(unittest.TestCase):
    def test_logout_state_snapshot_is_created_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bot = root / "bot"
            run = root / "run"
            source = bot / "bot_data" / "Account.Name" / "state.json"
            source.parent.mkdir(parents=True)
            run.mkdir()
            source.write_text(json.dumps({"bank": [{"item": "i_log", "qty": 1}]}))
            admitted = {"Name": {"account": "Account", "family": "mage"}}
            bots = [{"name": "Name", "logged_out": True}]

            with mock.patch.object(fleet_ramp, "BOT", bot):
                fleet_ramp.snapshot_logged_out_states(run, admitted, bots)
                destination = run / "Name.state_after.json"
                self.assertEqual(json.loads(destination.read_text())["bank"][0]["qty"], 1)

                source.write_text(json.dumps({"bank": [{"item": "i_log", "qty": 99}]}))
                fleet_ramp.snapshot_logged_out_states(run, admitted, bots)
                self.assertEqual(json.loads(destination.read_text())["bank"][0]["qty"], 1)


if __name__ == "__main__":
    unittest.main()
