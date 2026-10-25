"""Check-name validation of game_checks.run_all (lane ied-vanish); no game is started and no run folder is made."""
import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_checks


class UnknownCheckNames(unittest.TestCase):
    def test_known_and_marker_names_pass(self):
        known = [name for name, _ in game_checks.CHECKS]
        self.assertEqual(game_checks.unknown_checks(known), [])
        self.assertEqual(game_checks.unknown_checks(["-"]), [])
        self.assertEqual(game_checks.unknown_checks(None), [])
        self.assertEqual(game_checks.unknown_checks([]), [])

    def test_misspelt_name_is_listed(self):
        self.assertEqual(game_checks.unknown_checks(["ied", "tooltip", "itemevent"]), ["tooltip", "itemevent"])

    def test_run_all_refuses_before_run_folder_lock_and_game(self):
        with patch.object(game_checks, "new_run", side_effect=AssertionError("made a run folder")), \
                patch.object(game_checks, "lock", side_effect=AssertionError("took the lock")):
            self.assertEqual(game_checks.run_all(["ied", "tooltip"]), 2)

    def test_check_names_are_unique(self):
        names = [name for name, _ in game_checks.CHECKS]
        self.assertEqual(sorted(set(names)), sorted(names))


if __name__ == "__main__":
    unittest.main()
