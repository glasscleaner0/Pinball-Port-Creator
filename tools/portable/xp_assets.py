# SPDX-License-Identifier: GPL-3.0-only
"""Read original XP assets without executing the original program.

No game assets or reference implementation are embedded in this module. Downloads
are opt-in and pinned to the user's selected archive; hosting is not a rights grant.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

ARCHIVE_URL = "https://archive.org/download/3-d-pinball-for-windows.-7z/3D%20Pinball%20for%20Windows.7z"
ARCHIVE_SHA256 = "4cefe8382bc7e95d57a87510dc4da90f7c285ba7adf334e706325de18c3eb462"
MAX_FILES = 128
MAX_DIRECTORIES = 256
MAX_MEMBER_BYTES = 16 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MANIFEST = Path(__file__).with_name("expected_xp_assets.json")


class AssetError(ValueError):
    """The selected input is unsupported, unsafe, missing, or not the target XP set."""


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _file_digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def _is_link(path: Path) -> bool:
    details = path.lstat()
    return stat.S_ISLNK(details.st_mode) or bool(getattr(details, "st_file_attributes", 0) & 0x400)


def _safe_member(name: str) -> str:
    normalized = name.replace("\\", "/").rstrip("/")
    parts = normalized.split("/")
    if (not normalized or len(normalized) > 512 or len(parts) > 12
            or normalized.startswith("/") or any(part in ("", ".", "..") for part in parts)
            or any(len(part) > 128 for part in parts)
            or any(ord(char) < 32 or char in ':*?"<>|' for char in normalized)
            or re.match(r"^[A-Za-z]:", normalized)):
        raise AssetError("Unsafe archive/folder path: " + repr(name))
    return str(PurePosixPath(normalized))


def _metadata() -> dict:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if len(data["files"]) != 62:
        raise AssetError("Expected XP asset inventory is incomplete")
    return data


def _index_record(index: dict, name: str, size: int, item, paths: set) -> None:
    safe = _safe_member(name)
    folded = safe.casefold()
    if folded in paths:
        raise AssetError("Duplicate or case-colliding input path: " + safe)
    paths.add(folded)
    if size < 0 or size > MAX_MEMBER_BYTES:
        raise AssetError("Input file exceeds the per-file limit: " + safe)
    base = PurePosixPath(safe).name.casefold()
    if base in index:
        raise AssetError("Ambiguous duplicate asset filename: " + PurePosixPath(safe).name)
    index[base] = (size, item)
    if len(index) > MAX_FILES or sum(record[0] for record in index.values()) > MAX_TOTAL_BYTES:
        raise AssetError("Input exceeds the file-count or uncompressed-size limit")


def _selected(index: dict, expected: dict) -> list:
    needed = expected["files"]
    missing = [record["name"] for record in needed if record["name"].casefold() not in index]
    if missing:
        raise AssetError("Missing original XP files: " + ", ".join(missing))
    selected = []
    for record in needed:
        size, item = index[record["name"].casefold()]
        if size != record["bytes"]:
            raise AssetError("Wrong XP file size: " + record["name"])
        selected.append((record, item))
    return selected


def _folder_payloads(source: Path, expected: dict) -> dict:
    index, paths, directory_count = {}, set(), 0
    for directory, subdirectories, files in os.walk(source, followlinks=False):
        directory_count += 1
        if directory_count > MAX_DIRECTORIES:
            raise AssetError("Input folder has too many directories")
        current = Path(directory)
        for name in subdirectories:
            item = current / name
            _safe_member(item.relative_to(source).as_posix())
            if _is_link(item):
                raise AssetError("Linked folders are not accepted: " + name)
        for name in files:
            item = current / name
            if _is_link(item) or not item.is_file():
                raise AssetError("Linked or special files are not accepted: " + name)
            _index_record(index, item.relative_to(source).as_posix(), item.stat().st_size, item, paths)
    return {record["name"]: item.read_bytes() for record, item in _selected(index, expected)}


def _zip_payloads(source: Path, expected: dict) -> dict:
    index, paths, directory_count = {}, set(), 0
    with zipfile.ZipFile(source) as archive:
        if len(archive.infolist()) > MAX_FILES + MAX_DIRECTORIES:
            raise AssetError("ZIP has too many entries")
        for item in archive.infolist():
            safe = _safe_member(item.filename)
            mode = (item.external_attr >> 16) & 0xFFFF
            kind = stat.S_IFMT(mode)
            if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
                raise AssetError("ZIP contains a link or special file: " + safe)
            if item.flag_bits & 1:
                raise AssetError("Encrypted ZIP entries are not accepted")
            if item.is_dir():
                directory_count += 1
                if directory_count > MAX_DIRECTORIES:
                    raise AssetError("ZIP has too many directories")
                if safe.casefold() in paths:
                    raise AssetError("Case-colliding ZIP directory: " + safe)
                paths.add(safe.casefold())
            else:
                _index_record(index, safe, item.file_size, item, paths)
        result = {}
        for record, item in _selected(index, expected):
            with archive.open(item, "r") as stream:
                payload = stream.read(record["bytes"] + 1)
            if len(payload) != record["bytes"]:
                raise AssetError("ZIP entry length differs from its directory: " + record["name"])
            result[record["name"]] = payload
        return result


def _seven_zip() -> str:
    for name in ("7zz", "7z", "7za"):
        found = shutil.which(name)
        if found:
            return found
    if os.name == "nt":
        for key in ("ProgramFiles", "ProgramFiles(x86)"):
            base = os.environ.get(key)
            if base:
                candidate = Path(base) / "7-Zip" / "7z.exe"
                if candidate.is_file():
                    return str(candidate)
    raise AssetError("7-Zip is needed for .7z input; install 7z/7zz, or supply an extracted XP folder or ZIP")


def _run_seven_zip(arguments: list[str], *, runner=None, timeout: int = 20):
    """Run a trusted7z tool locally or through an explicitly supplied backend.

    Backend runners receive argument lists without an executable name and must
    return CompletedProcess-like results with byte stdout/stderr. No shell text
    or downloaded executable is executed by this module.
    """
    if runner is None:
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        result = subprocess.run([_seven_zip(), *arguments], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, creationflags=flags)
    else:
        result = runner(arguments, timeout=timeout)
    if (not isinstance(getattr(result, "returncode", None), int)
            or not isinstance(getattr(result, "stdout", None), bytes)):
        raise AssetError("7-Zip backend returned an invalid binary response")
    return result


def _seven_payloads(source: Path, expected: dict, *, runner=None,
                    archive_path: str | None = None) -> dict:
    # Pin before launching any external archive parser. Repacked inputs can be
    # supplied as a folder or ZIP; this route accepts the exact original archive.
    if _file_digest(source) != ARCHIVE_SHA256:
        raise AssetError("This .7z is not the verified XP archive; supply its extracted files or a ZIP instead")
    translated = str(source) if archive_path is None else archive_path
    if (not isinstance(translated, str) or not translated
            or any(ord(char) < 32 for char in translated)):
        raise AssetError("7-Zip backend archive path is invalid")
    if archive_path is not None and runner is None:
        raise AssetError("A translated archive path requires an explicit7z backend")
    listing = _run_seven_zip(["l", "-slt", "-ba", "-bd", "-sccUTF-8", "--", translated],
                              runner=runner)
    if listing.returncode != 0 or len(listing.stdout) > 1024 * 1024:
        raise AssetError("7-Zip could not list the original archive")
    blocks = listing.stdout.decode("utf-8-sig", errors="strict").replace("\r\n", "\n").strip().split("\n\n")
    index, paths = {}, set()
    for block in blocks:
        values = dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
        if "Path" not in values:
            continue
        if values.get("Encrypted", "-") != "-" or "Symbolic Link" in values or "Hard Link" in values:
            raise AssetError("Linked/encrypted 7z members are not accepted")
        safe = _safe_member(values["Path"])
        if values.get("Folder") == "+" or values.get("Attributes", "").startswith("D"):
            continue
        _index_record(index, safe, int(values.get("Size", "-1")), values["Path"], paths)
    result = {}
    for record, member in _selected(index, expected):
        extracted = _run_seven_zip(["e", "-so", "-y", "-bd", "--", translated, member],
                                    runner=runner)
        if extracted.returncode != 0 or len(extracted.stdout) != record["bytes"]:
            raise AssetError("7-Zip could not read the expected member: " + record["name"])
        result[record["name"]] = extracted.stdout
    return result


def prepare_assets(source: Path, dest: Path, *, sevenzip_runner=None,
                   sevenzip_archive_path: str | None = None) -> dict:
    """Validate this exact XP set and atomically create a new runtime asset folder.

    source is an extracted folder, ZIP, or the verified original7z. dest must not
    exist. The original executable is read only to extract its font and is never
    run, copied into the output, or compiled into the game.

    A GUI may provide sevenzip_runner(arguments, timeout=20) to use7z inside
    its Linux build environment. The optional sevenzip_archive_path supplies
    that environment's translated path to the same locally hash-verified file.
    Without a runner, the normal installed7z tool is used for7z input.
    """
    source, dest = Path(source), Path(dest)
    if not source.exists() or _is_link(source):
        raise AssetError("Asset input is missing or linked: " + str(source))
    source = source.resolve()
    if dest.exists() or dest.is_symlink():
        raise AssetError("Asset destination already exists; refusing to overwrite it: " + str(dest))
    if dest.resolve().is_relative_to(source) and source.is_dir():
        raise AssetError("Asset output must be outside the input folder")
    expected = _metadata()
    try:
        if source.is_dir():
            kind, payloads = "folder", _folder_payloads(source, expected)
        elif source.is_file() and source.stat().st_size <= MAX_ARCHIVE_BYTES and source.suffix.lower() == ".zip":
            kind, payloads = "zip", _zip_payloads(source, expected)
        elif source.is_file() and source.stat().st_size <= MAX_ARCHIVE_BYTES and source.suffix.lower() == ".7z":
            kind, payloads = "7z", _seven_payloads(source, expected,
                                                  runner=sevenzip_runner,
                                                  archive_path=sevenzip_archive_path)
        else:
            raise AssetError("Supply an extracted XP folder, ZIP, or verified original7z (maximum128MiB)")
    except (zipfile.BadZipFile, subprocess.TimeoutExpired, UnicodeError, OSError) as error:
        raise AssetError("Could not read the selected XP assets: " + str(error)) from error
    for record in expected["files"]:
        if _digest(payloads[record["name"]]) != record["sha256"]:
            raise AssetError("XP file does not match the supported version: " + record["name"])
    executable = payloads["Pinball.exe"]
    font = expected["font"]
    font_bytes = executable[font["offset"]:font["offset"] + font["bytes"]]
    if len(font_bytes) != font["bytes"] or _digest(font_bytes) != font["sha256"]:
        raise AssetError("Original XP message-font resource does not match")
    runtime = {"PINBALL.DAT": payloads["PINBALL.DAT"], "PBMSG_FT.bin": font_bytes}
    runtime.update({"SOUNDS/" + name: value for name, value in payloads.items() if name.upper().endswith(".WAV")})
    report = {"schema": 1, "source_kind": kind, "reference": expected["reference"],
              "original_executable_executed": False, "original_executable_bundled": False,
              "reference_executable_sha256": _digest(executable),
              "font_extraction": "Static byte range from hash-verified XP executable; no original implementation imported",
              "runtime_files": [{"path": name, "bytes": len(value), "sha256": _digest(value)}
                                for name, value in sorted(runtime.items())],
              "missing_original_wavs": expected["missing_original_wavs"],
              "rights_note": "Original assets remain separate from the rewrite; their presence in an archive is not a redistribution license."}
    dest.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".pinball-assets-", dir=dest.parent))
    try:
        for name, value in runtime.items():
            target = temporary / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as stream:
                stream.write(value)
        (temporary / "ASSET-PROVENANCE.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if dest.exists() or dest.is_symlink():
            raise AssetError("Asset destination appeared while preparing files; refusing overwrite")
        temporary.rename(dest)
    finally:
        if temporary.exists():
            if temporary.resolve().parent != dest.parent.resolve() or not temporary.name.startswith(".pinball-assets-"):
                raise AssetError("Temporary cleanup target escaped the chosen output directory")
            shutil.rmtree(temporary)
    return report


class _ArchiveRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        _archive_address(new_url)
        return super().redirect_request(request, fp, code, message, headers, new_url)


def _archive_address(url: str) -> None:
    parsed = urllib.parse.urlsplit(url)
    hostname = (parsed.hostname or "").lower()
    if (parsed.scheme != "https" or parsed.username or parsed.password or parsed.fragment
            or parsed.port not in (None, 443)
            or not (hostname == "archive.org" or hostname.endswith(".archive.org"))):
        raise AssetError("Download redirects must remain on HTTPS archive.org hosts")


def download_archive(cache_dir: Path, url: str = ARCHIVE_URL) -> Path:
    """Explicit opt-in download of the user's hash-pinned Archive.org file.

    This function does not run automatically and does not assert usage rights.
    It does not request account credentials, execute downloads, or install tools.
    """
    if url != ARCHIVE_URL:
        raise AssetError("Only the explicitly selected, hash-pinned archive URL is supported")
    _archive_address(url)
    cache_dir = Path(cache_dir)
    if cache_dir.exists() and (_is_link(cache_dir) or not cache_dir.is_dir()):
        raise AssetError("Download cache must be a normal directory")
    cache_dir.mkdir(parents=True, exist_ok=True)
    target = cache_dir / ("xp-reference-" + ARCHIVE_SHA256[:16] + ".7z")
    if target.exists() or target.is_symlink():
        if _is_link(target) or not target.is_file() or _file_digest(target) != ARCHIVE_SHA256:
            raise AssetError("Existing cached archive is different; refusing to overwrite it")
        return target
    lock = cache_dir / ".xp-download.lock"
    try:
        lock_stream = lock.open("x", encoding="ascii")
    except FileExistsError as error:
        raise AssetError("An XP download is already active, or its cache lock needs review") from error
    temporary = None
    try:
        with lock_stream:
            lock_stream.write("Pinball Port XP asset download\n")
        if target.exists():
            raise AssetError("A cached archive appeared before download; run again to validate it")
        request = urllib.request.Request(url, headers={"User-Agent": "PinballPortBuilder/0.0.15"})
        opener = urllib.request.build_opener(_ArchiveRedirect())
        deadline, digest, total = time.monotonic() + 120, hashlib.sha256(), 0
        with opener.open(request, timeout=30) as response, tempfile.NamedTemporaryFile(
                prefix=".xp-download-", suffix=".partial", dir=cache_dir, delete=False) as stream:
            temporary = Path(stream.name)
            _archive_address(response.geturl())
            advertised = response.headers.get("Content-Length")
            if advertised and int(advertised) > MAX_MEMBER_BYTES:
                raise AssetError("XP archive download exceeds the16MiB limit")
            while True:
                if time.monotonic() > deadline:
                    raise AssetError("XP archive download exceeded the120-second limit")
                block = response.read(64 * 1024)
                if not block:
                    break
                total += len(block)
                if total > MAX_MEMBER_BYTES:
                    raise AssetError("XP archive download exceeds the16MiB limit")
                digest.update(block)
                stream.write(block)
            stream.flush()
            os.fsync(stream.fileno())
        if digest.hexdigest() != ARCHIVE_SHA256:
            raise AssetError("Downloaded archive hash differs from the verified XP file; nothing was installed")
        if target.exists() or target.is_symlink():
            raise AssetError("A cached archive appeared during download; refusing overwrite")
        os.replace(temporary, target)
        temporary = None
        return target
    except (OSError, ValueError) as error:
        if isinstance(error, AssetError):
            raise
        raise AssetError("XP archive download failed: " + str(error)) from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        lock.unlink(missing_ok=True)
