"""tools/package_release.py on a small fake mod folder; no game files, no plugin build."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import package_release as pr


HERE = Path(__file__).resolve().parent
SEVEN = pr.find_7z()


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

    def test_manifest_archive_entry_is_optional(self):
        mod = self.staged()
        z = self.tmp / "r.zip"
        pr.write_zip(mod, z)
        plain = pr.manifest("1.2.3", pr.file_list(mod), z, True)
        self.assertNotIn("archive", plain)
        self.assertEqual(plain["schemaVersion"], 1)
        seven = self.tmp / "MedianXL-D2R-1.2.3.7z"
        seven.write_bytes(b"7z fake")
        m = pr.manifest("1.2.3", pr.file_list(mod), z, True, seven)
        self.assertEqual(m["archive"], {"format": "7z", "name": "MedianXL-D2R-1.2.3.7z", "size": 7, "sha256": pr.sha256(seven)})
        self.assertEqual({k: v for k, v in m.items() if k != "archive"}, plain, "nothing else changes")
        self.assertEqual(list(m).index("archive"), list(m).index("zip") + 1)

    def test_no_7zip_is_a_warning_and_no_archive(self):
        mod = self.staged()
        out = self.tmp / "r.7z"
        with mock.patch.object(pr, "find_7z", return_value=None), mock.patch("builtins.print") as shown:
            self.assertFalse(pr.write_7z(mod, out))
        self.assertFalse(out.exists())
        self.assertIn("no 7-Zip found", shown.call_args[0][0])

    def test_find_7z_looks_in_program_files_then_path(self):
        pf = self.tmp / "pf"
        (pf / "7-Zip").mkdir(parents=True)
        (pf / "7-Zip" / "7z.exe").write_bytes(b"")
        none = {"ProgramFiles": str(self.tmp / "none"), "ProgramFiles(x86)": ""}
        with mock.patch.dict("os.environ", {"ProgramFiles": str(pf)}), mock.patch.object(pr.shutil, "which", return_value="x"):
            self.assertEqual(pr.find_7z(), str(pf / "7-Zip" / "7z.exe"))
        with mock.patch.dict("os.environ", none), \
                mock.patch.object(pr.shutil, "which", side_effect=lambda n: "/bin/7zz" if n == "7zz" else None):
            self.assertEqual(pr.find_7z(), "/bin/7zz")
        with mock.patch.dict("os.environ", none), mock.patch.object(pr.shutil, "which", return_value=None):
            self.assertIsNone(pr.find_7z())

    def test_failing_7zip_raises_and_leaves_nothing(self):
        mod = self.staged()
        out = self.tmp / "r.7z"
        fake = self.tmp / "fail.py"
        fake.write_text("import sys; print('boom'); sys.exit(2)")
        if sys.platform == "win32":
            script = self.tmp / "fail.cmd"
            script.write_text(f'@"{sys.executable}" "{fake}"\r\n')
        else:
            script = self.tmp / "fail.sh"
            script.write_text(f'#!/bin/sh\n"{sys.executable}" "{fake}"\n')
            script.chmod(0o755)
        with self.assertRaises(RuntimeError):
            pr.write_7z(mod, out, str(script))
        self.assertFalse(out.exists())

    @unittest.skipUnless(SEVEN, "no 7-Zip on this machine")
    def test_real_7z_has_the_zips_content_and_is_reproducible(self):
        mod = self.staged({"global/excel/armor.txt": b"Median armor\n" * 5000, "global/ui/a.json": b"{}"})
        zip_path = self.tmp / "r.zip"
        pr.write_zip(mod, zip_path)
        a, b = self.tmp / "a.7z", self.tmp / "b.7z"
        self.assertTrue(pr.write_7z(mod, a))
        for p in mod.rglob("*"):  # other mtimes: write_7z sets them again, the bytes stay
            os.utime(p, (1.5e9, 1.5e9))
        self.assertTrue(pr.write_7z(mod, b))
        self.assertEqual(pr.sha256(a), pr.sha256(b))
        listing = subprocess.run([SEVEN, "l", "-slt", str(a)], capture_output=True, text=True).stdout
        self.assertIn("Method = LZMA2", listing)
        self.assertIn("Solid = +", listing)
        paths = {ln[7:].replace("\\", "/") for ln in listing.splitlines() if ln.startswith("Path = ")} - {a.as_posix()}
        zip_names = {n.rstrip("/") for n in zipfile.ZipFile(zip_path).namelist()}
        self.assertTrue(zip_names <= paths, zip_names - paths)  # every zip entry; the 7z also lists the parent folders
        self.assertTrue(all(n == "MedianXL" or n.startswith("MedianXL/") for n in paths), paths)
        out = self.tmp / "unpacked"
        subprocess.run([SEVEN, "x", "-y", f"-o{out}", str(a)], check=True, capture_output=True)
        self.assertEqual(pr.file_list(out / "MedianXL"), pr.file_list(mod))
        self.assertTrue((out / "MedianXL" / "d2rloader" / "patches").is_dir())

    @unittest.skipUnless(SEVEN, "no 7-Zip on this machine")
    def test_main_writes_zip_and_7z_and_lists_both(self):
        data = self.tmp / "d"
        (data / "global" / "excel").mkdir(parents=True)
        (data / "global" / "excel" / "armor.txt").write_bytes(b"Median armor\n" * 1000)
        out = self.tmp / "dist"

        def run(*extra):
            return subprocess.run([sys.executable, "-I", str(HERE / "package_release.py"), "--plugin", str(self.dll),
                                   "--version", "1.2.3", "--data-from", str(data), "--out", str(out), *extra],
                                  capture_output=True, text=True)
        r = run()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        seven = out / "MedianXL-D2R-1.2.3.7z"
        man = json.loads((out / "release-manifest.json").read_text())
        self.assertEqual(man["archive"]["name"], seven.name)
        self.assertEqual(man["archive"]["size"], seven.stat().st_size)
        self.assertEqual(man["archive"]["sha256"], pr.sha256(seven))
        self.assertEqual(man["zip"]["name"], "MedianXL-D2R-1.2.3.zip")
        sums = (out / "SHA256SUMS").read_text().splitlines()
        self.assertIn(f"{pr.sha256(seven)}  {seven.name}", sums)
        self.assertEqual(len(sums), 4)
        notes = (out / "RELEASE_NOTES-1.2.3.md").read_text()
        self.assertIn("`MedianXL-D2R-1.2.3.7z`", notes)
        self.assertIn("Windows 11 opens .7z files", notes)
        self.assertNotIn("Unzip", notes)
        self.assertIn("as 7z", r.stdout)
        r = run("--no-7z")  # drops the 7z of the earlier run; the zip alone
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertFalse(seven.exists())
        self.assertNotIn("archive", json.loads((out / "release-manifest.json").read_text()))
        self.assertEqual(len((out / "SHA256SUMS").read_text().splitlines()), 3)
        self.assertIn("Unzip `MedianXL-D2R-1.2.3.zip`", (out / "RELEASE_NOTES-1.2.3.md").read_text())

    def test_no_data_package_has_no_7z(self):
        out = self.tmp / "dist"
        r = subprocess.run([sys.executable, "-I", str(HERE / "package_release.py"), "--no-data", "--plugin", str(self.dll),
                            "--version", "1.2.3", "--out", str(out)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(sorted(p.name for p in out.glob("*.7z")), [])
        self.assertNotIn("archive", json.loads((out / "release-manifest.json").read_text()))

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
