"""tools/package_release.py on a small fake mod folder; no game files, no plugin build."""
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import package_release as pr


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        self.dll = self.tmp / "mxl-core.dll"
        self.dll.write_bytes(b"MZ..." + pr.plugin_version().encode() + b"\0...")
        self.mod = self.tmp / "stage" / "MedianXL"

    def staged(self, files=None):
        data = self.tmp / "data"
        for rel, body in (files or {"global/excel/armor.txt": b"Median armor\n"}).items():
            (data / rel).parent.mkdir(parents=True, exist_ok=True)
            (data / rel).write_bytes(body)
        pr.stage(self.mod, "1.2.3", self.dll, self.tmp, data_from=data)
        return self.mod

    def test_layout_matches_install_mod(self):
        mod = self.staged()
        self.assertEqual(json.loads((mod / "MedianXL.mpq" / "modinfo.json").read_text()), {"name": "MedianXL", "savepath": "MedianXL/"})
        meta = json.loads((mod / "d2rloader" / "metadata.json").read_text())
        self.assertEqual(meta["namespace"], "medianxl")
        self.assertEqual(meta["metadata"]["modVersion"], "1.2.3")
        self.assertEqual(meta["metadata"]["website"], "https://github.com/Nugwa628/mxl-d2r")
        toml = (mod / "d2rloader" / "config" / "d2rloader.toml").read_text()
        self.assertIn('[d2rcore.save_format]\nmode = "extended"', toml)
        self.assertIn("[d2rcore.stash]\nadd_shared_tabs = 7", toml)
        self.assertTrue((mod / "d2rloader" / "plugins" / "mxl-core.dll").is_file())
        self.assertTrue((mod / "d2rloader" / "patches").is_dir())

    def test_zip_has_top_folder_and_empty_patches(self):
        mod = self.staged()
        out = self.tmp / "r.zip"
        pr.write_zip(mod, out)
        names = zipfile.ZipFile(out).namelist()
        self.assertTrue(all(n.startswith("MedianXL/") for n in names))
        self.assertIn("MedianXL/MedianXL.mpq/data/global/excel/armor.txt", names)
        self.assertIn("MedianXL/d2rloader/patches/", names)
        out2 = self.tmp / "r2.zip"
        pr.write_zip(mod, out2)
        self.assertEqual(pr.sha256(out), pr.sha256(out2), "same input, same zip")

    def test_manifest_lists_every_file(self):
        mod = self.staged()
        out = self.tmp / "r.zip"
        pr.write_zip(mod, out)
        files = pr.file_list(mod)
        m = pr.manifest("1.2.3", files, out, True)
        self.assertEqual(m["schemaVersion"], 1)
        self.assertEqual(m["d2r"]["build"], "93847")
        self.assertEqual(m["d2rloader"]["version"], "1.3.1-beta")
        self.assertEqual(len(m["d2rloader"]["sha256"]), 64)
        paths = [f["path"] for f in files]
        self.assertEqual(paths, sorted(paths))
        self.assertIn("MedianXL.mpq/data/global/excel/armor.txt", paths)
        self.assertIn("d2rloader/plugins/mxl-core.dll", paths)
        armor = next(f for f in files if f["path"].endswith("armor.txt"))
        self.assertEqual(armor["size"], len(b"Median armor\n"))
        self.assertEqual(m["fileCount"], len(files))
        self.assertTrue(set(m["preserve"]) <= set(paths))

    def test_test_and_player_files_are_refused(self):
        mod = self.staged({"global/excel/armor.txt": b"x", "global/x.txt": b"y"})
        self.assertEqual(pr.forbidden_files(mod), [])
        for rel in ("d2rloader/plugins/mxl-testharness.txt", "d2rloader/plugins/mxl-core-off.txt", "Hero.d2s",
                    "d2rloader/logs/mxl-core.log", "d2rloader/config/input-bindings.toml", "Hero.map", "Hero.d2rl", "Hero.ctl"):
            (mod / rel).parent.mkdir(parents=True, exist_ok=True)
            (mod / rel).write_text("x")
        self.assertEqual(len(pr.forbidden_files(mod)), 8)

    def test_byte_copies_of_d2r_files_are_left_out(self):
        mod = self.staged({"global/excel/armor.txt": b"Median armor\n", "global/excel/sounds.txt": b"vanilla\n",
                           "global/dataversionbuild.txt": b"93847"})
        extract = self.tmp / "extract"
        for rel, body in {"global/excel/sounds.txt": b"vanilla\n", "global/excel/armor.txt": b"vanilla armor\n",
                          "global/dataversionbuild.txt": b"93847"}.items():
            (extract / "data" / rel).parent.mkdir(parents=True, exist_ok=True)
            (extract / "data" / rel).write_bytes(body)
        self.assertEqual(pr.drop_blizzard_copies(mod, extract), 1)
        data = mod / "MedianXL.mpq" / "data"
        self.assertFalse((data / "global/excel/sounds.txt").exists())
        self.assertTrue((data / "global/excel/armor.txt").exists())
        self.assertTrue((data / "global/dataversionbuild.txt").exists())

    def test_harness_plugin_is_refused(self):
        self.assertEqual(pr.check_release_plugin(self.dll, pr.plugin_version()), [])
        dev = self.tmp / "dev.dll"
        dev.write_bytes(self.dll.read_bytes() + b"mxl-testharness.txt\0")
        self.assertEqual(len(pr.check_release_plugin(dev, pr.plugin_version())), 1)
        self.assertEqual(len(pr.check_release_plugin(self.dll, "9.9.9")), 1)


if __name__ == "__main__":
    unittest.main()
