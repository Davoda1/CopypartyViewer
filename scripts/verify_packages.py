#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
"""Inspect runtime archives without extracting or executing their contents."""
import argparse
import hashlib
import io
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import tarfile
import zipfile


VERSION = "2.0.0"
NAMES = {
    "windows": f"Copyparty-Viewer-{VERSION}-Windows-x64.zip",
    "deb": f"copyparty-viewer_{VERSION}_amd64.deb",
    "linux": f"Copyparty-Viewer-{VERSION}-Linux-amd64.tar.gz",
}
ROOT_DOCS = ("README.md", "BUILDING.md", "CHANGELOG.md", "CONTRIBUTING.md",
             "SECURITY.md", "LICENSE", "THIRD_PARTY_NOTICES.md")
REQUIRED_NOTICE_HASHES = {
    "MD4C-LICENSE.md": "d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc",
    "Unicode-LICENSE.txt": "e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96",
}
MAX_FILE = 160 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
MAX_MEMBERS = 512


class VerificationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    require(isinstance(name, str) and "\\" not in name and "\x00" not in name,
            "Archive contains an invalid path")
    while name.startswith("./"):
        name = name[2:]
    if name in ("", "."):
        return ""
    path = PurePosixPath(name)
    require(not path.is_absolute() and ".." not in path.parts and
            not re.match(r"^[A-Za-z]:", name), "Archive path escapes its root")
    require(all(ord(c) >= 32 and ord(c) != 127 for c in name),
            "Archive path contains control characters")
    return path.as_posix().rstrip("/")


def read_tar(blob):
    files = {}
    seen = set()
    total = 0
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:*") as archive:
        for item in archive:
            require(len(seen) < MAX_MEMBERS, "Archive has too many members")
            name = safe_name(item.name)
            require(name not in seen, "Archive repeats a member path")
            seen.add(name)
            require(item.uid == 0 and item.gid == 0,
                    "Archive has non-neutral numeric ownership")
            require(item.uname in ("", "root") and item.gname in ("", "root"),
                    "Archive contains a builder's owner name")
            if item.isdir():
                require(item.mode & 0o7777 == 0o755, "Unexpected directory permissions")
                continue
            require(bool(name) and item.isfile(), "Archive contains a link or special file")
            require(0 <= item.size <= MAX_FILE, "Archive member exceeds the size limit")
            total += item.size
            require(total <= MAX_TOTAL, "Archive exceeds the expanded size limit")
            stream = archive.extractfile(item)
            require(stream is not None, "Cannot read archive member")
            data = stream.read(MAX_FILE + 1)
            require(len(data) == item.size, "Archive member is truncated")
            files[name] = (data, item.mode & 0o7777)
    return files


def read_zip(blob):
    files = {}
    seen = set()
    total = 0
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        for item in archive.infolist():
            require(len(seen) < MAX_MEMBERS, "Archive has too many members")
            name = safe_name(item.filename)
            require(name not in seen, "Archive repeats a member path")
            seen.add(name)
            mode = item.external_attr >> 16
            kind = stat.S_IFMT(mode)
            require(kind in (0, stat.S_IFREG, stat.S_IFDIR),
                    "Archive contains a link or special file")
            require(not item.flag_bits & 1, "Encrypted archive members are not allowed")
            if item.is_dir():
                continue
            require(bool(name) and 0 <= item.file_size <= MAX_FILE,
                    "Archive member exceeds the size limit")
            total += item.file_size
            require(total <= MAX_TOTAL, "Archive exceeds the expanded size limit")
            data = archive.read(item)
            require(len(data) == item.file_size, "Archive member is truncated")
            files[name] = (data, mode & 0o7777)
    return files


def read_deb(blob):
    require(blob.startswith(b"!<arch>\n"), "Invalid Debian archive signature")
    offset = 8
    parts = {}
    while offset < len(blob):
        header = blob[offset:offset + 60]
        require(len(header) == 60 and header[58:60] == b"`\n", "Invalid ar member header")
        name = header[:16].decode("ascii").strip().rstrip("/")
        require(name in ("debian-binary", "control.tar.gz", "control.tar.xz",
                         "data.tar.gz", "data.tar.xz") and name not in parts,
                "Unexpected Debian archive member")
        require(int(header[28:34].strip() or b"0") == 0 and
                int(header[34:40].strip() or b"0") == 0,
                "Debian archive has non-neutral ownership")
        size = int(header[48:58])
        require(0 <= size <= MAX_FILE, "Debian member exceeds the size limit")
        offset += 60
        parts[name] = blob[offset:offset + size]
        require(len(parts[name]) == size, "Truncated Debian archive")
        offset += size + (size & 1)
    require(offset == len(blob) and len(parts) == 3 and parts.get("debian-binary") == b"2.0\n",
            "Unexpected Debian archive structure")
    control = next((v for k, v in parts.items() if k.startswith("control.tar.")), None)
    data = next((v for k, v in parts.items() if k.startswith("data.tar.")), None)
    require(control is not None and data is not None, "Debian archive lacks its payload")
    return read_tar(data), read_tar(control)


def binary_privacy(data):
    # These describe general machine paths, not any contributor's machine.
    patterns = (rb"/(?:workspace|home)/[A-Za-z0-9_.-]+/",
                rb"[A-Za-z]:[\\/]Users[\\/][A-Za-z0-9_.-]+[\\/]")
    for pattern in patterns:
        require(not re.search(pattern, data), "Executable contains a builder-specific path")


def pe_imports(data):
    require(len(data) > 256 and data[:2] == b"MZ", "Windows application is not a PE executable")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    require(pe + 264 <= len(data) and data[pe:pe + 4] == b"PE\0\0", "Invalid PE header")
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    require(machine == 0x8664 and struct.unpack_from("<H", data, optional)[0] == 0x20b,
            "Windows application is not x64")
    require(struct.unpack_from("<H", data, optional + 68)[0] == 2,
            "Windows application does not use the GUI subsystem")
    section_table = optional + optional_size
    require(0 < sections < 100 and section_table + sections * 40 <= len(data),
            "Invalid PE section table")
    ranges = []
    for index in range(sections):
        at = section_table + 40 * index
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        require(raw + raw_size <= len(data), "PE section exceeds file bounds")
        ranges.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def position(rva, count=1):
        for start, length, raw, raw_size in ranges:
            if start <= rva < start + length and rva - start + count <= raw_size:
                return raw + rva - start
        raise VerificationError("PE data points outside a section")

    import_rva, import_size = struct.unpack_from("<II", data, optional + 112 + 8)
    require(import_rva and 20 <= import_size < 1024 * 1024, "Invalid PE import directory")
    imports = []
    for index in range(128):
        at = position(import_rva + index * 20, 20)
        descriptor = struct.unpack_from("<IIIII", data, at)
        if descriptor == (0, 0, 0, 0, 0):
            break
        start = position(descriptor[3])
        end = data.find(b"\0", start, start + 261)
        require(end >= start, "Unterminated PE import name")
        imports.append(data[start:end].decode("ascii").lower())
    else:
        raise VerificationError("PE import directory exceeds the limit")
    allowed = {"advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll",
               "dwmapi.dll", "gdi32.dll", "gdiplus.dll", "iphlpapi.dll", "kernel32.dll",
               "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "secur32.dll",
               "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll", "uxtheme.dll",
               "version.dll", "winmm.dll", "winspool.drv", "ws2_32.dll", "normaliz.dll",
               "ucrtbase.dll"}
    require(imports and all(name in allowed or re.fullmatch(r"api-ms-win-[a-z0-9-]+\.dll", name)
                            for name in imports), "Windows executable imports a non-system DLL")
    binary_privacy(data)
    return imports


def elf_imports(data):
    require(len(data) >= 64 and data[:6] == b"\x7fELF\x02\x01" and
            struct.unpack_from("<H", data, 18)[0] == 62, "Linux application is not amd64 ELF")
    section_offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count = struct.unpack_from("<HH", data, 58)
    require(entry_size == 64 and 0 < count < 1024 and section_offset + count * 64 <= len(data),
            "Invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, section_offset + i * 64)
                for i in range(count)]
    imports = []
    for section in sections:
        if section[1] != 6:  # SHT_DYNAMIC
            continue
        require(section[6] < count and section[5] % 16 == 0, "Invalid ELF dynamic section")
        strings = sections[section[6]]
        require(strings[4] + strings[5] <= len(data) and section[4] + section[5] <= len(data),
                "ELF dynamic data exceeds file bounds")
        for offset in range(section[4], section[4] + section[5], 16):
            tag, value = struct.unpack_from("<QQ", data, offset)
            require(tag not in (15, 29), "ELF contains an RPATH or RUNPATH")
            if tag == 1:
                require(value < strings[5], "Invalid ELF dependency name")
                start = strings[4] + value
                end = data.find(b"\0", start, strings[4] + strings[5])
                require(end >= start, "Unterminated ELF dependency name")
                imports.append(data[start:end].decode("ascii"))
    allowed = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
               "libX11.so.6", "libXext.so.6", "libXft.so.2", "libXrender.so.1",
               "libXfixes.so.3", "libXcursor.so.1", "libXinerama.so.1", "libfontconfig.so.1",
               "libfreetype.so.6", "ld-linux-x86-64.so.2"}
    require(imports and set(imports) <= allowed, "Linux executable imports a non-platform library")
    binary_privacy(data)
    return imports


def verify_notices(root):
    for name, expected in REQUIRED_NOTICE_HASHES.items():
        path = root / "packaging/third-party-licenses" / name
        require(path.is_file() and not path.is_symlink() and digest(path.read_bytes()) == expected,
                "A required third-party notice is missing or changed: " + name)


def documentation(root):
    verify_notices(root)
    expected = {name: (root / name).read_bytes() for name in ROOT_DOCS}
    for local, remote in (("docs", "docs"), ("packaging/third-party-licenses", "third-party-licenses")):
        for file in sorted((root / local).rglob("*")):
            if file.is_file():
                require(not file.is_symlink(), "Repository documentation contains a symlink")
                expected[remote + "/" + file.relative_to(root / local).as_posix()] = file.read_bytes()
    return expected


def verify_file_set(files, expected, executable):
    require(set(files) == set(expected) | {executable},
            "Package contents differ from the documented runtime file set")
    for name, data in expected.items():
        require(files[name][0] == data, "Packaged document or resource differs: " + name)


def verify_packages(root, directory, platforms=("windows", "deb", "linux")):
    root, directory = Path(root), Path(directory)
    docs = documentation(root)
    linux_binary = None
    deb_binary = None
    result = {}
    for kind in platforms:
        path = directory / NAMES[kind]
        require(path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= MAX_FILE,
                "Missing or oversized release package: " + NAMES[kind])
        blob = path.read_bytes()
        if kind == "windows":
            files = read_zip(blob)
            verify_file_set(files, docs, "CopypartyViewer.exe")
            pe_imports(files["CopypartyViewer.exe"][0])
        else:
            resources = {
                "share/applications/copyparty-viewer.desktop": (root / "packaging/copyparty-viewer.desktop").read_bytes(),
                "share/pixmaps/copyparty-viewer.png": (root / "assets/copyparty.png").read_bytes(),
            }
            if kind == "linux":
                files = read_tar(blob)
                executable = "copyparty-viewer"
                verify_file_set(files, dict(docs, **resources), executable)
                linux_binary = files[executable][0]
            else:
                files, control = read_deb(blob)
                require(set(control) <= {"control", "md5sums"} and "control" in control,
                        "Debian package contains unexpected maintainer scripts or metadata")
                fields = {}
                for line in control["control"][0].decode("utf-8").splitlines():
                    if ": " in line and not line.startswith(" "):
                        key, value = line.split(": ", 1)
                        fields[key] = value
                require(fields.get("Package") == "copyparty-viewer" and fields.get("Version") == VERSION and
                        fields.get("Architecture") == "amd64", "Debian package identity differs")
                require("ca-certificates" in fields.get("Depends", "") and
                        re.search(r"libc6 \(>= [0-9.]+\)", fields.get("Depends", "")),
                        "Debian runtime dependencies are incomplete")
                expected = {"usr/share/doc/copyparty-viewer/" + n: b for n, b in docs.items()}
                expected.update({"usr/" + n: b for n, b in resources.items()})
                expected["usr/share/doc/copyparty-viewer/copyright"] = (
                    ("Copyparty Viewer " + VERSION + "\n").encode("utf-8") +
                    b"Copyright (C) 2026 davoda1\nLicense: MIT\n\n"
                    b"Third-party components retain their own licenses; see THIRD_PARTY_NOTICES.md\n"
                    b"and the accompanying third-party-licenses directory.\n\n" + (root / "LICENSE").read_bytes())
                executable = "usr/bin/copyparty-viewer"
                verify_file_set(files, expected, executable)
                deb_binary = files[executable][0]
            for name, (_, mode) in files.items():
                require(mode == (0o755 if name == executable else 0o644),
                        "Unexpected runtime permissions: " + name)
            elf_imports(files[executable][0])
        result[NAMES[kind]] = {"sha256": digest(blob), "size": len(blob)}
    if linux_binary is not None and deb_binary is not None:
        require(linux_binary == deb_binary, "Linux TAR and Debian executable bytes differ")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--directory", type=Path, default=Path("dist"))
    parser.add_argument("--platform", choices=("all", "linux", "windows"), default="all")
    args = parser.parse_args()
    kinds = {"all": ("windows", "deb", "linux"), "linux": ("deb", "linux"), "windows": ("windows",)}[args.platform]
    result = verify_packages(args.root, args.directory, kinds)
    for name, metadata in result.items():
        print("PASS", name, metadata["size"], "bytes", metadata["sha256"])


if __name__ == "__main__":
    try:
        main()
    except (VerificationError, OSError, ValueError, struct.error, tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit("Package verification failed: " + str(error))
