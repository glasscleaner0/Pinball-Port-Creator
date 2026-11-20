#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""File-safety and packaging checks; no compiler, XP program or network runs.

Synthetic XEX/asset fixtures test packaging only, not game validity or gameplay.
The retained dependency notice payloads are checked against their real manifest.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import builder

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCRATCH = PROJECT_ROOT / 'work' / 'portable-builder' / 'builder-tests'
SCRATCH = DEFAULT_SCRATCH


class BuilderTests(unittest.TestCase):
    def setUp(self):
        if SCRATCH.is_symlink():
            self.fail('Test scratch directory must not be a symlink')
        SCRATCH.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='case-', dir=SCRATCH)
        self.case = Path(self.temporary.name).resolve()
        self.addCleanup(self.cleanup_case)
        self.assertTrue(self.case.is_relative_to(SCRATCH.resolve()))
        self.root = self.case / 'source'
        self.root.mkdir()
        (self.root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(Fixture)\n', encoding='utf-8')
        for name in ('LICENSE', 'LICENSE-SCOPE.txt'):
            shutil.copyfile(PROJECT_ROOT / name, self.root / name)
        for name, content in (
            ('src/rewrite/xbox360_main.cpp', '// Fresh synthetic test source.\nint main(){return 0;}\n'),
            ('tests/rewrite/fixture.cpp', '// Fresh synthetic test check.\n'),
            ('tools/portable/builder.py', '# Fresh builder fixture.\n'),
            ('tools/portable/xp_assets.py', '# Fresh importer fixture.\n'),
            ('tools/portable/README.txt', 'Synthetic Creator quick start: open Create Game.pyw.\n'),
        ):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
        self.portable = self.root / 'tools' / 'portable'
        shutil.copyfile(PROJECT_ROOT / 'tools/portable/BUILD-FROM-SOURCE.txt',
                        self.portable / 'BUILD-FROM-SOURCE.txt')
        self.notices = json.loads((PROJECT_ROOT / 'tools/portable/runtime-notices.json').read_text(encoding='utf-8-sig'))
        builder.save_json(self.portable / 'runtime-notices.json', self.notices)
        for entry in self.notices['files']:
            destination = self.portable / entry['path']
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(PROJECT_ROOT / 'tools/portable' / entry['path'], destination)
        self.input_assets = self.case / 'user-xp-files'
        self.input_assets.mkdir()
        self.output = self.case / 'output'
        self.cache = self.case / 'compiler-cache'

    def cleanup_case(self):
        # Every recursive deletion is confined to the unique directory this test owns.
        resolved = self.case.resolve()
        if resolved.parent != SCRATCH.resolve() or not resolved.name.startswith('case-'):
            raise RuntimeError('Refusing cleanup outside the test-owned scratch directory')
        self.temporary.cleanup()

    def arguments(self, *, output=None, cache=None):
        return ['--source', str(self.root), '--assets', str(self.input_assets),
                '--output', str(self.output if output is None else output),
                '--cache', str(self.cache if cache is None else cache), '--backend', 'native']

    def invoke(self, arguments, *, importer=None, compiler=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(builder, 'choose_backend', return_value='native'), \
             patch.object(builder, 'prepare_assets', side_effect=importer) as import_mock, \
             patch.object(builder, 'compile_game', side_effect=compiler) as compile_mock, \
             patch.object(builder, 'download_archive', side_effect=AssertionError('Network download is forbidden in builder tests')), \
             contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            status = builder.main(arguments)
        return status, stdout.getvalue(), stderr.getvalue(), import_mock, compile_mock

    @staticmethod
    def stage_assets(_source, destination):
        """Deliberately synthetic bytes, distinct from any original game asset."""
        destination.mkdir(parents=True, exist_ok=False)
        (destination / 'PINBALL.DAT').write_bytes(b'synthetic table asset fixture')
        (destination / 'PBMSG_FT.bin').write_bytes(b'synthetic font fixture')
        (destination / 'SOUNDS').mkdir()
        (destination / 'SOUNDS' / 'SOUND1.WAV').write_bytes(b'synthetic wave fixture')
        builder.save_json(destination / 'ASSET-PROVENANCE.json', {'fixture': True, 'original_executable_executed': False})
        return {'fixture': True, 'original_executable_executed': False}

    @staticmethod
    def stage_compiler(_args, _root, _cache, compiled):
        """A recognizable wrapper fixture, not an executable or gameplay check."""
        (compiled / 'default.xex').write_bytes(b'XEX2\x00synthetic packaging fixture\x00')
        builder.save_json(compiled / 'BUILD-TOOLCHAIN.json', {
            'fixture': True, 'xex_sha256': builder.digest(compiled / 'default.xex')})
        return 'native'

    def test_existing_output_is_preserved_without_starting_work(self):
        self.output.mkdir()
        report = self.output / 'build-report.json'
        report.write_bytes(b'previous accepted build report\n')
        sentinel = self.output / 'default.xex'
        sentinel.write_bytes(b'previous accepted executable\n')
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        status, _, _, importer, compiler = self.invoke(self.arguments())
        self.assertEqual(status, 1)
        self.assertEqual({p.name: p.read_bytes() for p in self.output.iterdir()}, before)
        self.assertFalse(self.cache.exists())
        importer.assert_not_called()
        compiler.assert_not_called()

    def test_output_inside_source_or_original_trees_is_rejected_before_creation(self):
        for tree in ('src/rewrite', 'tests/rewrite', 'tools/portable', 'original'):
            with self.subTest(tree=tree):
                output = self.root / tree / 'new-output'
                status, _, _, importer, compiler = self.invoke(self.arguments(output=output))
                self.assertEqual(status, 1)
                self.assertFalse(output.exists())
                self.assertFalse(self.cache.exists())
                importer.assert_not_called()
                compiler.assert_not_called()

    def test_cache_inside_source_or_original_trees_is_rejected_before_creation(self):
        for tree in ('src/rewrite', 'tests/rewrite', 'tools/portable', 'original'):
            with self.subTest(tree=tree):
                cache = self.root / tree / 'new-cache'
                status, _, _, importer, compiler = self.invoke(self.arguments(cache=cache))
                self.assertEqual(status, 1)
                self.assertFalse(cache.exists())
                self.assertFalse(self.output.exists())
                importer.assert_not_called()
                compiler.assert_not_called()

    def test_source_root_and_ancestor_cannot_be_cache_or_output(self):
        for choice in ('output', 'cache'):
            for target in (self.root, self.case):
                with self.subTest(choice=choice, target=target.name):
                    status, _, _, importer, compiler = self.invoke(self.arguments(**{choice: target}))
                    self.assertEqual(status, 1)
                    self.assertFalse((self.root / 'build-report.json').exists())
                    self.assertFalse((self.case / 'build-report.json').exists())
                    self.assertFalse(self.output.exists())
                    self.assertFalse(self.cache.exists())
                    importer.assert_not_called()
                    compiler.assert_not_called()

    def test_source_archive_contains_build_material_and_not_xp_or_build_outputs(self):
        for relative in ('original/XP/PINBALL.DAT', 'work/assets/PBMSG_FT.bin',
                         'outputs/accepted/default.xex', '.pinball-cache/runtime.a'):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'not distributable source fixture')
        archive = self.case / 'Source.zip'
        builder.archive_source(self.root, archive)
        with zipfile.ZipFile(archive) as source_zip:
            names = set(source_zip.namelist())
            readme = source_zip.read('README.txt')
            self.assertEqual(readme, (self.portable / 'BUILD-FROM-SOURCE.txt').read_bytes())
            self.assertNotEqual(readme, (self.portable / 'README.txt').read_bytes())
            self.assertIn(b'Start Builder.pyw', readme)
            for name in ('CMakeLists.txt', 'LICENSE', 'LICENSE-SCOPE.txt', 'build.py', 'README.txt',
                         'src/rewrite/xbox360_main.cpp', 'tests/rewrite/fixture.cpp',
                         'tools/portable/builder.py', 'tools/portable/runtime-notices.json'):
                self.assertIn(name, names)
            for entry in self.notices['files']:
                name = 'tools/portable/' + entry['path']
                self.assertIn(name, names)
                self.assertEqual(source_zip.read(name), (self.portable / entry['path']).read_bytes())
            self.assertFalse(any(name.startswith(('original/', 'work/', 'outputs/', '.pinball-cache/')) for name in names))
            self.assertFalse(any(name.lower().endswith(('.dat', '.wav', '.bin', '.exe', '.xex', '.a')) for name in names))
        with self.assertRaises(FileExistsError):
            builder.archive_source(self.root, archive)

    def test_changed_notice_stops_before_output_or_compilation(self):
        entry = self.notices['files'][0]
        (self.portable / entry['path']).write_bytes(b'changed notice fixture')
        status, _, stderr, importer, compiler = self.invoke(self.arguments())
        self.assertEqual(status, 1)
        self.assertIn('notice', stderr.lower())
        self.assertFalse(self.output.exists())
        self.assertFalse(self.cache.exists())
        importer.assert_not_called()
        compiler.assert_not_called()

    def test_notice_destination_cannot_escape_package(self):
        self.notices['files'][0]['output_path'] = '../escaped-notice.txt'
        builder.save_json(self.portable / 'runtime-notices.json', self.notices)
        self.output.mkdir()
        with self.assertRaises(builder.BuildError):
            builder.copy_notices(self.portable, self.output)
        self.assertFalse((self.case / 'escaped-notice.txt').exists())
        self.assertEqual(list(self.output.iterdir()), [])

    def test_both_packages_share_executable_and_keep_config_notices_and_hashes(self):
        status, _, stderr, importer, compiler = self.invoke(
            self.arguments(), importer=self.stage_assets, compiler=self.stage_compiler)
        self.assertEqual(status, 0, stderr)
        importer.assert_called_once()
        compiler.assert_called_once()
        hashes = []
        for name, mode in (('Xbox360', 'console'), ('Xenia', 'xenia')):
            directory = self.output / name
            hashes.append(builder.digest(directory / 'default.xex'))
            self.assertIn('present=' + mode + '\n', (directory / 'pinball360.ini').read_text(encoding='utf-8'))
            self.assertEqual((directory / 'COPYING.txt').read_bytes(), (self.root / 'LICENSE').read_bytes())
            self.assertEqual((directory / 'LICENSE-SCOPE.txt').read_bytes(), (self.root / 'LICENSE-SCOPE.txt').read_bytes())
            for entry in self.notices['files']:
                self.assertEqual(builder.digest(directory / entry['output_path']), entry['sha256'])
            manifest = json.loads((directory / 'PACKAGE-MANIFEST.json').read_text(encoding='utf-8'))
            self.assertEqual(manifest['presentation'], mode)
            self.assertEqual(manifest['gameplay_validation'], 'not performed by builder')
            self.assertEqual(manifest['xex_sha256'], hashes[-1])
            for entry in manifest['files']:
                payload = directory / entry['path']
                self.assertEqual(payload.stat().st_size, entry['bytes'])
                self.assertEqual(builder.digest(payload), entry['sha256'])
            recorded = {}
            for line in (directory / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
                digest, relative = line.split('  ', 1)
                recorded[relative] = digest
                self.assertEqual(builder.digest(directory / relative), digest)
            actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file() and p.name != 'SHA256SUMS.txt'}
            self.assertEqual(set(recorded), actual)
            self.assertTrue(all(builder.fatx_path(p.relative_to(directory)) for p in directory.rglob('*')))
        self.assertEqual(hashes[0], hashes[1])
        report = json.loads((self.output / 'build-report.json').read_text(encoding='utf-8'))
        self.assertEqual(report['result'], 'complete')
        self.assertEqual(len(report['packages']), 2)
        self.assertEqual(report['source_archive_sha256'], builder.digest(self.output / 'Source.zip'))
        with zipfile.ZipFile(self.output / 'Source.zip') as source_zip:
            self.assertFalse(any(name.lower().endswith(('.dat', '.wav', '.bin', '.exe', '.xex')) for name in source_zip.namelist()))

    def test_compiler_failure_retains_diagnostics_and_reports_no_package_success(self):
        def failed_compiler(*_):
            raise builder.BuildError('Synthetic compiler failure')
        status, _, stderr, _, _ = self.invoke(self.arguments(), importer=self.stage_assets, compiler=failed_compiler)
        self.assertEqual(status, 1)
        self.assertIn('Synthetic compiler failure', stderr)
        report = json.loads((self.output / 'build-report.json').read_text(encoding='utf-8'))
        self.assertEqual(report['result'], 'failed')
        self.assertIn('Synthetic compiler failure', report['error'])
        self.assertTrue((self.output / '.build/assets/PINBALL.DAT').is_file())
        self.assertFalse((self.output / 'Source.zip').exists())
        self.assertFalse((self.output / 'Xbox360').exists())

    def test_source_changed_during_compile_is_not_packaged_as_matching_source(self):
        def changed_source(args, root, cache, compiled):
            self.stage_compiler(args, root, cache, compiled)
            (root / 'src/rewrite/xbox360_main.cpp').write_text('// Changed during synthetic compile.\n', encoding='utf-8')
            return 'native'
        status, _, stderr, _, _ = self.invoke(self.arguments(), importer=self.stage_assets, compiler=changed_source)
        self.assertEqual(status, 1)
        self.assertIn('Source changed', stderr)
        self.assertFalse((self.output / 'Source.zip').exists())
        self.assertFalse((self.output / 'Xbox360').exists())
        report = json.loads((self.output / 'build-report.json').read_text(encoding='utf-8'))
        self.assertEqual(report['result'], 'failed')

    def test_changed_compiled_executable_is_rejected_before_creating_packages(self):
        compiled, assets = self.case / 'compiled', self.case / 'runtime-assets'
        compiled.mkdir()
        self.stage_compiler(None, None, None, compiled)
        self.stage_assets(None, assets)
        (compiled / 'default.xex').write_bytes(b'XEX2changed fixture after compiler record')
        self.output.mkdir()
        with self.assertRaisesRegex(builder.BuildError, 'differs'):
            builder.package_game(self.root, compiled, assets, self.output, ('console', 'xenia'), builder.source_identity(self.root))
        self.assertFalse((self.output / 'Xbox360').exists())
        self.assertFalse((self.output / 'Xenia').exists())

    def test_check_mode_does_not_import_assets_or_create_output(self):
        with patch.object(builder, 'check_environment') as readiness:
            status, _, _, importer, compiler = self.invoke(['--source', str(self.root), '--check', '--backend', 'native'])
        self.assertEqual(status, 0)
        readiness.assert_called_once()
        importer.assert_not_called()
        compiler.assert_not_called()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.cache.exists())


def main():
    global SCRATCH
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scratch', type=Path, default=DEFAULT_SCRATCH,
                        help='Dedicated parent for test-owned temporary fixtures; defaults inside the project work folder')
    args, unittest_args = parser.parse_known_args()
    SCRATCH = args.scratch.expanduser().resolve()
    if SCRATCH == SCRATCH.parent:
        parser.error('Choose a dedicated scratch directory, not a filesystem root')
    unittest.main(argv=[str(Path(__file__)), *unittest_args])


if __name__ == '__main__':
    main()