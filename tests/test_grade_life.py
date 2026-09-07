import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BOT = Path(__file__).resolve().parents[1]
GRADER = BOT / "tools" / "grade_life.py"


def base_console(*extra):
    return [
        "INFO [life] session_summary duration=600s goals=4/4 gold=100->110 skills=100.0->101.0 logs=+1",
        "INFO [life] session_goals families=4 picks=4 top=25% varied=1 self_superseded=0 | upkeep=1(25%) wander=1(25%)",
        "INFO [life] train: Tactics 10.0->10.1 gained by practice",
        *extra,
        "INFO [life] wind-down: arrived somewhere safe at 100,100",
        "LOG event logout_complete: acked",
    ]


def grade(family, lines, before=None, after=None):
    before = {"bank": []} if before is None else before
    after = {"bank": [{"item": "i_loot", "qty": 1}]} if after is None else after
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        console = root / "console.txt"
        state_before = root / "before.json"
        state_after = root / "after.json"
        console.write_text("\n".join(lines), encoding="utf-8")
        state_before.write_text(json.dumps(before), encoding="utf-8")
        state_after.write_text(json.dumps(after), encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(GRADER), str(console), str(state_before),
             str(state_after), "--family", family],
            capture_output=True, text=True, encoding="utf-8", errors="replace")


class GradeLifeRegressionTests(unittest.TestCase):
    def test_mage_hunt_and_hunt_return_are_the_family_faucet(self):
        result = grade("mage", base_console(
            "INFO [life] hunt: confirmed kill target='Skeleton' corpse=0x1234",
            "INFO [life] goal=GET_TOOL reason='spellbook'",
            "INFO [life] goal_completed=TRAIN_COMBAT progress=1"))
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("18/18 PASS", result.stdout)

    def test_tame_attempt_is_not_a_farm_outcome(self):
        result = grade("tamer", base_console(
            "INFO [life] tame: trying 'Sheep' (needs Taming 11.1)"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES: FARM-2", result.stdout)

    def test_confirmed_tame_is_a_farm_outcome(self):
        result = grade("tamer", base_console(
            "INFO [life] tame: success Sheep serial=0x1234 req=11.1 -- accepts me as master"))
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_unchanged_relevant_stock_does_not_pass(self):
        state = {"bank": [{"item": "i_scroll_recall", "qty": 1}]}
        result = grade("scribe", base_console(
            "INFO [life] goal_completed=CRAFT progress=1"), state, state)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES: STOCK-1", result.stdout)

    def test_realized_family_product_sale_preserves_stock_credit(self):
        state = {"bank": [{"item": "i_scroll_recall", "qty": 1}]}
        result = grade("scribe", base_console(
            "INFO [life] goal_completed=CRAFT progress=1",
            "INFO [life] craft: made i_scroll_recall pack 0->1 (1 this sitting)",
            "INFO [life] earn_gold: sold 1 i_scroll_recall for 25 gold (25 each) to a 'mage'"), state, state)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_smith_training_without_stock_evidence_fails(self):
        result = grade("miner_smith", base_console(
            "INFO [life] mine: ORE at 2446,515",
            "INFO [life] train: Blacksmithing 20.0->20.1 gained by practice"),
            {"bank": []}, {"bank": [{"item": "i_ingot_iron", "qty": 1}]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES: TRAIN-4", result.stdout)

    def test_smith_training_with_proven_stock_passes(self):
        result = grade("miner_smith", base_console(
            "INFO [life] mine: ORE at 2446,515",
            "INFO [life] 550 ore+ingots of the 550 wanted before smith training",
            "INFO [life] train: Blacksmithing 20.0->20.1 gained by practice"),
            {"bank": []}, {"bank": [{"item": "i_ingot_iron", "qty": 1}]})
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_goal_pick_after_tool_break_is_not_recovery(self):
        result = grade("lumberjack_swordsman", base_console(
            "INFO [life] first logs gathered at 100,100 (pack now holds 1)",
            "INFO [life] goal=GET_TOOL reason='hatchet broke'",
            "INFO [life] goal=GATHER_LOGS reason='tool acquired'"),
            {"bank": []}, {"bank": [{"item": "i_log", "qty": 1}]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES: FARM-3", result.stdout)

    def test_completed_farm_after_tool_break_is_recovery(self):
        result = grade("lumberjack_swordsman", base_console(
            "INFO [life] first logs gathered at 100,100 (pack now holds 1)",
            "INFO [life] goal=GET_TOOL reason='hatchet broke'",
            "INFO [life] goal_completed=GATHER_LOGS progress=1"),
            {"bank": []}, {"bank": [{"item": "i_log", "qty": 1}]})
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_already_safe_logout_with_no_travel_leg_still_passes_live4(self):
        # Hector/Castor/Leander/Baelos/Dravys (fleet122_20260907) all end this
        # way: WindDown finds `safeHere` true immediately (already on guarded
        # ground / at an open bank box), so no "wind-down: arrived somewhere
        # safe" line is ever printed -- the very next line is "checkpoint
        # (clean logout)", gated on the identical `safeHere` flag
        # (src/life/runner/Core.cpp). Quoted shape from Dravys.console.txt.
        result = grade("mage", [
            "INFO [life] session_summary duration=600s goals=4/4 gold=100->110 skills=100.0->101.0 logs=+1",
            "INFO [life] session_goals families=4 picks=4 top=25% varied=1 self_superseded=0 | upkeep=1(25%) wander=1(25%)",
            "INFO [life] train: Tactics 10.0->10.1 gained by practice",
            "INFO [life] hunt: confirmed kill target='Skeleton' corpse=0x1234",
            "INFO [life] checkpoint (clean logout) -> C:/bot_data/x.dravys/state.json",
            "INFO [life] logging out",
            "LOG event logout_complete: acked",
        ])
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("18/18 PASS", result.stdout)

    def test_logout_without_either_safe_marker_still_fails_live4(self):
        result = grade("mage", [
            "INFO [life] session_summary duration=600s goals=4/4 gold=100->110 skills=100.0->101.0 logs=+1",
            "INFO [life] session_goals families=4 picks=4 top=25% varied=1 self_superseded=0 | upkeep=1(25%) wander=1(25%)",
            "INFO [life] train: Tactics 10.0->10.1 gained by practice",
            "INFO [life] hunt: confirmed kill target='Skeleton' corpse=0x1234",
            "LOG event logout_complete: acked",
        ])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES", result.stdout)
        self.assertIn("LIVE-4", result.stdout.split("FAILING RULES:")[1])

    def test_bank_pick_after_pack_full_is_not_banking(self):
        result = grade("lumberjack_swordsman", base_console(
            "INFO [life] first logs gathered at 100,100 (pack now holds 1)",
            "INFO [life] gather: the pack is as full as this life will carry",
            "INFO [life] goal=BANK reason='pack full'"),
            {"bank": []}, {"bank": [{"item": "i_log", "qty": 1}]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILING RULES: FARM-4", result.stdout)


if __name__ == "__main__":
    unittest.main()
