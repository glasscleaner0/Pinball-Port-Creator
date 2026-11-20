#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the fresh Pinball port; original XP assets are supplied separately."""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import uuid
import zipfile

import xp_assets
from xp_assets import prepare_assets, download_archive, AssetError

VERSION = '0.0.15'
HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parents[1]

class BuildError(RuntimeError):
    pass

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def save_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

def hidden_subprocess_options():
    return {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}

def run(command, *, env=None, cwd=None, capture=False, timeout=None):
    if capture:
        result = subprocess.run(command, env=env, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace',
                                timeout=timeout, **hidden_subprocess_options())
        if result.returncode:
            raise BuildError((result.stderr or result.stdout).strip() or
                             f'{Path(command[0]).name} returned {result.returncode}')
        return result.stdout.strip()
    subprocess.run(command, env=env, cwd=cwd, check=True, timeout=timeout,
                   **hidden_subprocess_options())
    return ''

def wsl_path(path, distribution):
    value = run(['wsl.exe', '--distribution', distribution, '--exec', 'wslpath', '-a', '-u',
                 str(Path(path).resolve())], capture=True, timeout=20)
    if not value.startswith('/') or '\n' in value:
        raise BuildError('WSL did not return one absolute Linux path.')
    return value

def installed_engine(name):
    engine = shutil.which(name)
    if not engine:
        return None
    try:
        run([engine, 'info'], capture=True, timeout=20)
        return engine
    except (BuildError, OSError, subprocess.TimeoutExpired):
        return None

def choose_backend(requested, distribution):
    if requested != 'auto':
        if requested == 'native' and platform.system() != 'Linux':
            raise BuildError('Native compilation requires Linux. Choose Docker, Podman or Windows WSL.')
        if requested == 'wsl' and (os.name != 'nt' or not distribution):
            raise BuildError('For WSL, use Windows and supply --wsl-distribution NAME.')
        if requested in ('docker', 'podman') and not installed_engine(requested):
            raise BuildError(f'{requested.title()} is not installed or its Linux engine is not running.')
        return requested
    if platform.system() == 'Linux':
        return 'native'
    for name in ('docker', 'podman'):
        if installed_engine(name):
            return name
    if os.name == 'nt' and distribution and shutil.which('wsl.exe'):
        return 'wsl'
    raise BuildError('Start Docker/Podman with Linux containers, or choose --backend wsl '
                     '--wsl-distribution NAME on Windows. Python alone is not the Xbox compiler.')

def check_environment(backend, distribution):
    programs = ('bash','git','curl','make','gcc','g++','cmake','ninja','python3',
                'sha256sum','patch','tar','xz','bison','flex','makeinfo')
    if backend == 'native':
        missing = [name for name in programs if not shutil.which(name)]
        if missing:
            raise BuildError('Missing Linux prerequisites: '+', '.join(missing)+'. See Dockerfile or use a container.')
    elif backend == 'wsl':
        command = 'for p in '+ ' '.join(programs)+'; do command -v "$p" >/dev/null || { echo "Missing Linux prerequisite: $p" >&2; exit 1; }; done'
        run(['wsl.exe','--distribution',distribution,'--exec','sh','-c',command],capture=True,timeout=30)
    elif backend == 'docker':
        value = run([shutil.which('docker'),'info','--format','{{.OSType}}'],capture=True,timeout=20)
        if value.lower() != 'linux':
            raise BuildError('Switch Docker to Linux containers before building.')
    # installed_engine already establishes a responding Podman engine.

def validate_work_paths(root, output, cache):
    for destination in (output, cache):
        if destination == root or root.is_relative_to(destination):
            raise BuildError('Output/cache must not replace the source folder or one of its parents.')
        for tree in ('src/rewrite','tests/rewrite','tools/portable','original'):
            protected = root/tree
            if destination.is_relative_to(protected) or protected.is_relative_to(destination):
                raise BuildError('Output/cache must stay outside source and reference trees: '+tree)

def source_files(root):
    """Only our implementation/tests/builder and retained notices; never original assets."""
    root = Path(root).resolve()
    result = []
    for name in ('CMakeLists.txt', 'LICENSE', 'LICENSE-SCOPE.txt'):
        p = root / name
        if not p.is_file() or p.is_symlink():
            raise BuildError(f'Missing source file: {name}')
        result.append(p)
    for tree in ('src/rewrite', 'tests/rewrite', 'tools/portable'):
        base = root / tree
        if not base.is_dir() or base.is_symlink():
            raise BuildError(f'Missing source directory: {tree}')
        for p in sorted(base.rglob('*')):
            relative = p.relative_to(base)
            if '__pycache__' in relative.parts or p.suffix in ('.pyc', '.pyo'):
                continue
            if p.is_symlink():
                raise BuildError(f'Source symlink is not accepted: {p.relative_to(root)}')
            if p.is_file():
                if not p.resolve().is_relative_to(root):
                    raise BuildError('Source file leaves the source folder.')
                result.append(p)
    return sorted(result)

def source_identity(root):
    entries = [{'path':p.relative_to(root).as_posix(), 'bytes':p.stat().st_size,
                'sha256':digest(p)} for p in source_files(root)]
    identity = hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest()
    return {'sha256':identity, 'files':entries}

def archive_source(root, destination):
    # This allowlist includes build scripts and headers, and excludes XP assets/cache/output.
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED) as out:
        for p in source_files(root):
            out.write(p, p.relative_to(root).as_posix())
        # runpy does not add the module directory to sys.path; the top-level launcher does.
        launcher = "#!/usr/bin/env python3\n# SPDX-License-Identifier: GPL-3.0-only\nimport runpy,sys\nfrom pathlib import Path\np=Path(__file__).resolve().parent/'tools/portable'\nsys.path.insert(0,str(p))\nrunpy.run_path(str(p/'builder.py'),run_name='__main__')\n"
        out.writestr('build.py', launcher)
        gui_launcher = "# SPDX-License-Identifier: GPL-3.0-only\nimport runpy,sys\nfrom pathlib import Path\np=Path(__file__).resolve().parent/'tools/portable'\nsys.path.insert(0,str(p))\nrunpy.run_path(str(p/'gui.py'),run_name='__main__')\n"
        out.writestr('Start Builder.pyw', gui_launcher)
        # Source-only bundles need developer instructions, not the Creator quick start.
        out.write(root/'tools/portable/BUILD-FROM-SOURCE.txt', 'README.txt')

def fatx_path(relative):
    forbidden = set('"*/:<>?\\|+,;=[]')
    return all(0 < len(part) <= 42 and part == part.strip(' .') and
               all(ord(c) >= 32 and ord(c) != 127 and c not in forbidden for c in part)
               for part in Path(relative).parts)

def copy_notices(portable, target):
    inventory = json.loads((portable/'runtime-notices.json').read_text(encoding='utf-8-sig'))
    for entry in inventory['files']:
        source = portable/entry['path']
        output = target/entry['output_path']
        if not source.resolve().is_relative_to(portable.resolve()) or not output.resolve().is_relative_to(target.resolve()):
            raise BuildError('Notice inventory path leaves its package.')
        if not fatx_path(entry['output_path']) or digest(source) != entry['sha256']:
            raise BuildError('A retained runtime notice is changed or has an invalid console filename.')
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, output)
    shutil.copyfile(portable/'runtime-notices.json', target/'RUNTIME-NOTICES.json')

def write_config(target, mode):
    text = f'''# Pinball Port {VERSION}; original XP assets supplied separately.
dat=game:\\PINBALL.DAT
present={mode}
canvas_width=600
canvas_height=416
max_frames=0
sound=1
sound_volume=100
startup=1
left_flipper=LT
right_flipper=RT
plunger=A,DPAD_DOWN
nudge_left=LB
nudge_right=RB
pause=START
hidden_test=BACK
cheat_unlock=DPAD_UP,DPAD_UP,DPAD_DOWN,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,DPAD_LEFT,DPAD_RIGHT,B,A
exit=NONE
new_game=Y
'''
    (target/'pinball360.ini').write_text(text, encoding='utf-8', newline='\n')

def package_game(root, compiled, assets, output, modes, identity):
    executable = compiled/'default.xex'
    if executable.read_bytes()[:4] != b'XEX2':
        raise BuildError('The compiler output is not an Xbox XEX2 executable.')
    toolchain = json.loads((compiled/'BUILD-TOOLCHAIN.json').read_text(encoding='utf-8'))
    if digest(executable) != toolchain['xex_sha256']:
        raise BuildError('The executable differs from its compiler record.')
    packages = []
    for mode in modes:
        directory = output/('Xbox360' if mode == 'console' else 'Xenia')
        directory.mkdir(exist_ok=False)
        for name in ('PINBALL.DAT', 'PBMSG_FT.bin', 'ASSET-PROVENANCE.json'):
            shutil.copyfile(assets/name, directory/name)
        shutil.copytree(assets/'SOUNDS', directory/'SOUNDS')
        shutil.copyfile(executable, directory/'default.xex')
        shutil.copyfile(root/'LICENSE', directory/'COPYING.txt')
        shutil.copyfile(root/'LICENSE-SCOPE.txt', directory/'LICENSE-SCOPE.txt')
        copy_notices(root/'tools/portable', directory)
        write_config(directory, mode)
        (directory/'START-HERE.txt').write_text(
            f'Pinball Port {VERSION} - {mode}\n\nLaunch default.xex and keep this folder together.\n'
            'Controls: LT/RT flippers; A or D-pad Down plunger; LB/RB shake;\n'
            'Start pause; Y new game. Konami code unlocks Back cheat menu.\n\n'
            'The newly written game code is GPL-3.0-only. Corresponding source\n'
            'and build scripts are in ../Source.zip. External runtime notices\n'
            'are under notices/. Original XP assets retain separate rights;\n'
            'GPL does not grant permission to redistribute those assets.\n\n'
            'Compilation and package checks do not establish a playtest pass.\n', encoding='utf-8')
        files = []
        for p in sorted(directory.rglob('*')):
            if p.is_file():
                rel = p.relative_to(directory).as_posix()
                if not fatx_path(rel):
                    raise BuildError(f'Unsupported Xbox filesystem path: {rel}')
                files.append({'path':rel, 'bytes':p.stat().st_size, 'sha256':digest(p)})
        manifest = {'schema':1, 'version':VERSION, 'presentation':mode,
                    'license_new_code':'GPL-3.0-only', 'assets_license':'separate, not granted by this package',
                    'source_sha256':identity['sha256'], 'xex_sha256':digest(executable),
                    'gameplay_validation':'not performed by builder', 'files':files}
        save_json(directory/'PACKAGE-MANIFEST.json', manifest)
        checksums = ''.join(digest(p)+'  '+p.relative_to(directory).as_posix()+'\n'
                            for p in sorted(directory.rglob('*')) if p.is_file())
        (directory/'SHA256SUMS.txt').write_text(checksums, encoding='ascii')
        packages.append({'mode':mode, 'executable':str(directory/'default.xex'),
                         'xex_sha256':manifest['xex_sha256'], 'files':len(files)+2})
    return packages

def prepare_container_image(root, backend):
    engine = shutil.which(backend)
    if not engine:
        raise BuildError(f'{backend.title()} is not installed or is not available.')
    containerfile = root/'tools/portable/Dockerfile'
    image = 'pinball-port-builder:'+digest(containerfile)[:12]
    try:
        run([engine, 'image', 'inspect', image], capture=True, timeout=20)
    except BuildError:
        print('Preparing build tools. This first setup can take a while...', flush=True)
        run([engine, 'build', '--tag', image, '--file', str(containerfile), str(containerfile.parent)])
    return engine, image

def prepare_asset_input(args, root, source, destination, backend):
    """Read verified7z through an available environment, without a host add-on."""
    source = Path(source)
    if source.suffix.lower() != '.7z' or not source.is_file():
        return prepare_assets(source, destination)
    if xp_assets._is_link(source) or source.stat().st_size > xp_assets.MAX_ARCHIVE_BYTES:
        raise AssetError('Choose the supported original XP archive or an extracted folder.')
    # This pin precedes any archive parser or container setup/download.
    if digest(source) != xp_assets.ARCHIVE_SHA256:
        raise AssetError('This .7z is not the supported XP archive; supply its extracted files or a ZIP instead.')
    try:
        xp_assets._seven_zip()
    except AssetError:
        pass
    else:
        return prepare_assets(source, destination)
    print('Preparing archive support...', flush=True)
    if backend in ('docker', 'podman'):
        if ',' in str(source.resolve()):
            raise BuildError('Choose a download/cache path without commas for the container environment.')
        engine, image = prepare_container_image(root, backend)
        alias = '/input/asset.7z'
        mount = f'type=bind,source={source.resolve()},target={alias},readonly'
        def runner(arguments, timeout=20):
            return subprocess.run([engine, 'run', '--rm', '--mount', mount,
                                   '--entrypoint', '7z', image, *arguments],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=timeout, **hidden_subprocess_options())
        return prepare_assets(source, destination, sevenzip_runner=runner,
                              sevenzip_archive_path=alias)
    if backend == 'wsl':
        try:
            program = run(['wsl.exe', '--distribution', args.wsl_distribution, '--exec',
                           'sh', '-c', 'command -v 7z || command -v 7zz'],
                          capture=True, timeout=20)
        except BuildError as error:
            raise BuildError('Archive support is missing from your Linux build environment. '
                             'Use Docker/Podman, which includes it, or select an extracted XP folder or ZIP.') from error
        if not program.startswith('/') or '\n' in program:
            raise BuildError('The Linux archive tool could not be located.')
        alias = wsl_path(source, args.wsl_distribution)
        def runner(arguments, timeout=20):
            return subprocess.run(['wsl.exe', '--distribution', args.wsl_distribution,
                                   '--exec', program, *arguments],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=timeout, **hidden_subprocess_options())
        return prepare_assets(source, destination, sevenzip_runner=runner,
                              sevenzip_archive_path=alias)
    raise BuildError('Archive support is missing. Use the supplied container environment, '
                     'or select an extracted XP folder or ZIP.')

def compile_game(args, root, cache, compiled):
    backend = choose_backend(args.backend, args.wsl_distribution)
    print('Build environment: '+backend, flush=True)
    values = {'PINBALL_SOURCE':str(root), 'PINBALL_CACHE':str(cache),
              'PINBALL_OUTPUT':str(compiled), 'PINBALL_JOBS':str(args.jobs)}
    if args.toolchain:
        values['PINBALL_TOOLCHAIN'] = str(Path(args.toolchain).resolve())
    if args.sdk:
        values['PINBALL_SDK_ROOT'] = str(Path(args.sdk).resolve())
    if backend == 'native':
        if any(any(c.isspace() for c in values[key]) for key in ("PINBALL_CACHE", "PINBALL_TOOLCHAIN", "PINBALL_SDK_ROOT") if key in values):
            raise BuildError("Use a compiler cache/SDK/toolchain path without spaces for native Linux, or use a container backend.")
        run(['bash', str(root/'tools/portable/bootstrap.sh')], env={**os.environ, **values})
    elif backend == 'wsl':
        for key in list(values):
            if key != 'PINBALL_JOBS':
                values[key] = wsl_path(values[key], args.wsl_distribution)
        if any(any(c.isspace() for c in values[key]) for key in ("PINBALL_CACHE", "PINBALL_TOOLCHAIN", "PINBALL_SDK_ROOT") if key in values):
            raise BuildError("Use a compiler cache/SDK/toolchain path without spaces for WSL, or use a container backend.")
        run(['wsl.exe', '--distribution', args.wsl_distribution, '--exec', 'env',
             *[key+'='+value for key,value in values.items()], 'bash',
             values['PINBALL_SOURCE']+'/tools/portable/bootstrap.sh'])
    else:
        if args.toolchain or args.sdk:
            raise BuildError('--toolchain/--sdk overrides are supported for native Linux/WSL; containers manage their own compiler cache.')
        engine, image = prepare_container_image(root, backend)
        mounts = []
        for source, destination, readonly in ((root,'/source',True),(cache,'/cache',False),(compiled,'/output',False)):
            if ',' in str(source):
                raise BuildError('Docker/Podman bind mount paths must not contain commas. Choose another source/cache/output path.')
            mounts += ['--mount', f'type=bind,source={source},target={destination}'+(',readonly' if readonly else '')]
        command = [engine, 'run', '--rm', *mounts, '--env', 'PINBALL_JOBS='+str(args.jobs)]
        if os.name != 'nt' and backend == 'docker':
            command += ['--user', f'{os.getuid()}:{os.getgid()}']
        run(command+[image])
    return backend

def parser():
    p = argparse.ArgumentParser(description='Build Pinball Port for Xbox 360 and Xenia from source and separately supplied XP assets.')
    source = p.add_mutually_exclusive_group()
    source.add_argument('--assets', type=Path, help='XP game folder, ZIP or original7z archive')
    source.add_argument('--download-xp', action='store_true', help='Explicitly download the user-selected Internet Archive file, verify its exact hash, then import it')
    p.add_argument('--output', type=Path, help='New destination folder; existing folders are never overwritten')
    p.add_argument('--cache', type=Path, help='Compiler/download cache (first setup needs roughly10-15GB)')
    p.add_argument('--source', type=Path, default=SOURCE_ROOT, help=argparse.SUPPRESS)
    p.add_argument('--backend', choices=('auto','native','docker','podman','wsl'), default='auto')
    p.add_argument('--wsl-distribution', help='Existing WSL Linux distribution name; no installation or OS changes are performed')
    p.add_argument('--target', choices=('both','console','xenia'), default='both')
    p.add_argument('--jobs', type=int, choices=(1,2), default=2)
    p.add_argument('--toolchain', type=Path, help='Reuse an existing pinned PowerPC toolchain on native Linux/WSL')
    p.add_argument('--sdk', type=Path, help='Reuse an existing clean pinned xsdk checkout on native Linux/WSL')
    p.add_argument('--check', action='store_true', help='Check source, notices and build environment without downloading or compiling')
    return p

def main(argv=None):
    args = parser().parse_args(argv)
    root = args.source.expanduser().resolve()
    output = None
    owned_output = False
    record = {'schema':1, 'version':VERSION, 'started_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
              'result':'incomplete', 'license_new_code':'GPL-3.0-only'}
    try:
        identity = source_identity(root)
        # Verify notice payloads without producing a game or invoking a compiler.
        inv = json.loads((root/'tools/portable/runtime-notices.json').read_text(encoding='utf-8-sig'))
        for item in inv['files']:
            candidate = root/'tools/portable'/item['path']
            if not candidate.resolve().is_relative_to((root/'tools/portable').resolve()) or digest(candidate) != item['sha256']:
                raise BuildError('A retained dependency notice is missing or changed.')
        backend = choose_backend(args.backend,args.wsl_distribution)
        if args.check:
            check_environment(backend,args.wsl_distribution)
            print(f'Checks passed. Source files: {len(identity["files"])}; build environment: {backend}.')
            print('Compilation, asset import and gameplay have not run.')
            return 0
        if not args.assets and not args.download_xp:
            raise BuildError('Supply --assets PATH, or explicitly choose --download-xp. See README.txt.')
        stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:6]
        output = (args.output or root/'dist'/('Pinball-'+stamp)).expanduser().resolve()
        cache = (args.cache or root/'.pinball-cache').expanduser().resolve()
        if args.assets:
            source = args.assets.expanduser().resolve()
            if source.is_dir() and (output.is_relative_to(source) or cache.is_relative_to(source)):
                raise BuildError('Output/cache must be outside the supplied XP game folder.')
        validate_work_paths(root,output,cache)
        if output == cache or output.is_relative_to(cache) or cache.is_relative_to(output):
            raise BuildError('Output and compiler cache must be separate folders.')
        if output.exists():
            raise BuildError('Output already exists. Choose a new destination to preserve previous builds.')
        output.mkdir(parents=True,exist_ok=False)
        owned_output = True
        cache.mkdir(parents=True,exist_ok=True)
        work = output/'.build'; work.mkdir(); compiled = work/'compiled';compiled.mkdir()
        asset_source = download_archive(cache/'downloads') if args.download_xp else args.assets.expanduser().resolve()
        print('Checking XP assets without running the original executable...',flush=True)
        asset_report = prepare_asset_input(args,root,asset_source,work/'assets',backend)
        record.update({'source':identity,'backend':backend,'download_xp_selected':args.download_xp,'assets':asset_report})
        backend = compile_game(args,root,cache,compiled)
        # Preserve the exact source that produced this build, with no XP assets.
        if source_identity(root) != identity:
            raise BuildError('Source changed while compilation was running; refusing to package an ambiguous build.')
        archive_source(root,output/'Source.zip')
        modes = ('console','xenia') if args.target == 'both' else (args.target,)
        record['packages'] = package_game(root,compiled,work/'assets',output,modes,identity)
        record['source_archive_sha256'] = digest(output/'Source.zip')
        record['result'] = 'complete'
        print('\nBuild complete. Launch the extracted executable for your target:',flush=True)
        for item in record['packages']:
            print('  '+item['executable'],flush=True)
        print('Source.zip contains the matching GPLv3 source; original XP assets remain separate.',flush=True)
        return 0
    except (BuildError,AssetError,OSError,ValueError,subprocess.SubprocessError,KeyError) as error:
        record['result'] = 'failed';record['error'] = str(error)
        print('Build stopped: '+str(error),file=sys.stderr,flush=True)
        if owned_output and output and output.exists():
            print('Partial files and diagnostics are preserved at '+str(output),file=sys.stderr,flush=True)
        return 1
    finally:
        record['ended_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        if owned_output and output and output.is_dir():
            save_json(output/'build-report.json',record)

if __name__ == '__main__':
    raise SystemExit(main())
