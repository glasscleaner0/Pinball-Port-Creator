# SPDX-License-Identifier: GPL-3.0-only
"""Tests for public GUI discovery; no engine, WSL or installation is launched."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock
import gui_backend as backend

SCRATCH = Path(__file__).resolve().parents[2]/'work/public-gui-backend-tests'

class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='public-gui-',dir=SCRATCH)
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base/'source'; (self.root/'tools/portable').mkdir(parents=True)
        (self.root/'CMakeLists.txt').write_text('project(Fixture)\n')
        (self.root/'tools/portable/builder.py').write_text('# fixture\n')
        self.cache = self.base/'cache'

    def discover(self, **kwargs):
        return backend.discover_environment(self.root,cache_root=self.cache,**kwargs)

    def test_standard_cache_locations(self):
        home = self.base/'home'
        with patch.object(backend.Path,'home',return_value=home):
            with patch.object(backend.platform,'system',return_value='Windows'), patch.dict(os.environ,{'LOCALAPPDATA':str(self.base/'local')},clear=True):
                self.assertEqual(backend.default_cache_root(),self.base/'local/PinballPortBuilder')
            with patch.object(backend.platform,'system',return_value='Darwin'):
                self.assertEqual(backend.default_cache_root(),home/'Library/Caches/PinballPortBuilder')
            with patch.object(backend.platform,'system',return_value='Linux'), patch.dict(os.environ,{'XDG_CACHE_HOME':str(self.base/'xdg')},clear=True):
                self.assertEqual(backend.default_cache_root(),self.base/'xdg/pinball-port-builder')
            with patch.object(backend.platform,'system',return_value='Linux'), patch.dict(os.environ,{'XDG_CACHE_HOME':'relative'},clear=True):
                self.assertEqual(backend.default_cache_root(),home/'.cache/pinball-port-builder')
        self.assertFalse(home.exists())

    def test_wsl_utf16_and_normal_output_decode(self):
        value = 'Ubuntu\r\nDebian 日本語\r\n'
        self.assertEqual(backend._decode_output(value.encode('utf-16-le')),value)
        self.assertEqual(backend._decode_output(value.encode('utf-16')),value)
        self.assertEqual(backend._decode_output(b'Docker version 28.0'), 'Docker version 28.0')

    def test_public_discovery_prefers_working_container_without_host_7zip(self):
        with patch.object(backend,'_engine_ready',side_effect=['Docker version 28.0',None]) as engines, patch.object(backend,'_host_archive_tool',return_value=None):
            plan = self.discover()
        self.assertTrue(plan.ready);self.assertEqual(plan.backend,'docker')
        self.assertEqual(plan.archive_tool,'container');self.assertEqual(plan.cache_root,self.cache)
        engines.assert_called_once_with('docker')
        self.assertFalse(self.cache.exists())

    def test_unhealthy_docker_can_use_podman(self):
        with patch.object(backend,'_engine_ready',side_effect=[None,'podman version 5.0']), patch.object(backend,'_host_archive_tool',return_value=None):
            plan = self.discover()
        self.assertEqual(plan.backend,'podman');self.assertTrue(plan.ready)

    def test_engine_requires_linux_and_successful_version_check(self):
        with patch.object(backend.shutil,'which',return_value='/bin/docker'):
            with patch.object(backend,'_run_text',return_value='windows'):
                self.assertIsNone(backend._engine_ready('docker'))
            with patch.object(backend,'_run_text',side_effect=['linux','Docker version 28.0']):
                self.assertEqual(backend._engine_ready('docker'),'Docker version 28.0')
            with patch.object(backend,'_run_text',side_effect=subprocess.TimeoutExpired('docker',8)):
                self.assertIsNone(backend._engine_ready('docker'))

    def test_installed_wsl_uses_any_ready_distribution_not_a_project_name(self):
        with patch.object(backend.platform,'system',return_value='Windows'), patch.object(backend,'_engine_ready',return_value=None), patch.object(backend,'_host_archive_tool',return_value='/host/7z'), patch.object(backend,'_wsl_distributions',return_value=['Minimal','Custom Debian']), patch.object(backend,'_wsl_ready',side_effect=[(False,None),(True,None)]) as readiness:
            plan = self.discover()
        self.assertTrue(plan.ready);self.assertEqual(plan.backend,'wsl');self.assertEqual(plan.distribution,'Custom Debian')
        self.assertEqual(readiness.call_args_list[1].args,('Custom Debian',False))

    def test_wsl_archive_check_only_when_host_tool_is_missing(self):
        with patch.object(backend.platform,'system',return_value='Windows'), patch.object(backend,'_engine_ready',return_value=None), patch.object(backend,'_host_archive_tool',return_value=None), patch.object(backend,'_wsl_distributions',return_value=['Linux']), patch.object(backend,'_wsl_ready',return_value=(True,'/usr/bin/7z')) as readiness:
            plan = self.discover()
        readiness.assert_called_once_with('Linux',True)
        self.assertEqual(plan.archive_tool,'/usr/bin/7z')

    def test_missing_public_environment_returns_setup_link_and_creates_nothing(self):
        with patch.object(backend.platform,'system',return_value='Darwin'), patch.object(backend,'_engine_ready',return_value=None), patch.object(backend,'_host_archive_tool',return_value=None):
            plan = self.discover()
        self.assertFalse(plan.ready);self.assertTrue(plan.needs_setup)
        self.assertEqual(plan.setup_url,backend.DOCKER_SETUP_URL)
        self.assertFalse(self.cache.exists())

    def test_native_folder_mode_does_not_require_an_archive_tool(self):
        with patch.object(backend.platform,'system',return_value='Linux'), patch.object(backend,'_engine_ready',return_value=None), patch.object(backend,'_host_archive_tool',return_value=None), patch.object(backend,'_native_ready',return_value=(True,'ready')):
            plan = self.discover(require_archive=False)
        self.assertTrue(plan.ready);self.assertEqual(plan.backend,'native')

    def test_native_download_with_missing_7zip_has_clear_setup(self):
        with patch.object(backend.platform,'system',return_value='Linux'), patch.object(backend,'_engine_ready',return_value=None), patch.object(backend,'_host_archive_tool',return_value=None), patch.object(backend,'_native_ready',return_value=(True,'ready')):
            plan = self.discover()
        self.assertFalse(plan.ready);self.assertEqual(plan.setup_url,backend.SEVEN_ZIP_SETUP_URL)

    def test_invalid_source_returns_readable_failure_without_discovery(self):
        with patch.object(backend,'_engine_ready') as engine:
            plan = backend.discover_environment(self.base/'missing',cache_root=self.cache)
        self.assertFalse(plan.ready);self.assertIsNone(plan.setup_url);engine.assert_not_called()

    def test_build_command_uses_only_public_cache_and_picks_both_targets(self):
        plan = backend.BuildPlan('wsl',self.cache,distribution='Custom Linux')
        command = plan.command(self.root,self.base/'result',python='python-fixture')
        self.assertEqual(command[0],'python-fixture')
        self.assertIn('--download-xp',command);self.assertIn('both',command)
        self.assertEqual(command[command.index('--cache')+1],str((self.cache/'native').resolve()))
        self.assertEqual(command[command.index('--wsl-distribution')+1],'Custom Linux')
        self.assertNotIn('--toolchain',command)
        self.assertNotIn('PinballPortBuild',' '.join(command))
        self.assertFalse((self.base/'result').exists())

    def test_only_an_existing_app_cache_compiler_is_reused(self):
        external = self.root/'work/native/powerpc-xsdk/bin';external.mkdir(parents=True)
        (external/'powerpc-xsdk-elf-g++').write_text('external fixture')
        plan = backend.BuildPlan('native',self.cache)
        self.assertNotIn('--toolchain',plan.command(self.root,self.base/'out'))
        own = self.cache/'native/powerpc-xsdk/bin';own.mkdir(parents=True)
        (own/'powerpc-xsdk-elf-g++').write_text('cached fixture')
        command = plan.command(self.root,self.base/'out')
        self.assertEqual(command[command.index('--toolchain')+1],str(own.parent.resolve()))

    def test_local_assets_are_mutually_exclusive_with_download(self):
        plan = backend.BuildPlan('native',self.cache)
        command = plan.command(self.root,self.base/'out',download=False,assets=self.base/'xp.zip')
        self.assertIn('--assets',command);self.assertNotIn('--download-xp',command)
        with self.assertRaises(ValueError):plan.command(self.root,self.base/'out',download=False)
        with self.assertRaises(ValueError):plan.command(self.root,self.base/'out',assets=self.base/'xp.zip')
        with self.assertRaises(RuntimeError):backend.BuildPlan(None,self.cache,ready=False,message='Set up tools').command(self.root,self.base/'out')

    def test_unique_destinations_are_not_created_during_planning(self):
        first=backend.new_output_directory(self.base/'games');second=backend.new_output_directory(self.base/'games')
        self.assertNotEqual(first,second);self.assertEqual(first.parent,self.base/'games')
        self.assertFalse(first.parent.exists())

    def test_wsl_query_checks_versions_and_keeps_distribution_in_argv(self):
        with patch.object(backend,'_run_text',return_value='cmake version 3.25.1\n/usr/bin/7zz') as runner:
            ready,archive=backend._wsl_ready('My Linux ; untouched',True)
        self.assertTrue(ready);self.assertEqual(archive,'/usr/bin/7zz')
        args=runner.call_args.args[0]
        self.assertEqual(args[2],'My Linux ; untouched');self.assertNotIn('untouched',args[-1])
        self.assertIn('sys.version_info < (3,10)',args[-1])

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--scratch',type=Path)
    options,extra=parser.parse_known_args()
    if options.scratch:SCRATCH=options.scratch.resolve()
    unittest.main(argv=[str(Path(__file__)),*extra],verbosity=2)
