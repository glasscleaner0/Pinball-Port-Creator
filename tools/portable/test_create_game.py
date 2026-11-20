# SPDX-License-Identifier: GPL-3.0-only
"""Safety and package checks for the Python-only game creator; fixtures never run an EXE."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import builder
import create_game as creator

PROJECT=Path(__file__).resolve().parents[2]
SCRATCH=PROJECT/'work/simple-creator-tests'

class CreatorChecks(unittest.TestCase):
    def setUp(self):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(prefix='creator-',dir=SCRATCH)
        self.addCleanup(self.temporary.cleanup)
        self.base=Path(self.temporary.name);self.root=self.base/'tool';self.output=self.base/'created';self.cache=self.base/'cache'
        payload=self.root/'payload';payload.mkdir(parents=True)
        self.payload=payload
        (payload/'default.xex').write_bytes(b'XEX2synthetic-port-fixture-never-executed')
        game_pin=patch.object(creator,'ACCEPTED_XEX_SHA256',builder.digest(payload/'default.xex'))
        game_pin.start();self.addCleanup(game_pin.stop)
        (payload/'7zr.exe').write_bytes(b'MZ'+b'synthetic-test-helper-never-executed')
        helper_pin=patch.object(creator,'ARCHIVE_READER_SHA256',builder.digest(payload/'7zr.exe'))
        helper_pin.start();self.addCleanup(helper_pin.stop)
        with zipfile.ZipFile(payload/'GameSource.zip','x') as archive:archive.writestr('README.txt','Synthetic source fixture')
        for name in ('LICENSE','LICENSE-SCOPE.txt'):shutil.copyfile(PROJECT/name,self.root/name)
        portable=self.root/'tools/portable';portable.mkdir(parents=True)
        shutil.copytree(PROJECT/'tools/portable/runtime-notices',portable/'runtime-notices')
        shutil.copyfile(PROJECT/'tools/portable/runtime-notices.json',portable/'runtime-notices.json')
        self.manifest={'files':[{'path':name,'sha256':builder.digest(payload/name)} for name in ('default.xex','7zr.exe')],
                       'source_archive':'GameSource.zip','source_archive_sha256':builder.digest(payload/'GameSource.zip')}
        builder.save_json(payload/'PAYLOAD-MANIFEST.json',self.manifest)
        self.local=self.base/'xp';self.local.mkdir()
        self.prepare_patch=patch.object(creator.xp_assets,'prepare_assets',side_effect=self.prepare_fixture)
        self.importer=self.prepare_patch.start();self.addCleanup(self.prepare_patch.stop)
        self.download_patch=patch.object(creator.xp_assets,'download_archive',return_value=self.local)
        self.downloader=self.download_patch.start();self.addCleanup(self.download_patch.stop)

    def prepare_fixture(self,source,destination,**kwargs):
        destination.mkdir()
        (destination/'PINBALL.DAT').write_bytes(b'fake DAT for package logic')
        (destination/'PBMSG_FT.bin').write_bytes(b'fake font for package logic')
        builder.save_json(destination/'ASSET-PROVENANCE.json',{'synthetic_test':True})
        (destination/'SOUNDS').mkdir();(destination/'SOUNDS/SOUND1.WAV').write_bytes(b'fake waveform fixture')
        return {'synthetic_test':True,'original_executable_executed':False}

    def invoke(self,**kwargs):
        return creator.create_game(self.root,self.output,self.cache,emit=lambda text:None,**kwargs)

    def test_creates_both_correct_modes_same_accepted_game_and_matching_source(self):
        report=self.invoke(assets=self.local)
        self.assertEqual(report['result'],'complete');self.assertFalse(report['compiler_executed'])
        self.assertFalse(report['original_executable_executed']);self.downloader.assert_not_called()
        self.assertEqual(len(report['packages']),2)
        self.assertEqual(builder.digest(self.output/'Source.zip'),self.manifest['source_archive_sha256'])
        self.assertEqual(json.loads((self.output/'creation-report.json').read_text())['result'],'complete')
        for mode,name in [('console','Xbox360'),('xenia','Xenia')]:
            target=self.output/name
            self.assertEqual(builder.digest(target/'default.xex'),creator.ACCEPTED_XEX_SHA256)
            self.assertIn('present='+mode,(target/'pinball360.ini').read_text())
            self.assertIn('did not compile source',(target/'START-HERE.txt').read_text())
            self.assertFalse((target/'Pinball.exe').exists());self.assertFalse((target/'7zr.exe').exists())
            package=json.loads((target/'PACKAGE-MANIFEST.json').read_text())
            self.assertEqual(package['creation_method'],report['creation_method'])
            self.assertFalse(package['compiler_executed'])
            for item in package['files']:
                self.assertEqual(builder.digest(target/item['path']),item['sha256'])
                self.assertTrue(builder.fatx_path(item['path']))
            for line in (target/'SHA256SUMS.txt').read_text().splitlines():
                digest,name=line.split('  ',1);self.assertEqual(builder.digest(target/name),digest)

    def test_default_download_is_used_without_any_host_environment_lookup(self):
        report=self.invoke()
        self.assertTrue(report['download_selected'])
        self.downloader.assert_called_once_with(self.cache/'downloads')
        self.assertTrue(callable(self.importer.call_args.kwargs['sevenzip_runner']))

    def test_existing_output_and_report_are_preserved(self):
        self.output.mkdir();prior=self.output/'creation-report.json';prior.write_text('old accepted report')
        with self.assertRaises(creator.CreationError):self.invoke()
        self.assertEqual(prior.read_text(),'old accepted report')
        self.downloader.assert_not_called();self.importer.assert_not_called()

    def test_changed_game_is_rejected_even_if_manifest_is_updated(self):
        (self.payload/'default.xex').write_bytes(b'XEX2changed')
        self.manifest['files'][0]['sha256']=builder.digest(self.payload/'default.xex')
        builder.save_json(self.payload/'PAYLOAD-MANIFEST.json',self.manifest)
        with self.assertRaisesRegex(creator.CreationError,'verified port'):self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_changed_archive_reader_is_rejected_before_any_download(self):
        (self.payload/'7zr.exe').write_bytes(b'MZchanged')
        with self.assertRaisesRegex(creator.CreationError,'changed'):self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_changed_helper_is_rejected_even_if_manifest_is_updated(self):
        (self.payload/'7zr.exe').write_bytes(b'MZchanged but manifest updated')
        self.manifest['files'][1]['sha256']=builder.digest(self.payload/'7zr.exe')
        builder.save_json(self.payload/'PAYLOAD-MANIFEST.json',self.manifest)
        with self.assertRaisesRegex(creator.CreationError,'verified Windows'):
            self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_reparse_payload_and_source_are_rejected_before_download(self):
        for linked in (self.payload,self.payload/'GameSource.zip'):
            with self.subTest(linked=linked),patch.object(creator.xp_assets,'_is_link',side_effect=lambda path:Path(path)==linked):
                with self.assertRaises(creator.CreationError):self.invoke()
                self.assertFalse(self.output.exists())
        self.downloader.assert_not_called()

    def test_changed_source_is_rejected_before_any_download(self):
        (self.payload/'GameSource.zip').write_bytes(b'changed')
        with self.assertRaisesRegex(creator.CreationError,'source archive'):self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_manifest_path_escape_is_rejected(self):
        self.manifest['files'][1]['path']='../LICENSE'
        builder.save_json(self.payload/'PAYLOAD-MANIFEST.json',self.manifest)
        with self.assertRaisesRegex(creator.CreationError,'invalid filename'):self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_changed_runtime_notice_is_rejected_before_download(self):
        inventory=json.loads((self.root/'tools/portable/runtime-notices.json').read_text())
        (self.root/'tools/portable'/inventory['files'][0]['path']).write_text('changed notice')
        with self.assertRaisesRegex(creator.CreationError,'runtime notice'):self.invoke()
        self.assertFalse(self.output.exists());self.downloader.assert_not_called()

    def test_cache_output_source_and_payload_overlap_are_rejected(self):
        for output,cache in [(self.root,self.cache),(self.output,self.output/'cache'),(self.root/'payload/new',self.cache),
                             (self.output,self.root/'tools/portable/cache'),(self.local/'created',self.cache)]:
            with self.subTest(output=output,cache=cache):
                with self.assertRaises((creator.CreationError,builder.BuildError)):
                    creator.create_game(self.root,output,cache,assets=self.local,emit=lambda text:None)
        self.downloader.assert_not_called();self.importer.assert_not_called()

    def test_asset_failure_preserves_owned_diagnostic_report(self):
        self.importer.side_effect=creator.xp_assets.AssetError('wrong XP version')
        with self.assertRaisesRegex(creator.xp_assets.AssetError,'wrong XP'):self.invoke()
        report=json.loads((self.output/'creation-report.json').read_text())
        self.assertEqual(report['result'],'failed');self.assertEqual(report['error'],'wrong XP version')
        self.assertFalse((self.output/'Xbox360').exists())

    def test_reader_executes_only_bundled_verified_helper_and_preserves_binary_output(self):
        captured={}
        def capture(source,destination,**kwargs):
            captured['runner']=kwargs['sevenzip_runner']
            return self.prepare_fixture(source,destination,**kwargs)
        self.importer.side_effect=capture
        self.invoke(assets=self.local)
        expected=subprocess.CompletedProcess([],0,stdout=b'\x00\xffbinary',stderr=b'')
        with patch.object(creator.subprocess,'run',return_value=expected) as run:
            result=captured['runner'](['l','--','C:/fixture.7z'],timeout=99)
        self.assertEqual(result.stdout,b'\x00\xffbinary')
        args=run.call_args.args[0]
        self.assertEqual(args[0],str(self.payload/'7zr.exe'));self.assertEqual(args[1:],['l','--','C:/fixture.7z'])
        self.assertEqual(run.call_args.kwargs['timeout'],20)
        self.assertEqual(run.call_args.kwargs['stdin'],subprocess.DEVNULL)

    def test_reader_rechecks_helper_immediately_before_execution(self):
        captured={}
        def capture(source,destination,**kwargs):
            captured['runner']=kwargs['sevenzip_runner'];return self.prepare_fixture(source,destination,**kwargs)
        self.importer.side_effect=capture;self.invoke(assets=self.local)
        (self.payload/'7zr.exe').write_bytes(b'MZnow changed')
        with patch.object(creator.subprocess,'run') as run:
            with self.assertRaisesRegex(creator.CreationError,'nothing was executed'):
                captured['runner'](['l','--','fixture.7z'])
        run.assert_not_called()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--scratch',type=Path)
    args,extra=parser.parse_known_args()
    if args.scratch:SCRATCH=args.scratch.resolve()
    unittest.main(argv=[str(Path(__file__)),*extra],verbosity=2)
