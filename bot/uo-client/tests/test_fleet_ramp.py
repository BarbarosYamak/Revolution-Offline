import datetime
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



class PersonaMirrorTests(unittest.TestCase):
    """tools/fleet_ramp.py must compute exactly what include/uo/persona.h does;
    tests/persona.cpp checks the C++ side against the same file."""

    def test_vectors_match_the_cpp_generator(self):
        rows = (BOT / "tests" / "data" / "persona_vectors.tsv").read_text().splitlines()
        self.assertGreater(len(rows), 50)
        for row in rows:
            ident, rhythm, risk, sociability, schedule = row.split("\t")
            persona = fleet_ramp.make_persona(ident)
            self.assertEqual(
                (persona["rhythm"], persona["risk_shift"], persona["sociability"],
                 fleet_ramp.describe(persona)),
                (rhythm, int(risk), int(sociability), schedule), ident)

    def test_identity_id_matches_the_client(self):
        self.assertEqual(fleet_ramp.identity_id("RevScale100_001", "Ayse Nur"),
                         "revscale100_001.ayse_nur")

    def test_week_wraps_and_windows_end(self):
        persona = {"windows": [(fleet_ramp.SUN, 23 * 60, 120)]}
        self.assertEqual(fleet_ramp.minutes_left(persona, 6, 23 * 60 + 30), 90)
        self.assertEqual(fleet_ramp.minutes_left(persona, 0, 30), 30)
        self.assertEqual(fleet_ramp.minutes_left(persona, 0, 61), 0)
        self.assertEqual(fleet_ramp.minutes_until_next(persona, 6, 22 * 60), 60)

    def test_saved_persona_wins_over_the_derived_one(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            state = data / "acc.bob" / "state.json"
            state.parent.mkdir()
            state.write_text(json.dumps({"persona": {
                "rhythm": "morning", "risk_shift": 3, "sociability": 90,
                "windows": [{"days": 127, "start_min": 60, "length_min": 30}]}}))
            persona = fleet_ramp.persona_for("Acc", "Bob", data)
            self.assertEqual(persona["windows"], [(127, 60, 30)])
            self.assertEqual(fleet_ramp.persona_for("Acc", "Other", data),
                             fleet_ramp.make_persona("acc.other"))


class PopulationDecisionTests(unittest.TestCase):
    def setUp(self):
        self.roster = [[f"N{i}", f"A{i}", "mage"] for i in range(6)]
        self.evening = {"rhythm": "evening", "windows": [(fleet_ramp.EVERY_DAY, 19 * 60, 180)]}
        self.morning = {"rhythm": "morning", "windows": [(fleet_ramp.EVERY_DAY, 8 * 60, 60)]}
        self.personas = {r[0]: (self.evening if i < 4 else self.morning)
                         for i, r in enumerate(self.roster)}
        self.tuesday_night = datetime.datetime(2026, 9, 29, 20, 0)

    def test_only_characters_in_their_window_log_in(self):
        chosen = fleet_ramp.decide(self.roster, self.personas, set(),
                                   self.tuesday_night, 10, {})
        self.assertEqual(sorted(r[0] for r, _ in chosen), ["N0", "N1", "N2", "N3"])
        # They play until the window closes, not a fixed stage length.
        self.assertTrue(all(minutes == 120 for _, minutes in chosen))

    def test_cap_running_and_cooldown(self):
        now = self.tuesday_night
        chosen = fleet_ramp.decide(self.roster, self.personas, {"N0"}, now, 3,
                                   {"N1": now.timestamp() + 60})
        self.assertEqual(len(chosen), 2)
        self.assertTrue({r[0] for r, _ in chosen} <= {"N2", "N3"})

    def test_no_launch_near_the_end_of_a_window(self):
        late = datetime.datetime(2026, 9, 29, 21, 50)
        self.assertEqual(fleet_ramp.decide(self.roster, self.personas, set(), late, 10, {}), [])

    def test_a_different_subset_each_day_when_capped(self):
        roster = [[f"C{i}", f"A{i}", "mage"] for i in range(40)]
        personas = {r[0]: self.evening for r in roster}
        days = [frozenset(r[0] for r, _ in fleet_ramp.decide(
                    roster, personas, set(), datetime.datetime(2026, 9, d, 20, 0), 10, {}))
                for d in range(1, 8)]
        self.assertGreater(len(set(days)), 1)

    def test_live_launches_by_schedule_once(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            launched = []

            class Done:
                def poll(self):
                    return None

            def fake_launch(*args, **kwargs):
                launched.append(args[4])
                return Done()

            clock = lambda: self.tuesday_night
            with mock.patch.object(fleet_ramp, "launch", fake_launch), \
                 mock.patch.object(fleet_ramp, "account_passwords",
                                   lambda: {r[1].lower(): "pw" for r in self.roster}), \
                 mock.patch.object(fleet_ramp, "CREDS", run / "none.json"), \
                 mock.patch.object(fleet_ramp, "memory_free_gib", lambda: 16.0), \
                 mock.patch.object(fleet_ramp, "persona_for",
                                   lambda account, name, data: self.personas[name]), \
                 mock.patch.object(fleet_ramp.time, "sleep", lambda s: None):
                running = fleet_ramp.live(self.roster, run, 3, clock=clock, once=True,
                                          exe="x", bot_data=run, data_dir=run)
            self.assertEqual(len(launched), 3)
            self.assertEqual(set(running), set(launched))
            status = json.loads((run / "population.json").read_text())
            self.assertEqual(status["wanting_now"], 4)
            self.assertEqual(len(status["online"]), 3)



class EraClockTests(unittest.TestCase):
    def test_fixed_and_advancing_calendar(self):
        self.assertEqual(fleet_ramp.era_date("2009-05-01"), "2009-05-01")
        self.assertEqual(fleet_ramp.era_date("2009-05-01", 30, since=0, now=86400), "2009-05-31")
        self.assertEqual(fleet_ramp.era_date("2016-12-01", 30, since=0, now=86400 * 10), "2016-12-31")

    def test_population_scale_is_owner_data(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "era_population.tsv"
            self.assertEqual(fleet_ramp.era_population_scale("2012-01-01", path), 1.0)
            path.write_text("# year\tscale\n2012\t0.4\n")
            self.assertEqual(fleet_ramp.era_population_scale("2012-06-01", path), 0.4)
            self.assertEqual(fleet_ramp.era_population_scale("2009-06-01", path), 1.0)


if __name__ == "__main__":
    unittest.main()
