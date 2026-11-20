#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create runnable game folders from the accepted port and separately supplied XP assets.

This is an asset assembler. It does not compile source or launch either game.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys

import builder
import xp_assets

VERSION = '0.0.15'
ACCEPTED_XEX_SHA256 = 'fc389d132b474eca8085ca91d62f0962a5e61758255c766be68745ed57ae8310'
ARCHIVE_READER_SHA256 = 'fe66e92b57b88a80585d50e194572b33edde4a4af8a0d33e83801f9fa37e8a4e'
DEFAULT_ROOT = Path(__file__).resolve().parents[2]

class CreationError(RuntimeError):
    pass


def _regular(path, root):
    path, root = Path(path), Path(root)
    if not path.is_file() or xp_assets._is_link(path) or not path.resolve().is_relative_to(root.resolve()):
        raise CreationError('A bundled file is missing, linked or outside the package: '+path.name)
    return path


def _payload_path(payload, relative):
    # The release needs only flat, precisely named payload files.
    if not isinstance(relative,str) or Path(relative).name != relative or relative in ('','.','..') or '/' in relative or '\\' in relative:
        raise CreationError('The bundled payload manifest contains an invalid filename.')
    return _regular(payload/relative,payload)


def verify_payload(root):
    root = Path(root).resolve(); payload = root/'payload'
    if not payload.is_dir() or xp_assets._is_link(payload):
        raise CreationError('Extract the complete game creator ZIP before starting it.')
    manifest_file = _regular(payload/'PAYLOAD-MANIFEST.json',payload)
    manifest = json.loads(manifest_file.read_text(encoding='utf-8-sig'))
    entries = manifest.get('files',[])
    if not isinstance(entries,list):raise CreationError('The bundled payload inventory is invalid.')
    indexed = {}
    for entry in entries:
        name = entry.get('path')
        if name in indexed:raise CreationError('The bundled payload inventory has duplicate entries.')
        path = _payload_path(payload,name)
        expected = entry.get('sha256')
        if not isinstance(expected,str) or len(expected)!=64 or builder.digest(path)!=expected:
            raise CreationError('A bundled file has changed: '+str(name))
        indexed[name] = (path,expected)
    if 'default.xex' not in indexed or '7zr.exe' not in indexed:
        raise CreationError('The bundled game or archive reader is missing.')
    if indexed['default.xex'][1]!=ACCEPTED_XEX_SHA256 or indexed['default.xex'][0].read_bytes()[:4]!=b'XEX2':
        raise CreationError('The bundled game is not the verified port executable.')
    if indexed['7zr.exe'][1]!=ARCHIVE_READER_SHA256 or indexed['7zr.exe'][0].read_bytes()[:2]!=b'MZ':
        raise CreationError('The bundled archive reader is not the verified Windows executable.')
    source = _payload_path(payload,manifest.get('source_archive','GameSource.zip'))
    source_sha = manifest.get('source_archive_sha256')
    if builder.digest(source)!=source_sha:
        raise CreationError('The matching GPL source archive is missing or changed.')
    _regular(root/'LICENSE',root); _regular(root/'LICENSE-SCOPE.txt',root)
    portable = root/'tools/portable'
    inventory = json.loads(_regular(portable/'runtime-notices.json',portable).read_text(encoding='utf-8-sig'))
    for entry in inventory['files']:
        notice = _regular(portable/entry['path'],portable)
        if builder.digest(notice)!=entry['sha256'] or not builder.fatx_path(entry['output_path']):
            raise CreationError('A required runtime notice is changed or has an invalid filename.')
    return {'executable':indexed['default.xex'][0],'archive_reader':indexed['7zr.exe'][0],
            'archive_reader_sha256':indexed['7zr.exe'][1], 'source':source,'source_sha256':source_sha,
            'manifest_sha256':builder.digest(manifest_file)}


def _package(root, payload, assets, output, mode):
    directory = output/('Xbox360' if mode=='console' else 'Xenia')
    directory.mkdir(exist_ok=False)
    for name in ('PINBALL.DAT','PBMSG_FT.bin','ASSET-PROVENANCE.json'):
        shutil.copyfile(assets/name,directory/name)
    shutil.copytree(assets/'SOUNDS',directory/'SOUNDS')
    shutil.copyfile(payload['executable'],directory/'default.xex')
    if builder.digest(directory/'default.xex')!=ACCEPTED_XEX_SHA256:
        raise CreationError('The bundled game changed while creating the game folders.')
    shutil.copyfile(root/'LICENSE',directory/'COPYING.txt')
    shutil.copyfile(root/'LICENSE-SCOPE.txt',directory/'LICENSE-SCOPE.txt')
    builder.copy_notices(root/'tools/portable',directory)
    builder.write_config(directory,mode)
    (directory/'START-HERE.txt').write_text(
        f'Pinball Port {VERSION} - {mode}\n\n'
        'Launch default.xex and keep this folder together.\n'
        'Controls: LT/RT flippers; A or D-pad Down plunger; LB/RB shake;\n'
        'Start pause; Y new game. Konami code unlocks the Back cheat menu.\n\n'
        'This folder combines the verified precompiled port with your XP assets.\n'
        'The creator did not compile source or run the original XP executable.\n'
        'Matching GPL-3.0-only game/build source is in ../Source.zip. External\n'
        'runtime notices are under notices/. Original XP assets retain separate\n'
        'rights; the GPL does not grant permission to redistribute those assets.\n',encoding='utf-8')
    files = []
    for path in sorted(directory.rglob('*')):
        if path.is_file():
            relative = path.relative_to(directory).as_posix()
            if not builder.fatx_path(relative):raise CreationError('Unsupported console filename: '+relative)
            files.append({'path':relative,'bytes':path.stat().st_size,'sha256':builder.digest(path)})
    builder.save_json(directory/'PACKAGE-MANIFEST.json',{
        'schema':1,'version':VERSION,'presentation':mode,'creation_method':'verified precompiled port plus separately supplied XP assets',
        'compiler_executed':False,'original_executable_executed':False,'license_new_code':'GPL-3.0-only',
        'assets_license':'separate; no asset redistribution grant','xex_sha256':ACCEPTED_XEX_SHA256,
        'matching_source_archive_sha256':payload['source_sha256'],'files':files})
    (directory/'SHA256SUMS.txt').write_text(''.join(builder.digest(path)+'  '+path.relative_to(directory).as_posix()+'\n'
        for path in sorted(directory.rglob('*')) if path.is_file()),encoding='ascii')
    return {'mode':mode,'executable':str(directory/'default.xex'),'xex_sha256':ACCEPTED_XEX_SHA256,'files':len(files)+2}


def create_game(root: Path, output: Path, cache: Path, *, assets: Path | None = None, emit=print) -> dict:
    """Validate the private payload, fetch/import assets and create two new game folders.

    Existing output is never changed. Failures after creating an owned output
    leave creation-report.json and partial files for diagnosis, then raise.
    """
    root, output, cache = (Path(p).expanduser().resolve() for p in (root,output,cache))
    owned_output = False
    report = {'schema':1,'version':VERSION,'result':'incomplete','started_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
              'creation_method':'verified precompiled port plus separately supplied XP assets','compiler_executed':False,
              'original_executable_executed':False,'license_new_code':'GPL-3.0-only'}
    try:
        builder.validate_work_paths(root,output,cache)
        for chosen in (output,cache):
            if chosen.is_relative_to(root/'payload') or (root/'payload').is_relative_to(chosen):
                raise CreationError('Choose destination and cache folders outside the bundled payload.')
        if output==cache or output.is_relative_to(cache) or cache.is_relative_to(output):
            raise CreationError('The destination and cache must be separate folders.')
        if output.exists():raise CreationError('The destination already exists. Choose a new folder to preserve earlier games.')
        if assets is not None:
            assets = Path(assets).expanduser().resolve()
            if assets.is_dir() and (output.is_relative_to(assets) or cache.is_relative_to(assets)):
                raise CreationError('Choose destination and cache folders outside your original XP folder.')
        emit('Checking the bundled game and source...')
        payload = verify_payload(root)
        output.mkdir(parents=True,exist_ok=False);owned_output = True
        cache.mkdir(parents=True,exist_ok=True)
        stage = output/'.create';stage.mkdir()
        report.update({'payload_manifest_sha256':payload['manifest_sha256'],'xex_sha256':ACCEPTED_XEX_SHA256,
                       'source_archive_sha256':payload['source_sha256'],'download_selected':assets is None})
        if assets is None:
            emit('Downloading or checking the original XP files...')
            assets = xp_assets.download_archive(cache/'downloads')
        emit('Checking and extracting the original game assets...')
        def reader(arguments,timeout=20):
            # Verify immediately before each execution, not only before download.
            helper = _regular(payload['archive_reader'],root/'payload')
            if builder.digest(helper)!=payload['archive_reader_sha256']:
                raise CreationError('The bundled archive reader changed; nothing was executed.')
            kwargs = {'stdout':subprocess.PIPE,'stderr':subprocess.PIPE,'stdin':subprocess.DEVNULL,'timeout':min(timeout,20)}
            if os.name=='nt':kwargs['creationflags']=getattr(subprocess,'CREATE_NO_WINDOW',0)
            return subprocess.run([str(helper),*arguments],**kwargs)
        asset_report = xp_assets.prepare_assets(assets,stage/'assets',sevenzip_runner=reader)
        report['assets'] = asset_report
        shutil.copyfile(payload['source'],output/'Source.zip')
        if builder.digest(output/'Source.zip')!=payload['source_sha256']:
            raise CreationError('The matching source archive changed while creating the game.')
        emit('Creating the Xbox 360 and Xenia game folders...')
        report['packages'] = [_package(root,payload,stage/'assets',output,mode) for mode in ('console','xenia')]
        report['result']='complete'
        emit('Your game is ready.')
        return report
    except Exception as error:
        report['result']='failed';report['error']=str(error)
        raise
    finally:
        report['ended_utc']=dt.datetime.now(dt.timezone.utc).isoformat()
        if owned_output:builder.save_json(output/'creation-report.json',report)


def main(argv=None):
    parser=argparse.ArgumentParser(description='Create Xbox 360 and Xenia game folders automatically; no compiler is required.')
    parser.add_argument('--source',type=Path,default=DEFAULT_ROOT,help=argparse.SUPPRESS)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True)
    parser.add_argument('--assets',type=Path,help='Optional local XP folder,ZIP or supported7z; otherwise download automatically')
    args=parser.parse_args(argv)
    if platform.system()!='Windows' or struct.calcsize('P')!=8:
        print('This game creator requires 64-bit Python for Windows.',file=sys.stderr);return 1
    try:
        report=create_game(args.source,args.output,args.cache,assets=args.assets,emit=lambda value:print(value,flush=True))
        for package in report['packages']:print(package['executable'],flush=True)
        return 0
    except (CreationError,builder.BuildError,xp_assets.AssetError,OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print('Could not create the game: '+str(error),file=sys.stderr,flush=True);return 1

if __name__=='__main__':raise SystemExit(main())
