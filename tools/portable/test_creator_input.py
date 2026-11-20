# SPDX-License-Identifier: GPL-3.0-only
"""Local-input regressions using mocked widgets; no native UI or child process is run."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import queue
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import Mock, patch
import tkinter as tk

import create_game_gui as gui

SCRATCH = Path(__file__).resolve().parents[2]/'work/creator-input-tests'

class Value:
    def __init__(self,value=''):self.value=value
    def get(self):return self.value
    def set(self,value):self.value=value

class InputChecks(unittest.TestCase):
    def setUp(self):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='local-input-',dir=SCRATCH)
        self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name)
        self.archive=self.base/'XP files.7z';self.archive.write_bytes(b'synthetic file for path checks')
        self.folder=self.base/'XP folder';self.folder.mkdir()

    def window(self,text=''):
        return SimpleNamespace(root=Mock(),asset_path=Value(text),input_kind=Value('download'),
                               input_changed=Mock(),check_environment=Mock(),local_entry=Mock())

    def test_copy_as_path_quotes_and_surrounding_spaces_are_normalized(self):
        self.assertEqual(gui.normalize_input_path('  "'+str(self.archive)+'"  '),self.archive)
        self.assertEqual(gui.normalize_input_path("'"+str(self.archive)+"'"),self.archive)
        self.assertEqual(gui.normalize_input_path(str(self.archive)),self.archive)

    def test_environment_and_home_paths_expand_without_shell_execution(self):
        with patch.dict(os.environ,{'PINBALL_INPUT_TEST':str(self.base)}):
            self.assertEqual(gui.normalize_input_path('$PINBALL_INPUT_TEST/XP files.7z'),self.archive)
        self.assertEqual(gui.normalize_input_path('~/XP files.7z'),Path.home()/'XP files.7z')

    def test_blank_multiline_and_nul_are_rejected(self):
        for value in ('','   ','""',"''",'one\ntwo','one\rtwo','one\0two'):
            with self.subTest(value=repr(value)),self.assertRaises(ValueError):
                gui.normalize_input_path(value)
        for value in (None,[],123):
            with self.subTest(value=value),self.assertRaises((TypeError,ValueError)):
                gui.normalize_input_path(value)

    def test_paste_normalizes_whole_path_selects_local_mode_and_focuses_entry(self):
        window=self.window('previous')
        window.root.clipboard_get.return_value='"'+str(self.archive)+'"'
        result=gui.CreatorWindow.paste_asset_path(window)
        self.assertEqual(result,'break')
        self.assertEqual(window.asset_path.get(),str(self.archive))
        self.assertEqual(window.input_kind.get(),'local')
        window.input_changed.assert_called_once()
        window.local_entry.focus_set.assert_called_once()
        window.local_entry.selection_range.assert_called_once_with(0,tk.END)

    def test_paste_event_is_consumed_to_prevent_duplicate_default_insertion(self):
        window=self.window()
        window.root.clipboard_get.return_value=str(self.archive)
        self.assertEqual(gui.CreatorWindow.paste_asset_path(window,object()),'break')
        self.assertEqual(window.asset_path.get(),str(self.archive))

    def test_empty_clipboard_error_preserves_prior_value(self):
        window=self.window('keep this path');window.root.clipboard_get.side_effect=tk.TclError('clipboard empty')
        with patch.object(gui.messagebox,'showerror') as error:
            self.assertEqual(gui.CreatorWindow.paste_asset_path(window),'break')
        self.assertEqual(window.asset_path.get(),'keep this path')
        window.input_changed.assert_not_called();error.assert_called_once()

    def test_invalid_clipboard_path_preserves_prior_value(self):
        window=self.window('keep this path');window.root.clipboard_get.return_value='path1\npath2'
        with patch.object(gui.messagebox,'showerror') as error:
            self.assertEqual(gui.CreatorWindow.paste_asset_path(window),'break')
        self.assertEqual(window.asset_path.get(),'keep this path')
        window.input_changed.assert_not_called();error.assert_called_once()

    def test_archive_button_opens_file_picker_with_zip_and_7z_filters(self):
        window=self.window()
        with patch.object(gui.filedialog,'askopenfilename',return_value=str(self.archive)) as picker,patch.object(gui.filedialog,'askdirectory') as folder:
            gui.CreatorWindow.browse_assets_archive(window)
        filters=picker.call_args.kwargs['filetypes']
        patterns=' '.join(str(pattern) for _,pattern in filters)
        self.assertIn('*.zip',patterns);self.assertIn('*.7z',patterns)
        folder.assert_not_called()
        self.assertEqual(window.asset_path.get(),str(self.archive));self.assertEqual(window.input_kind.get(),'local')
        window.input_changed.assert_called_once()

    def test_cancelled_archive_picker_preserves_path_and_mode(self):
        window=self.window('keep this path')
        with patch.object(gui.filedialog,'askopenfilename',return_value=''):
            gui.CreatorWindow.browse_assets_archive(window)
        self.assertEqual(window.asset_path.get(),'keep this path')
        self.assertEqual(window.input_kind.get(),'download');window.input_changed.assert_not_called()

    def test_folder_selection_remains_a_separate_directory_picker(self):
        window=self.window()
        with patch.object(gui.filedialog,'askdirectory',return_value=str(self.folder)) as folder,patch.object(gui.filedialog,'askopenfilename') as archive:
            gui.CreatorWindow.browse_assets_folder(window)
        folder.assert_called_once();archive.assert_not_called()
        self.assertEqual(window.asset_path.get(),str(self.folder));self.assertEqual(window.input_kind.get(),'local')
        window.input_changed.assert_called_once()

    def test_cancelled_folder_picker_preserves_path(self):
        window=self.window('keep this path')
        with patch.object(gui.filedialog,'askdirectory',return_value=''):
            gui.CreatorWindow.browse_assets_folder(window)
        self.assertEqual(window.asset_path.get(),'keep this path');window.input_changed.assert_not_called()

    def test_create_command_receives_normalized_manually_pasted_archive(self):
        window=self.window('  "'+str(self.archive)+'"  ')
        window.input_kind.set('local')
        window.busy=False;window.checking=False
        window.plan=SimpleNamespace(ready=True,command=Mock(return_value=['synthetic child never started']),cache_root=self.base/'cache')
        window.source_root=self.base/'source';window.destination=Value(str(self.base/'destination'))
        window.append_log=Mock();window.events=queue.Queue()
        for name in ('build_button','destination_entry','browse_button','download_radio','local_radio',
                     'local_entry','local_folder_button','local_archive_button','local_paste_button',
                     'open_button','retry_button','setup_button','status','description','progress'):
            setattr(window,name,Mock())
        with patch.object(gui.threading,'Thread') as thread,patch.object(gui.messagebox,'showerror') as error:
            gui.CreatorWindow.start_build(window)
        error.assert_not_called();thread.return_value.start.assert_called_once()
        self.assertFalse(window.plan.command.call_args.kwargs['download'])
        self.assertEqual(window.plan.command.call_args.kwargs['assets'],self.archive)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--scratch',type=Path)
    args,rest=parser.parse_known_args()
    if args.scratch:SCRATCH=args.scratch.resolve()
    unittest.main(argv=[str(Path(__file__)),*rest],verbosity=2)
