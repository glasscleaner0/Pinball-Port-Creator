# SPDX-License-Identifier: GPL-3.0-only
"""Asset-import boundary checks; optional --source/--archive enable real XP checks."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
import unittest
from unittest import mock
import zipfile
import xp_assets

_SOURCE = None
_ARCHIVE = None
_SCRATCH = None


class AssetChecks(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="asset-check-", dir=_SCRATCH)).resolve()
        self.dest = self.folder / "prepared"

    def tearDown(self):
        if _SCRATCH is not None and self.folder.parent != Path(_SCRATCH).resolve():
            raise RuntimeError("Test cleanup target escaped scratch folder")
        if not self.folder.name.startswith("asset-check-"):
            raise RuntimeError("Test cleanup target has unexpected name")
        shutil.rmtree(self.folder)

    def make_zip(self, entries):
        target = self.folder / "input.zip"
        with zipfile.ZipFile(target, "w") as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
        return target

    def test_zip_traversal_rejected_without_output(self):
        source = self.make_zip([("../escape.dat", b"payload")])
        with self.assertRaisesRegex(xp_assets.AssetError, "Unsafe"):
            xp_assets.prepare_assets(source, self.dest)
        self.assertFalse(self.dest.exists())
        self.assertFalse((self.folder.parent / "escape.dat").exists())

    def test_windows_absolute_and_ads_paths_rejected(self):
        for value in ("C:/PINBALL.DAT", "nested/file:stream", "\\\\host\\share\\PINBALL.DAT"):
            with self.subTest(value=value), self.assertRaises(xp_assets.AssetError):
                xp_assets._safe_member(value)

    def test_case_collisions_rejected(self):
        source = self.make_zip([("PINBALL.DAT", b"a"), ("pinball.dat", b"b")])
        with self.assertRaisesRegex(xp_assets.AssetError, "colliding"):
            xp_assets.prepare_assets(source, self.dest)
        self.assertFalse(self.dest.exists())

    def test_duplicate_basename_is_ambiguous(self):
        source = self.make_zip([("one/PINBALL.DAT", b"a"), ("two/PINBALL.DAT", b"b")])
        with self.assertRaisesRegex(xp_assets.AssetError, "Ambiguous"):
            xp_assets.prepare_assets(source, self.dest)

    def test_zip_symlink_rejected(self):
        entry = zipfile.ZipInfo("PINBALL.DAT")
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        source = self.make_zip([(entry, b"/etc/passwd")])
        with self.assertRaisesRegex(xp_assets.AssetError, "link or special"):
            xp_assets.prepare_assets(source, self.dest)

    def test_zip_file_count_limit(self):
        source = self.make_zip([("file" + str(i), b"") for i in range(xp_assets.MAX_FILES + 1)])
        with self.assertRaisesRegex(xp_assets.AssetError, "file-count"):
            xp_assets.prepare_assets(source, self.dest)

    def test_per_file_declared_size_limit(self):
        with self.assertRaisesRegex(xp_assets.AssetError, "per-file"):
            xp_assets._index_record({}, "PINBALL.DAT", xp_assets.MAX_MEMBER_BYTES + 1, None, set())

    def test_bad_7z_rejected_before_external_parser(self):
        source = self.folder / "wrong.7z"
        source.write_bytes(b"This is not the approved archive")
        with mock.patch.object(xp_assets.subprocess, "run", side_effect=AssertionError("must not parse")):
            with self.assertRaisesRegex(xp_assets.AssetError, "not the verified"):
                xp_assets.prepare_assets(source, self.dest)

    def test_bad_7z_rejected_before_injected_backend(self):
        source = self.folder / "wrong.7z"
        source.write_bytes(b"unverified archive")
        runner = mock.Mock(side_effect=AssertionError("unverified input reached backend"))
        with self.assertRaisesRegex(xp_assets.AssetError, "not the verified"):
            xp_assets.prepare_assets(source, self.dest, sevenzip_runner=runner,
                                     sevenzip_archive_path="/incoming/game.7z")
        runner.assert_not_called()
        self.assertFalse(self.dest.exists())

    def test_translated_7z_path_requires_backend(self):
        source = self.folder / "expected.7z"
        source.write_bytes(b"synthetic fixture")
        with mock.patch.object(xp_assets, "_file_digest", return_value=xp_assets.ARCHIVE_SHA256):
            with self.assertRaisesRegex(xp_assets.AssetError, "requires an explicit"):
                xp_assets.prepare_assets(source, self.dest,
                                         sevenzip_archive_path="/incoming/game.7z")
        self.assertFalse(self.dest.exists())

    def test_injected_backend_listing_is_still_path_checked(self):
        source = self.folder / "expected.7z"
        source.write_bytes(b"synthetic fixture")
        response = xp_assets.subprocess.CompletedProcess([], 0,
            stdout=b"Path = ../escape.dat\nSize = 1\nEncrypted = -\n", stderr=b"")
        runner = mock.Mock(return_value=response)
        with mock.patch.object(xp_assets, "_file_digest", return_value=xp_assets.ARCHIVE_SHA256):
            with mock.patch.object(xp_assets, "_seven_zip", side_effect=AssertionError("must not find a host extractor")):
                with self.assertRaisesRegex(xp_assets.AssetError, "Unsafe"):
                    xp_assets.prepare_assets(source, self.dest, sevenzip_runner=runner,
                                             sevenzip_archive_path="/incoming/game.7z")
        runner.assert_called_once_with(["l", "-slt", "-ba", "-bd", "-sccUTF-8", "--", "/incoming/game.7z"], timeout=20)
        self.assertFalse(self.dest.exists())
        self.assertFalse((self.folder.parent / "escape.dat").exists())

    def test_injected_backend_requires_binary_stdout(self):
        response = xp_assets.subprocess.CompletedProcess([], 0, stdout="text", stderr="")
        with self.assertRaisesRegex(xp_assets.AssetError, "invalid binary"):
            xp_assets._run_seven_zip(["l"], runner=lambda *args, **kwargs: response)

    def test_actual_7z_backend_uses_translated_path_without_host_discovery(self):
        if _ARCHIVE is None:
            self.skipTest("pass --archive for actual7z backend route")
        source = Path(_ARCHIVE).resolve()
        program = xp_assets._seven_zip()
        translated = "/incoming/original XP archive.7z"
        calls = []
        def runner(arguments, *, timeout):
            calls.append(list(arguments))
            args = list(arguments)
            path_index = args.index("--") + 1
            self.assertEqual(args[path_index], translated)
            args[path_index] = str(source)
            flags = xp_assets.subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            return xp_assets.subprocess.run([program, *args], check=False,
                       stdout=xp_assets.subprocess.PIPE, stderr=xp_assets.subprocess.PIPE,
                       timeout=timeout, creationflags=flags)
        with mock.patch.object(xp_assets, "_seven_zip", side_effect=AssertionError("host tool discovery must not run")):
            report = xp_assets.prepare_assets(source, self.dest, sevenzip_runner=runner,
                                             sevenzip_archive_path=translated)
        self.assertEqual(report["source_kind"], "7z")
        self.assertEqual(len(report["runtime_files"]), 62)
        self.assertEqual(len(calls), 63)
        self.assertEqual(hashlib.sha256((self.dest / "PBMSG_FT.bin").read_bytes()).hexdigest(),
                         "3679477baf73444a2b5a5d93b32153a7266c7e5edfb4d4644a917ca6766cb4c8")

    def test_existing_destination_preserved(self):
        self.dest.mkdir()
        marker = self.dest / "user-file"
        marker.write_text("keep", encoding="utf-8")
        source = self.make_zip([])
        with self.assertRaisesRegex(xp_assets.AssetError, "already exists"):
            xp_assets.prepare_assets(source, self.dest)
        self.assertEqual(marker.read_text(), "keep")

    def test_download_source_is_exact_and_https_only(self):
        with self.assertRaisesRegex(xp_assets.AssetError, "hash-pinned"):
            xp_assets.download_archive(self.folder / "cache", "https://example.com/file.7z")
        for value in ("http://archive.org/file", "https://archive.org.evil.test/file", "https://user@archive.org/file"):
            with self.subTest(value=value), self.assertRaises(xp_assets.AssetError):
                xp_assets._archive_address(value)
        xp_assets._archive_address("https://ia800001.us.archive.org/path/file.7z")

    def test_poisoned_download_cache_is_not_overwritten(self):
        cache = self.folder / "cache"
        cache.mkdir()
        target = cache / ("xp-reference-" + xp_assets.ARCHIVE_SHA256[:16] + ".7z")
        target.write_bytes(b"keep this different file")
        with self.assertRaisesRegex(xp_assets.AssetError, "refusing to overwrite"):
            xp_assets.download_archive(cache)
        self.assertEqual(target.read_bytes(), b"keep this different file")

    def test_unverified_download_is_removed(self):
        response = io.BytesIO(b"unexpected server content")
        response.headers = {"Content-Length": "25"}
        response.geturl = lambda: xp_assets.ARCHIVE_URL
        opener = mock.Mock()
        opener.open.return_value = response
        cache = self.folder / "cache"
        with mock.patch.object(xp_assets.urllib.request, "build_opener", return_value=opener):
            with self.assertRaisesRegex(xp_assets.AssetError, "hash differs"):
                xp_assets.download_archive(cache)
        self.assertEqual(list(cache.iterdir()), [])

    def test_actual_folder_has_identical_assets_and_static_font(self):
        if _SOURCE is None:
            self.skipTest("pass --source for actual original XP fixtures")
        with mock.patch.object(xp_assets.urllib.request, "build_opener", side_effect=AssertionError("offline import must not download")):
            report = xp_assets.prepare_assets(Path(_SOURCE), self.dest)
        self.assertEqual(len(report["runtime_files"]), 62)
        self.assertFalse(report["original_executable_executed"])
        self.assertFalse(list(self.dest.rglob("*.exe")))
        self.assertEqual(hashlib.sha256((self.dest / "PBMSG_FT.bin").read_bytes()).hexdigest(),
                         "3679477baf73444a2b5a5d93b32153a7266c7e5edfb4d4644a917ca6766cb4c8")
        self.assertEqual(len(list((self.dest / "SOUNDS").iterdir())), 60)

    def test_repacked_zip_and_folder_produce_identical_runtime_hashes(self):
        if _SOURCE is None:
            self.skipTest("pass --source for actual original XP fixtures")
        source = self.folder / "xp.zip"
        with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
            for item in Path(_SOURCE).iterdir():
                if item.is_file():
                    archive.write(item, "XP/" + item.name)
        report = xp_assets.prepare_assets(source, self.dest)
        expected = xp_assets._metadata()
        hashes = {record["path"].rsplit("/", 1)[-1]: record["sha256"] for record in report["runtime_files"]}
        for record in expected["files"]:
            if record["name"] != "Pinball.exe":
                self.assertEqual(hashes[record["name"]], record["sha256"])

    def test_corrupted_same_size_asset_rejected_before_output(self):
        if _SOURCE is None:
            self.skipTest("pass --source for actual original XP fixtures")
        source = self.folder / "changed"
        shutil.copytree(_SOURCE, source)
        dat = source / "PINBALL.DAT"
        data = bytearray(dat.read_bytes())
        data[0] ^= 1
        dat.write_bytes(data)
        with self.assertRaisesRegex(xp_assets.AssetError, "does not match"):
            xp_assets.prepare_assets(source, self.dest)
        self.assertFalse(self.dest.exists())

    def test_original_7z_and_verified_cache(self):
        if _ARCHIVE is None:
            self.skipTest("pass --archive for actual7z and verified-cache checks")
        report = xp_assets.prepare_assets(Path(_ARCHIVE), self.dest)
        self.assertEqual(report["source_kind"], "7z")
        self.assertEqual(len(report["runtime_files"]), 62)
        cache = self.folder / "cache"
        cache.mkdir()
        target = cache / ("xp-reference-" + xp_assets.ARCHIVE_SHA256[:16] + ".7z")
        shutil.copyfile(_ARCHIVE, target)
        with mock.patch.object(xp_assets.urllib.request, "build_opener", side_effect=AssertionError("verified cache must not download")):
            self.assertEqual(xp_assets.download_archive(cache), target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--scratch", type=Path)
    options, remaining = parser.parse_known_args()
    _SOURCE, _ARCHIVE, _SCRATCH = options.source, options.archive, options.scratch
    if _SCRATCH is not None:
        _SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[__file__] + remaining, verbosity=2)
