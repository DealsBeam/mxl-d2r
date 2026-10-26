"""Slot selection/refusal checks; no game files or saves are written."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import game_slots
import install_mod


class InstallSlotTests(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, MXL_SLOT="3", MXL_RUN="waypoints")
        self.env.start()
        self.addCleanup(self.env.stop)

    def test_environment_selects_slot_three(self):
        with patch.object(sys, "argv", ["install_mod.py"]), \
                patch.object(Path, "exists", return_value=True), \
                patch.object(install_mod, "install_one", return_value=0) as install, \
                patch.object(game_slots, "setup"):
            self.assertEqual(install_mod.main(), 0)
            self.assertEqual(install.call_args.args[:3], (Path(game_slots.slot_dir(3)), "D2R-XL", "D2R-XL-s3/"))

    def test_foreign_lock_refuses_before_file_changes(self):
        with patch.object(game_slots, "lock_info", return_value=("lock", "another pid=0")), \
                patch.object(Path, "exists", side_effect=AssertionError("refused install probed game files")), \
                patch.object(install_mod.shutil, "rmtree") as remove:
            self.assertEqual(install_mod.install_one(Path(game_slots.slot_dir(3)), "D2R-XL", "D2R-XL-s3/"), 1)
            remove.assert_not_called()

    def test_running_game_refuses_even_with_our_lock(self):
        with patch.object(game_slots, "lock_info", return_value=("lock", "waypoints pid=0")), \
                patch.object(game_slots, "slot_processes", return_value=[(123, "D2RLoader-s3.exe")]), \
                patch.object(Path, "exists", side_effect=AssertionError("running game files were probed")), \
                patch.object(install_mod.shutil, "rmtree") as remove:
            self.assertEqual(install_mod.install_one(Path(game_slots.slot_dir(3)), "D2R-XL", "D2R-XL-s3/"), 1)
            remove.assert_not_called()

    def test_refused_install_does_not_change_slot_config(self):
        with patch.object(sys, "argv", ["install_mod.py"]), \
                patch.object(Path, "exists", return_value=True), \
                patch.object(install_mod, "install_one", return_value=1), \
                patch.object(game_slots, "setup") as setup:
            self.assertEqual(install_mod.main(), 1)
            setup.assert_not_called()


if __name__ == "__main__":
    unittest.main()
