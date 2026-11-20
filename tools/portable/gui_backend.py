# SPDX-License-Identifier: GPL-3.0-only
"""Public build-environment discovery for the Pinball builder window.

Discovery is read-only. It checks installed tools and versions; it never installs
packages, enables OS features, or depends on the developer's workspace.
"""
from __future__ import annotations

from dataclasses import dataclass
import datetime as dt
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Callable, Optional
import uuid

DOCKER_SETUP_URL = 'https://www.docker.com/products/docker-desktop/'
PODMAN_SETUP_URL = 'https://podman-desktop.io/downloads'
SEVEN_ZIP_SETUP_URL = 'https://www.7-zip.org/download.html'
PREREQUISITES = ('bash', 'git', 'curl', 'make', 'gcc', 'g++', 'cmake', 'ninja',
                 'python3', 'sha256sum', 'patch', 'tar', 'xz', 'bison', 'flex', 'makeinfo')
SDK_REVISION = 'ade2c81d914634834d77394c5d9f8c4575f395bc'


def default_cache_root() -> Path:
    """Return the conventional per-user cache without creating it."""
    system = platform.system()
    home = Path.home()
    if system == 'Windows':
        return Path(os.environ.get('LOCALAPPDATA') or home/'AppData/Local')/'PinballPortBuilder'
    if system == 'Darwin':
        return home/'Library/Caches/PinballPortBuilder'
    xdg = os.environ.get('XDG_CACHE_HOME')
    base = Path(xdg).expanduser() if xdg and Path(xdg).is_absolute() else home/'.cache'
    return base/'pinball-port-builder'


def _decode_output(payload: bytes) -> str:
    # wsl.exe --list emits UTF-16LE on several supported Windows versions.
    if payload.startswith((b'\xff\xfe', b'\xfe\xff')):
        return payload.decode('utf-16', errors='replace')
    if b'\x00' in payload:
        return payload.decode('utf-16-le', errors='replace')
    return payload.decode('utf-8-sig', errors='replace')


def _run_text(command, timeout=12) -> str:
    kwargs = {'stdout':subprocess.PIPE, 'stderr':subprocess.PIPE, 'timeout':timeout,
              'check':False, 'stdin':subprocess.DEVNULL}
    if platform.system() == 'Windows':
        kwargs['creationflags'] = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    result = subprocess.run(command, **kwargs)
    if result.returncode:
        raise RuntimeError(_decode_output(result.stderr).strip() or
                           _decode_output(result.stdout).strip() or 'Tool check failed')
    return _decode_output(result.stdout).strip()


def _host_archive_tool() -> Optional[str]:
    for name in ('7zz', '7z', '7za'):
        program = shutil.which(name)
        if program:
            return program
    if platform.system() == 'Windows':
        for key in ('ProgramFiles', 'ProgramFiles(x86)'):
            base = os.environ.get(key)
            if base:
                candidate = Path(base)/'7-Zip/7z.exe'
                if candidate.is_file():
                    return str(candidate)
    return None


def _engine_ready(name: str) -> Optional[str]:
    executable = shutil.which(name)
    if not executable:
        return None
    field = '{{.OSType}}' if name == 'docker' else '{{.Host.OS}}'
    try:
        if _run_text([executable, 'info', '--format', field], timeout=8).lower() != 'linux':
            return None
        version = _run_text([executable, '--version'], timeout=5)
        return version.splitlines()[0] if version else name.title()
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return None


def _native_ready() -> tuple[bool, str]:
    missing = [name for name in PREREQUISITES if not shutil.which(name)]
    if missing:
        return False, 'Some Linux build tools are missing.'
    try:
        version = _run_text(['cmake', '--version'], timeout=5)
        match = re.search(r'(\d+)\.(\d+)', version)
        if not match or tuple(map(int, match.groups())) < (3, 16):
            return False, 'The installed Linux build tools need updating.'
        _run_text(['python3', '-c', 'import sys;sys.exit(sys.version_info < (3,10))'], timeout=5)
        return True, 'Linux build tools are ready.'
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return False, 'The installed Linux build tools need updating.'


def _wsl_distributions() -> list[str]:
    executable = shutil.which('wsl.exe')
    if not executable:
        return []
    try:
        listing = _run_text([executable, '--list', '--quiet'], timeout=12)
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return []
    return [line.strip().lstrip('\ufeff') for line in listing.splitlines()
            if line.strip() and len(line.strip()) <= 128][:12]


def _wsl_ready(distribution: str, need_archive: bool) -> tuple[bool, Optional[str]]:
    # Every word of this shell program is fixed; the distribution is a separate argv value.
    command = 'for p in '+' '.join(PREREQUISITES)+'; do command -v "$p" >/dev/null || exit 1; done; '
    command += "python3 -c 'import sys;sys.exit(sys.version_info < (3,10))' || exit 1; "
    command += "cmake --version | head -n 1; "
    if need_archive:
        command += 'command -v 7zz || command -v 7z || exit 1'
    try:
        output = _run_text(['wsl.exe','--distribution',distribution,'--exec','sh','-c',command], timeout=12)
        match = re.search(r'(\d+)\.(\d+)', output)
        if not match or tuple(map(int,match.groups())) < (3,16):
            return False, None
        archive = output.splitlines()[-1] if need_archive else None
        return True, archive
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return False, None


@dataclass(frozen=True)
class BuildPlan:
    backend: Optional[str]
    cache_root: Path
    distribution: Optional[str] = None
    ready: bool = True
    message: str = ''
    setup_url: Optional[str] = None
    archive_tool: Optional[str] = None
    version: str = ''

    @property
    def cache(self) -> Path:
        return self.cache_root/'native'

    @property
    def needs_setup(self) -> bool:
        return not self.ready

    def command(self, source_root: Path, output: Path, *, python: Optional[str] = None,
                download: bool = True, assets: Optional[Path] = None) -> list[str]:
        """Create argv for the existing CLI; no source or cache is modified here."""
        if not self.ready or not self.backend:
            raise RuntimeError(self.message or 'Build tools need setup first.')
        if not download and assets is None:
            raise ValueError('Select the original game files or use the download option.')
        if download and assets is not None:
            raise ValueError('Select either download or local files, not both.')
        root = Path(source_root).resolve()
        command = [python or sys.executable, '-u', '-B', str(root/'tools/portable/builder.py'),
                   '--backend', self.backend, '--cache', str(self.cache.resolve()),
                   '--output', str(Path(output).resolve()), '--target', 'both']
        if self.distribution:
            command += ['--wsl-distribution', self.distribution]
        if download:
            command.append('--download-xp')
        else:
            command += ['--assets', str(Path(assets).resolve())]
        # Only this app's own compiler cache is eligible for reuse. The bootstrap
        # still checks target/version before running it. Workspace paths are never searched.
        compiler = self.cache/'powerpc-xsdk'
        binary = compiler/'bin/powerpc-xsdk-elf-g++'
        if self.backend in ('native','wsl') and binary.is_file() and compiler.resolve().is_relative_to(self.cache_root.resolve()):
            command += ['--toolchain', str(compiler.resolve())]
        return command


def new_output_directory(destination: Path) -> Path:
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d-%H%M%S')
    return Path(destination).expanduser().resolve()/('Pinball-'+stamp+'-'+uuid.uuid4().hex[:6])


def discover_environment(source_root: Path, emit: Optional[Callable[[str],None]] = None,
                         cache_root: Optional[Path] = None, require_archive: bool = True) -> BuildPlan:
    """Find a supported public installation, without installing or changing it."""
    say = emit or (lambda message: None)
    root = Path(source_root).resolve()
    cache = Path(cache_root).expanduser().resolve() if cache_root is not None else default_cache_root()
    if not (root/'CMakeLists.txt').is_file() or not (root/'tools/portable/builder.py').is_file():
        return BuildPlan(None,cache,ready=False,message='Extract the complete source package before opening the builder.')
    archive = _host_archive_tool()
    for name in ('docker','podman'):
        say('Checking installed build tools…')
        version = _engine_ready(name)
        if version:
            if ',' in str(root) or ',' in str(cache):
                return BuildPlan(None,cache,ready=False,message='Move the builder and its cache to folders without commas, then check again.')
            return BuildPlan(name,cache,message='Ready to download and build.',archive_tool=archive or 'container',version=version)
    system = platform.system()
    if system == 'Linux':
        say('Checking Linux build tools…')
        ready, message = _native_ready()
        if ready and not any(char.isspace() for char in str(cache)):
            if require_archive and not archive:
                return BuildPlan(None,cache,ready=False,message='7-Zip is needed to unpack the download. Install it, then check again.',setup_url=SEVEN_ZIP_SETUP_URL)
            return BuildPlan('native',cache,message='Ready to download and build.',archive_tool=archive)
    if system == 'Windows':
        if any(char.isspace() for char in str(cache)):
            return BuildPlan(None,cache,ready=False,message='Install or start Docker Desktop for this user folder, then check again.',setup_url=DOCKER_SETUP_URL,archive_tool=archive)
        say('Checking installed Linux build environments…')
        for distribution in _wsl_distributions():
            say('Checking installed build tools…')
            ready, linux_archive = _wsl_ready(distribution, require_archive and not archive)
            if ready:
                return BuildPlan('wsl',cache,distribution=distribution,message='Ready to download and build.',archive_tool=archive or linux_archive,version=distribution)
    return BuildPlan(None,cache,ready=False,
                     message='Install or start Docker Desktop, then click Check Again. The builder will prepare the compiler automatically.',
                     setup_url=DOCKER_SETUP_URL,archive_tool=archive)
