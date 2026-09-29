import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


BOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("observer", BOT / "tools" / "observer.py")
observer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(observer)


def write_status(root, ident, **fields):
    folder = root / ident
    folder.mkdir(parents=True)
    base = {"character": ident, "family": "fencer", "online": True, "updated_ms": 1_000_000,
            "gold": 100, "gold_at_login": 40, "kills": 1, "deaths": 0, "goal": "TRAIN_COMBAT",
            "goal_family": "Training", "home_city": "Britain", "party_size": 0}
    base.update(fields)
    (folder / "status.json").write_text(json.dumps(base))


class ObserverTests(unittest.TestCase):
    def test_collect_counts_only_live_characters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_status(root, "a.one")
            write_status(root, "a.two", family="tailor", goal="CRAFT", party_size=2, kills=0)
            write_status(root, "a.gone", online=False, phase="offline")
            write_status(root, "a.crashed", updated_ms=1_000_000 - 300_000)
            (root / "a.broken").mkdir()
            (root / "a.broken" / "status.json").write_text("{not json")
            data = observer.collect(root, now=1_000.0 + 5)
            summary = data["summary"]
            self.assertEqual(summary["known"], 4)
            self.assertEqual(summary["online"], 2)
            self.assertEqual(summary["stale"], 1)
            self.assertEqual(summary["in_party"], 1)
            self.assertEqual(summary["kills"], 1)
            self.assertEqual(summary["gold_earned"], 120)
            self.assertEqual(summary["by_goal"], {"TRAIN_COMBAT": 1, "CRAFT": 1})
            crashed = next(b for b in data["bots"] if b["character"] == "a.crashed")
            self.assertTrue(crashed["stale"])
            self.assertFalse(crashed["live"])

    def test_population_file_is_included(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "population.json").write_text(json.dumps({"online": ["x"], "max_online": 5}))
            data = observer.collect(root, run_dir=root, now=0)
            self.assertEqual(data["population"]["max_online"], 5)

    def test_snapshot_embeds_data_safely(self):
        data = {"summary": {}, "bots": [{"character": "</script><b>"}]}
        page = observer.snapshot(data)
        self.assertIn("window.__DATA__=", page)
        self.assertNotIn("</script><b>", page)
        self.assertTrue(page.startswith("<!doctype html>"))


if __name__ == "__main__":
    unittest.main()
