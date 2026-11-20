#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Windows Python-only game creator: download assets and assemble the verified port."""
from __future__ import annotations
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from tkinter.scrolledtext import ScrolledText
import webbrowser

from dataclasses import dataclass
import datetime as dt
import platform
import struct
import uuid
from create_game import verify_payload

@dataclass(frozen=True)
class CreatorPlan:
    cache_root: Path
    ready: bool = True
    message: str = ''
    setup_url: str | None = None

    def command(self, source_root, output, *, python=None, download=True, assets=None):
        if not self.ready:
            raise ValueError(self.message)
        command = [python or sys.executable, '-u', '-B', str(Path(source_root)/'tools/portable/create_game.py'),
                   '--source', str(source_root), '--output', str(output), '--cache', str(self.cache_root)]
        if not download:
            if assets is None:raise ValueError('Choose your original XP files.')
            command += ['--assets', str(assets)]
        return command

def discover_environment(source_root, emit=None, cache_root=None, require_archive=True):
    cache = Path(cache_root).expanduser().resolve() if cache_root else Path(os.environ.get('LOCALAPPDATA') or Path.home()/'AppData/Local')/'PinballPortCreator'
    if platform.system() != 'Windows' or struct.calcsize('P') != 8:
        return CreatorPlan(cache,ready=False,message='Open this Windows creator with 64-bit Python 3.10 or newer.')
    try:
        verify_payload(Path(source_root))
    except Exception as error:
        return CreatorPlan(cache,ready=False,message=str(error))
    return CreatorPlan(cache,message='The included game and archive reader are verified. Python is all you need installed.')

def new_output_directory(destination):
    return Path(destination).expanduser().resolve()/('Pinball-'+dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6])

SOURCE_ROOT = Path(__file__).resolve().parents[2]


def normalize_input_path(value: str) -> Path:
    """Accept a pasted Windows Copy-as-path value without its wrapping quotes."""
    if not isinstance(value,str):
        raise ValueError('Paste one file or folder path.')
    text=value.strip()
    if len(text)>=2 and text[0]==text[-1] and text[0] in ('"', "'"):
        text=text[1:-1].strip()
    if not text or any(char in text for char in ('\r','\n','\0')):
        raise ValueError('Paste one ZIP/7z file path or game folder path.')
    expanded=os.path.expanduser(os.path.expandvars(text))
    if not expanded or any(char in expanded for char in ('\r','\n','\0')):
        raise ValueError('Paste one file or folder path.')
    return Path(expanded)


def console_python():
    executable = Path(sys.executable)
    if os.name == 'nt' and executable.name.lower() == 'pythonw.exe':
        sibling = executable.with_name('python.exe')
        if sibling.is_file():
            return str(sibling)
    return str(executable)


def friendly_stage(line):
    if 'Downloading or checking' in line:
        return 'Getting the original XP files…'
    if 'Checking and extracting' in line:
        return 'Verifying and extracting game assets…'
    if 'Creating the Xbox' in line:
        return 'Creating both game folders…'
    return None


class CreatorWindow:
    def __init__(self, root, source_root=SOURCE_ROOT):
        self.root, self.source_root = root, Path(source_root).resolve()
        self.events = queue.Queue()
        self.plan = None
        self.busy = False
        self.checking = False
        self.discovery_generation = 0
        self.process = None
        self.finished_output = None
        self.details_visible = False
        self.session_log = None
        self.cache_override = os.environ.get('PINBALL_BUILDER_CACHE')
        output = os.environ.get('PINBALL_BUILDER_OUTPUT') or str(Path.home()/'Downloads/Pinball Port Games')
        self.destination = tk.StringVar(value=output)
        self.input_kind = tk.StringVar(value='download')
        self.asset_path = tk.StringVar(value='')
        root.title('Pinball Port Creator — r2')
        root.geometry('740x600')
        root.minsize(690,570)
        root.configure(background='#f3f5f9')
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.style = ttk.Style(root)
        if 'clam' in self.style.theme_names():
            self.style.theme_use('clam')
        self.style.configure('TFrame',background='#f3f5f9')
        self.style.configure('TLabel',background='#f3f5f9',foreground='#19243a',font=('Segoe UI',10))
        self.style.configure('Title.TLabel',font=('Segoe UI',24,'bold'))
        self.style.configure('Muted.TLabel',foreground='#536078')
        self.style.configure('Status.TLabel',font=('Segoe UI',12,'bold'))
        self.style.configure('TButton',font=('Segoe UI',10),padding=(12,8))
        self.style.configure('Primary.TButton',font=('Segoe UI',12,'bold'),padding=(22,13),
                             background='#285fd5',foreground='white')
        self.style.map('Primary.TButton',background=[('active','#174eC2'),('disabled','#bec8db')],
                       foreground=[('disabled','#f7f8fa')])
        self.style.configure('TRadiobutton',background='#f3f5f9',font=('Segoe UI',10),padding=(0,5))
        self.style.configure('TEntry',padding=7,font=('Segoe UI',10))
        frame = ttk.Frame(root,padding=(30,24))
        frame.pack(fill='both',expand=True)
        frame.columnconfigure(0,weight=1)
        ttk.Label(frame,text='Pinball Port Creator',style='Title.TLabel').grid(row=0,column=0,sticky='w')
        ttk.Label(frame,text='Download the files. Create your game.',style='Muted.TLabel').grid(row=1,column=0,sticky='w',pady=(4,24))
        ttk.Label(frame,text='Save the finished game to').grid(row=2,column=0,sticky='w',pady=(0,6))
        destination_row = ttk.Frame(frame)
        destination_row.grid(row=3,column=0,sticky='ew')
        destination_row.columnconfigure(0,weight=1)
        self.destination_entry = ttk.Entry(destination_row,textvariable=self.destination)
        self.destination_entry.grid(row=0,column=0,sticky='ew',padx=(0,10))
        self.browse_button = ttk.Button(destination_row,text='Browse…',command=self.browse_output)
        self.browse_button.grid(row=0,column=1)
        ttk.Label(frame,text='Original game files').grid(row=4,column=0,sticky='w',pady=(20,2))
        self.download_radio = ttk.Radiobutton(frame,text='Download from Internet Archive',variable=self.input_kind,
                                               value='download',command=self.input_changed)
        self.download_radio.grid(row=5,column=0,sticky='w')
        self.local_radio = ttk.Radiobutton(frame,text='Use files I already have',variable=self.input_kind,
                                            value='local',command=self.input_changed)
        self.local_radio.grid(row=6,column=0,sticky='w')
        self.local_row = ttk.Frame(frame)
        self.local_row.columnconfigure(0,weight=1)
        input_row = ttk.Frame(self.local_row)
        input_row.grid(row=0,column=0,sticky='ew')
        input_row.columnconfigure(0,weight=1)
        self.local_entry = ttk.Entry(input_row,textvariable=self.asset_path,state='normal')
        self.local_entry.grid(row=0,column=0,sticky='ew',padx=(0,8))
        self.local_paste_button = ttk.Button(input_row,text='Paste path',command=self.paste_asset_path)
        self.local_paste_button.grid(row=0,column=1)
        picker_row = ttk.Frame(self.local_row)
        picker_row.grid(row=1,column=0,sticky='w',pady=(7,0))
        self.local_archive_button = ttk.Button(picker_row,text='Choose ZIP / 7z…',command=self.browse_assets_archive)
        self.local_archive_button.grid(row=0,column=0,padx=(0,8))
        self.local_folder_button = ttk.Button(picker_row,text='Choose folder…',command=self.browse_assets_folder)
        self.local_folder_button.grid(row=0,column=1)
        self.local_entry.bind('<Control-v>',self.paste_asset_path)
        self.local_entry.bind('<Control-V>',self.paste_asset_path)
        self.local_entry.bind('<Shift-Insert>',self.paste_asset_path)
        self.local_entry.bind('<Control-a>',self.select_asset_path)
        self.local_entry.bind('<Control-A>',self.select_asset_path)
        self.local_menu = tk.Menu(self.local_entry,tearoff=False)
        self.local_menu.add_command(label='Cut',command=lambda:self.local_entry.event_generate('<<Cut>>'))
        self.local_menu.add_command(label='Copy',command=lambda:self.local_entry.event_generate('<<Copy>>'))
        self.local_menu.add_command(label='Paste path',command=self.paste_asset_path)
        self.local_menu.add_separator()
        self.local_menu.add_command(label='Select all',command=self.select_asset_path)
        self.local_entry.bind('<Button-3>',self.show_asset_menu)
        self.status = ttk.Label(frame,text='Checking the included files…',style='Status.TLabel',wraplength=660)
        self.status.grid(row=8,column=0,sticky='w',pady=(22,5))
        self.description = ttk.Label(frame,text='Verifying the included game and archive reader.',style='Muted.TLabel',wraplength=660)
        self.description.grid(row=9,column=0,sticky='w')
        self.progress = ttk.Progressbar(frame,mode='indeterminate')
        self.progress.grid(row=10,column=0,sticky='ew',pady=(14,16))
        actions = ttk.Frame(frame)
        actions.grid(row=11,column=0,sticky='ew')
        actions.columnconfigure(0,weight=1)
        self.build_button = ttk.Button(actions,text='Create Game',style='Primary.TButton',command=self.start_build,state='disabled')
        self.build_button.grid(row=0,column=0,sticky='w')
        self.setup_button = ttk.Button(actions,text='Check creator files',command=self.open_setup)
        self.retry_button = ttk.Button(actions,text='Check again',command=self.check_environment)
        self.open_button = ttk.Button(actions,text='Open game folder',command=self.open_output)
        self.details_button = ttk.Button(actions,text='Show details',command=self.toggle_details)
        self.details_button.grid(row=0,column=2,sticky='e')
        self.details = ScrolledText(frame,height=11,wrap='word',font=('Consolas',9),state='disabled',
                                   background='white',foreground='#26334c',borderwidth=1,relief='solid')
        ttk.Label(frame,text='GPLv3 source included. Original XP assets keep their separate rights.',
                  style='Muted.TLabel',wraplength=660).grid(row=13,column=0,sticky='w',pady=(20,0))
        self.root.after(80,self.poll_events)
        self.root.after(120,self.check_environment)

    def browse_output(self):
        selected = filedialog.askdirectory(parent=self.root,title='Where should the game be saved?')
        if selected:
            self.destination.set(selected)

    def select_asset_path(self,event=None):
        self.local_entry.focus_set()
        self.local_entry.selection_range(0,tk.END)
        return 'break'

    def show_asset_menu(self,event):
        try:
            self.local_menu.tk_popup(event.x_root,event.y_root)
        finally:
            self.local_menu.grab_release()
        return 'break'

    def paste_asset_path(self,event=None):
        try:
            path=normalize_input_path(self.root.clipboard_get())
        except (tk.TclError,ValueError,OSError) as error:
            messagebox.showerror('Paste a file path',str(error),parent=self.root)
            return 'break'
        self.asset_path.set(str(path))
        self.input_kind.set('local')
        self.input_changed()
        self.local_entry.focus_set()
        self.local_entry.selection_range(0,tk.END)
        return 'break'

    def browse_assets_folder(self):
        selected = filedialog.askdirectory(parent=self.root,title='Choose the extracted XP Pinball folder')
        if selected:
            self.asset_path.set(selected)
            self.input_kind.set('local')
            self.input_changed()

    def browse_assets_archive(self):
        selected = filedialog.askopenfilename(parent=self.root,title='Choose a ZIP or 7z file',
            filetypes=[('ZIP and 7z archives','*.zip *.7z'),('ZIP archives','*.zip'),('7z archives','*.7z'),('All files','*')])
        if selected:
            self.asset_path.set(selected)
            self.input_kind.set('local')
            self.input_changed()

    def input_changed(self):
        if self.input_kind.get() == 'local':
            self.local_row.grid(row=7,column=0,sticky='ew',pady=(6,0))
            if not self.details_visible:self.root.geometry('740x680')
            self.build_button.configure(text='Create Game')
        else:
            self.local_row.grid_remove()
            if not self.details_visible:self.root.geometry('740x600')
            self.build_button.configure(text='Create Game')
        self.check_environment()

    def check_environment(self):
        if self.busy:
            return
        self.discovery_generation += 1
        generation = self.discovery_generation
        require_archive = self.input_kind.get() == 'download' or Path(self.asset_path.get()).suffix.lower() == '.7z'
        self.checking = True
        self.build_button.configure(state='disabled')
        self.setup_button.grid_remove(); self.retry_button.grid_remove(); self.open_button.grid_remove()
        self.status.configure(text='Checking the included files…')
        self.description.configure(text='Verifying the included game and archive reader.')
        self.progress.start(12)
        def worker():
            try:
                plan = discover_environment(self.source_root,
                    cache_root=Path(self.cache_override).expanduser() if self.cache_override else None,
                    require_archive=require_archive)
                self.events.put(('discovery',generation,plan))
            except Exception as error:
                self.events.put(('discovery_error',generation,str(error)))
        threading.Thread(target=worker,daemon=True).start()

    def append_log(self,text):
        self.details.configure(state='normal')
        self.details.insert('end',text)
        if int(self.details.index('end-1c').split('.')[0]) > 2500:
            self.details.delete('1.0','500.0')
        self.details.see('end')
        self.details.configure(state='disabled')

    def start_build(self):
        if self.busy or self.checking or self.plan is None or not self.plan.ready:
            return
        try:
            destination = normalize_input_path(self.destination.get()).resolve()
            if not str(self.destination.get()).strip() or not destination.is_absolute():
                raise ValueError('Choose an absolute destination folder using Browse.')
            selected_assets = normalize_input_path(self.asset_path.get()).resolve() if self.input_kind.get() == 'local' else None
            if selected_assets is not None and not selected_assets.exists():
                raise ValueError('Choose an existing ZIP/7z file or extracted XP game folder.')
            if selected_assets is not None:self.asset_path.set(str(selected_assets))
            output = new_output_directory(destination)
            command = self.plan.command(self.source_root,output,python=console_python(),
                                       download=self.input_kind.get() == 'download',
                                       assets=selected_assets)
        except (OSError,ValueError) as error:
            messagebox.showerror('Choose the game files and destination',str(error),parent=self.root)
            return
        self.busy = True
        self.finished_output = None
        self.build_button.configure(state='disabled')
        for widget in (self.destination_entry,self.browse_button,self.download_radio,self.local_radio,
                       self.local_entry,self.local_paste_button,self.local_folder_button,self.local_archive_button):
            widget.configure(state='disabled')
        self.open_button.grid_remove();self.retry_button.grid_remove();self.setup_button.grid_remove()
        self.status.configure(text='Preparing the original game files…' if self.input_kind.get() == 'download' else 'Checking game files…')
        self.description.configure(text='The game and archive reader are included. Downloading and verifying the XP files is automatic.')
        self.progress.start(12)
        self.append_log('Creating a new game.\nDestination: '+str(output)+'\n')
        def worker():
            try:
                logs = self.plan.cache_root/'logs'
                logs.mkdir(parents=True,exist_ok=True)
                log_path = logs/(output.name+'.log')
                env = {**os.environ,'PYTHONUNBUFFERED':'1','PYTHONIOENCODING':'utf-8','PYTHONDONTWRITEBYTECODE':'1'}
                kwargs = {'stdout':subprocess.PIPE,'stderr':subprocess.STDOUT,'stdin':subprocess.DEVNULL,
                          'text':True,'encoding':'utf-8','errors':'replace','env':env,'bufsize':1}
                if os.name == 'nt':
                    kwargs['creationflags'] = getattr(subprocess,'CREATE_NO_WINDOW',0)
                with log_path.open('x',encoding='utf-8') as log:
                    process = subprocess.Popen(command,**kwargs)
                    self.process = process
                    for line in process.stdout:
                        log.write(line);log.flush()
                        self.events.put(('line',line))
                    result = process.wait()
                self.process = None
                report = output/'creation-report.json'
                data = json.loads(report.read_text(encoding='utf-8')) if report.is_file() else {}
                if result == 0 and data.get('result') == 'complete':
                    self.events.put(('complete',output,log_path))
                else:
                    self.events.put(('failed',data.get('error') or 'The game could not be created. Open details for the reason.',output,log_path))
            except Exception as error:
                self.process = None
                self.events.put(('failed',str(error),output,None))
        # Keep the reader alive until the owned file-preparation process exits.
        threading.Thread(target=worker,daemon=False).start()

    def finish_controls(self):
        self.busy = False
        self.progress.stop()
        for widget in (self.destination_entry,self.browse_button,self.download_radio,self.local_radio,
                       self.local_entry,self.local_paste_button,self.local_folder_button,self.local_archive_button):
            widget.configure(state='normal')
        self.build_button.configure(state='normal' if self.plan and self.plan.ready else 'disabled')

    def poll_events(self):
        try:
            for _ in range(200):
                item = self.events.get_nowait()
                kind = item[0]
                if kind == 'line':
                    self.append_log(item[1])
                    stage = friendly_stage(item[1].strip())
                    if stage:
                        self.status.configure(text=stage)
                elif kind == 'discovery' and item[1] == self.discovery_generation:
                    self.plan = item[2]
                    self.checking = False
                    self.progress.stop()
                    if self.plan.ready:
                        self.build_button.grid(row=0,column=0,sticky='w')
                        self.status.configure(text='Ready to create your game')
                        self.description.configure(text='Choose where to save the game, then click Create Game. Python is the only installed requirement.')
                        self.build_button.configure(state='normal')
                        self.append_log(self.plan.message+'\n')
                    else:
                        self.status.configure(text='Please check the creator files')
                        self.description.configure(text=self.plan.message)
                        if self.plan.setup_url:
                            self.setup_button.grid(row=0,column=0,sticky='w')
                        self.build_button.grid_remove()
                        self.retry_button.grid(row=0,column=1,padx=10)
                elif kind == 'discovery_error' and item[1] == self.discovery_generation:
                    self.checking = False;self.progress.stop()
                    self.status.configure(text='Could not check the included files')
                    self.description.configure(text='Open details for the reason, then try Check again.')
                    self.append_log(item[2]+'\n')
                    self.retry_button.grid(row=0,column=1,padx=10)
                elif kind == 'complete':
                    self.finish_controls();self.finished_output = item[1]
                    self.status.configure(text='Your game is ready')
                    self.description.configure(text='The Xbox360 and Xenia folders contain the finished games. Matching GPLv3 source is included.')
                    self.open_button.grid(row=0,column=1,padx=10)
                    self.build_button.configure(text='Create Another')
                    self.append_log('Game created.\nDetails log: '+str(item[2])+'\n')
                elif kind == 'failed':
                    self.finish_controls()
                    self.status.configure(text='The game could not be created')
                    self.description.configure(text='Your files are preserved. Open details to see what happened, then try again.')
                    self.append_log('\nCreation stopped: '+item[1]+'\n')
                    if item[3]:self.append_log('Details log: '+str(item[3])+'\n')
                    if not self.details_visible:self.toggle_details()
        except queue.Empty:
            pass
        self.root.after(80,self.poll_events)

    def toggle_details(self):
        self.details_visible = not self.details_visible
        if self.details_visible:
            self.details.grid(row=12,column=0,sticky='nsew',pady=(18,0))
            self.root.geometry('740x880' if self.input_kind.get() == 'local' else '740x800')
            self.details_button.configure(text='Hide details')
        else:
            self.details.grid_remove()
            self.root.geometry('740x680' if self.input_kind.get() == 'local' else '740x600')
            self.details_button.configure(text='Show details')

    def open_setup(self):
        if self.plan and self.plan.setup_url:
            webbrowser.open(self.plan.setup_url)

    def open_output(self):
        if not self.finished_output or not self.finished_output.is_dir():
            return
        if os.name == 'nt':
            os.startfile(str(self.finished_output))
        elif sys.platform == 'darwin':
            subprocess.Popen(['open',str(self.finished_output)])
        else:
            subprocess.Popen(['xdg-open',str(self.finished_output)])

    def close(self):
        if self.busy:
            messagebox.showinfo('Creating game','The game files are still being prepared. You can minimize this window and close it when it finishes.',parent=self.root)
            return
        self.root.destroy()


def main():
    root = tk.Tk()
    CreatorWindow(root)
    root.mainloop()

if __name__ == '__main__':
    main()
