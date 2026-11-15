#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
"""Verify the public source tree, pinned dependencies and workflow boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile

from verify_packages import VerificationError, require, verify_notices


PUBLIC_URL = "https://a.ocv.me/pub/"
ROOT_FILES = {".gitignore", ".gitattributes", "CMakeLists.txt", "Makefile", "README.md", "BUILDING.md", "LICENSE",
              "THIRD_PARTY_NOTICES.md", "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md"}
DIRECTORIES = {".github", "src", "tests", "assets", "cmake", "packaging", "docs", "vendor", "scripts"}
REQUIRED_CTEST = {"core-tests", "preview-tests", "document-tests", "rtf-tests", "csv-tests",
                  "text-encoding-tests", "http-fixture", "retro-tests"}
SECRET_PATTERNS = (
    rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b",
    rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b",
    rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----",
    rb"\bAKIA[0-9A-Z]{16}\b",
)


def secret_like(data):
    return any(re.search(pattern, data) for pattern in SECRET_PATTERNS)


def local_path(data):
    return bool(re.search(rb"/(?:workspace|home)/[A-Za-z0-9_.-]+/|[A-Za-z]:[\\/]Users[\\/][A-Za-z0-9_.-]+[\\/]", data))


def source_files(root):
    # A checkout is checked exactly as tracked, including accidentally tracked
    # ignored material. The same command also works before the first commit.
    if (root / ".git").exists():
        result = subprocess.run(["git", "ls-files", "-z", "--cached"], cwd=root,
                                check=True, stdout=subprocess.PIPE)
        names = [name.decode("utf-8") for name in result.stdout.split(b"\0") if name]
        if names:
            return [root / name for name in names]
    files = []
    for child in root.iterdir():
        if child.name in ROOT_FILES:
            files.append(child)
        elif child.name in DIRECTORIES:
            files.extend(path for path in child.rglob("*") if path.is_file() and
                         "__pycache__" not in path.parts and path.suffix != ".pyc")
        elif child.name != ".git" and not child.name.startswith(("build", "dist")):
            raise VerificationError("Unexpected repository root entry: " + child.name)
    return files


def verify_md4c(root, records):
    matches = [item for item in records if item.get("name") == "MD4C"]
    require(len(matches) == 1 and matches[0].get("version") == "0.5.2" and
            matches[0].get("license") == "MIT" and
            matches[0].get("url") == "https://github.com/mity/md4c/tree/release-0.5.2",
            "The pinned MD4C identity is missing or changed")
    upstream = {"md4c.c", "md4c.h", "entity.c", "entity.h", "LICENSE.md"}
    names = upstream | {"README.md", "SHA256SUMS"}
    hashes = matches[0].get("files")
    require(isinstance(hashes, dict) and set(hashes) == {"md4c/" + name for name in names},
            "Pinned MD4C file inventory is incomplete or unexpected")
    directory = root / "vendor/md4c"
    require(directory.is_dir() and not directory.is_symlink() and
            {path.name for path in directory.iterdir()} == names,
            "Vendored MD4C file inventory differs")
    for name, expected in hashes.items():
        path = root / "vendor" / name
        require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected) and
                path.is_file() and not path.is_symlink() and
                hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                "Pinned MD4C bytes differ: " + name)
    sums = {}
    for line in (directory / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        sha, name = line.split()
        require(name not in sums and name in upstream and sha == hashes["md4c/" + name],
                "MD4C checksum manifests disagree")
        sums[name] = sha
    require(set(sums) == upstream, "MD4C checksum list is incomplete")


def verify_dependencies(root):
    records = json.loads((root / "vendor/dependencies.json").read_text(encoding="utf-8"))
    archives = [item for item in records if "file" in item]
    require(len(archives) == 8 and len({item["file"] for item in archives}) == 8,
            "Pinned dependency inventory is incomplete or duplicated")
    sums = {}
    for line in (root / "vendor/sources/SHA256SUMS").read_text(encoding="utf-8").splitlines():
        sha, name = line.split()
        require(name not in sums and re.fullmatch(r"[0-9a-f]{64}", sha), "Invalid dependency checksum list")
        sums[name] = sha
    require(set(sums) == {item["file"] for item in archives}, "Dependency manifests disagree")
    cmake = (root / "cmake/Sources.cmake").read_text(encoding="utf-8")
    pins = dict(re.findall(r'set\(CPV_([a-z]+)_FILE "([^"\n]+)"\)', cmake))
    hashes = dict(re.findall(r'set\(CPV_([a-z]+)_SHA256 "([0-9a-f]{64})"\)', cmake))
    urls = dict(re.findall(r'set\(CPV_([a-z]+)_URL "([^"\n]+)"\)', cmake))
    require(len(pins) == 8 and set(pins) == set(hashes) == set(urls), "CMake dependency pins are incomplete")
    cmake_by_file = {name: (hashes[key], urls[key]) for key, name in pins.items()}
    require({path.name for path in (root / "vendor/sources").iterdir()} == set(sums) | {"SHA256SUMS"},
            "Unexpected dependency source files")
    for item in archives:
        path = root / "vendor/sources" / item["file"]
        require(not path.is_symlink() and path.stat().st_size == item["bytes"],
                "Pinned dependency size differs: " + item["file"])
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        require(sha == item["sha256"] == sums[item["file"]] and
                cmake_by_file.get(item["file"]) == (sha, item["url"]) and
                item["url"].startswith("https://"), "Pinned dependency bytes or source differ: " + item["file"])
    nanosvg = next(item for item in records if item.get("name") == "NanoSVG")
    fltk = next(item for item in archives if item.get("name") == "FLTK")
    require(fltk["version"] == "1.4.5", "The required FLTK version changed")
    with tarfile.open(root / "vendor/sources" / fltk["file"], "r:*") as archive:
        for name, expected in nanosvg["files"].items():
            matches = [item for item in archive if item.name.endswith("/" + name)]
            require(len(matches) == 1 and matches[0].isfile(), "Bundled NanoSVG header is missing")
            require(hashlib.sha256(archive.extractfile(matches[0]).read()).hexdigest() == expected,
                    "Bundled NanoSVG header differs")
    verify_md4c(root, records)
    verify_notices(root)
    return len(archives)


def verify_defaults(root):
    core = (root / "src/core.cpp").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    for key, expected in (("URL", PUBLIC_URL), ("USERNAME", ""), ("PASSWORD", "")):
        fallback = re.findall(r'#\s*define\s+CPV_DEFAULT_' + key + r'\s+"([^"\n]*)"', core)
        configured = re.findall(r'set\(CPV_DEFAULT_' + key + r'\s+"([^"\n]*)"\s+CACHE', cmake)
        require(fallback == configured == [expected], "Source or CMake release default differs: " + key)


def validate_ctest_inventory(data):
    require(isinstance(data, dict) and isinstance(data.get("tests"), list),
            "Configured CTest inventory is unavailable")
    tests = data["tests"]
    require(all(isinstance(test, dict) and isinstance(test.get("name"), str) for test in tests),
            "Configured CTest inventory is malformed")
    by_name = {test["name"]: test for test in tests}
    require(len(by_name) == len(tests) and REQUIRED_CTEST <= set(by_name),
            "A required native regression suite is missing or duplicated")
    for name in REQUIRED_CTEST:
        test = by_name[name]
        command = test.get("command")
        require(isinstance(command, list) and command and
                all(isinstance(argument, str) and argument for argument in command),
                "A required test executable is missing: " + name)
        properties = {item["name"]: item.get("value") for item in test.get("properties", [])}
        require(not properties.get("DISABLED") and not properties.get("WILL_FAIL"),
                "A required suite is disabled or expects failure: " + name)
        if name in ("core-tests", "http-fixture"):
            require("--public-defaults" in command,
                    "Public release defaults are not tested by " + name)
    return len(tests)


def verify_configured_tests(directory):
    result = subprocess.run(["ctest", "--test-dir", str(directory), "--show-only=json-v1"],
                            check=True, stdout=subprocess.PIPE, encoding="utf-8")
    return validate_ctest_inventory(json.loads(result.stdout))


def verify_workflows(root):
    workflows = root / ".github/workflows"
    require({file.name for file in workflows.iterdir()} == {"ci.yml", "publish.yml"},
            "Unexpected workflow inventory")
    for file in workflows.iterdir():
        text = file.read_text(encoding="utf-8")
        uses = re.findall(r"^\s*uses:\s*(\S+)", text, re.MULTILINE)
        require(bool(uses) and all(re.fullmatch(r"(?:actions/(?:checkout|cache/(?:restore|save))|github/codeql-action/(?:init|analyze))@[0-9a-f]{40}", action)
                                  for action in uses), "A workflow action is not fully SHA-pinned")
        require("pull_request_target" not in text and "workflow_run:" not in text,
                "Untrusted or duplicate automatic workflow trigger")
        require("permissions:\n  contents: read\n" in text,
                "Workflow token is not read-only by default")
        require(text.count("persist-credentials: false") == sum(action.startswith("actions/checkout@") for action in uses),
                "Checkout would persist a repository credential")
        require(not re.search(r"^\s*(?:secrets|write-all)\s*:", text, re.MULTILINE),
                "Workflow requests unexpected secret or blanket write permissions")
    ci = (workflows / "ci.yml").read_text(encoding="utf-8")
    triggers = [block.rstrip("\n") for block in
                re.findall(r"^on:\n(.*?)(?=^\S|\Z)", ci, re.MULTILINE | re.DOTALL)]
    expected_triggers = ("  push:\n    branches: [master]\n    tags: ['preflight-*']\n"
                         "  pull_request:\n    branches: [master]")
    require("name: CI\n" in ci and triggers == [expected_triggers],
            "CI must run only on master pushes, master pull requests and preflight-* tag pushes")
    require("contents: write" not in ci and "runs-on: windows-2025" in ci and "runs-on: ubuntu-24.04" in ci,
            "Native CI platforms or permissions changed")
    require(all("name: " + name in ci for name in ("Repository", "Linux", "Windows")),
            "Required CI check names changed")
    require(ci.index("- name: Build pinned dependencies") < ci.index("- name: Initialize C++ security analysis") <
            ci.index("- name: Build the application and regression executables") and
            "languages: c-cpp" in ci and "build-mode: manual" in ci,
            "CodeQL must trace the native application build")
    inventory_commands = re.findall(r"^\s+(python3? scripts/verify_repository\.py --ctest-dir [^\r\n]+)$",
                                    ci, re.MULTILINE)
    test_commands = re.findall(r"^\s+((?:xvfb-run --auto-servernum )?ctest [^\r\n]+)$",
                               ci, re.MULTILINE)
    require(inventory_commands == ["python3 scripts/verify_repository.py --ctest-dir build/ci/app",
                                   "python scripts/verify_repository.py --ctest-dir build/ci/app"] and
            test_commands == ["xvfb-run --auto-servernum ctest --test-dir build/ci/app --output-on-failure",
                              "ctest --test-dir build/ci/app --output-on-failure"],
            "Both native jobs must verify the configured suites and run unfiltered CTest")
    publish = (workflows / "publish.yml").read_text(encoding="utf-8")
    require("  workflow_dispatch:" in publish and
            not re.search(r"^\s*(?:push|pull_request|release|workflow_run):", publish, re.MULTILINE),
            "Publishing must remain an explicit dispatch")
    require(publish.count("contents: write") == 2 and
            "    needs: validate\n" in publish and
            "VALIDATION_RECEIPT: ${{ needs.validate.outputs.receipt }}" in publish and
            "run: python3 scripts/release_gate.py --publish" in publish and
            "github.sha == inputs.commit" in publish,
            "Publishing bypasses the exact-commit validation job")
    # Unpublished draft reads require push access on GitHub. Only these two
    # narrowly scoped jobs may hold it; the first invokes validation mode only.
    for identifier, command in (("validate", "python3 scripts/release_gate.py"),
                                ("publish", "python3 scripts/release_gate.py --publish")):
        match = re.search(r"^  " + identifier + r":\n(.*?)(?=^  [a-z][a-z_-]*:\n|\Z)",
                          publish, re.MULTILINE | re.DOTALL)
        require(match is not None, "A required publication job is missing")
        block = match.group(1)
        require("    permissions:\n      contents: write\n      actions: read\n      security-events: read\n" in block and
                re.findall(r"^\s+run:\s*(.+)$", block, re.MULTILINE) == [command] and
                "github.sha == inputs.commit" in block,
                "A publication job has unexpected permissions or commands")


def verify_tree(root):
    root = root.resolve()
    files = source_files(root)
    paths = {file.relative_to(root).as_posix() for file in files}
    require(ROOT_FILES <= paths, "Required root documentation or build files are missing")
    for file in files:
        name = file.relative_to(root).as_posix()
        first = name.split("/", 1)[0]
        require(first in DIRECTORIES or name in ROOT_FILES, "Unexpected tracked repository path: " + name)
        require(file.is_file() and not file.is_symlink(), "Source tree contains a link or non-file: " + name)
        parts = file.relative_to(root).parts
        require(not any(part in {"settings.json", ".env", "__pycache__"} or
                        part.startswith(".env.") for part in parts) and
                file.suffix.lower() not in {".exe", ".dll", ".so", ".deb", ".pyc", ".pem", ".key", ".pfx", ".p12", ".log"},
                "A local profile, secret or build artifact is in the source tree: " + name)
        # Upstream archives and license notices retain their original identities.
        if name.startswith(("vendor/sources/", "packaging/third-party-licenses/")):
            continue
        data = file.read_bytes()
        require(not secret_like(data), "Credential-like material found in " + name)
        require(not local_path(data), "Builder-specific path found in " + name)
        if name.startswith(("src/", "tests/")) and file.suffix in {".cpp", ".hpp", ".py"}:
            identifiers = re.findall(rb"SPDX-License-Identifier:\s*([^\r\n]+)", data[:512])
            require(all(identifier == b"MIT" for identifier in identifiers),
                    "Project source retains a conflicting license identifier: " + name)
    license_text = (root / "LICENSE").read_text(encoding="utf-8")
    require("MIT License" in license_text and "2026 davoda1" in license_text and
            "Permission is hereby granted, free of charge" in license_text,
            "The project MIT license is incomplete")
    ignore = (root / ".gitignore").read_text(encoding="utf-8")
    require(all(item in ignore for item in ("/build*/", "/dist*/", "settings.json", ".env", "*.pem", "*.key")),
            "Local build and secret exclusions are incomplete")
    attributes = (root / ".gitattributes").read_text(encoding="utf-8")
    require("* text=auto eol=lf" in attributes and
            all("*." + extension + " binary" in attributes for extension in
                ("tar.gz", "tar.xz", "zip", "deb", "png", "jpg", "jpeg", "ico", "gif", "webp", "bmp", "rgba")),
            "Cross-platform text or binary checkout rules are incomplete")
    readme = (root / "README.md").read_text(encoding="utf-8")
    images = re.findall(r"!\[[^\]]*\]\((docs/images/[^)]+)\)", readme)
    require(len(set(images)) >= 3 and "docs/images/banner.png" in images,
            "README must include its banner and actual platform screenshots")
    for image in images:
        require(image in paths, "README references a missing image")
        data = (root / image).read_bytes()
        extension = Path(image).suffix.lower()
        require((extension == ".png" and data.startswith(b"\x89PNG\r\n\x1a\n")) or
                (extension in (".jpg", ".jpeg") and data.startswith(b"\xff\xd8\xff") and data.endswith(b"\xff\xd9")),
                "README references an invalid screenshot or banner")
    verify_defaults(root)
    dependencies = verify_dependencies(root)
    verify_workflows(root)
    return len(files), dependencies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--ctest-dir", type=Path,
                        help="Verify the built CI test inventory instead of the source tree")
    args = parser.parse_args()
    if args.ctest_dir is not None:
        tests = verify_configured_tests(args.ctest_dir)
        print(f"PASS: {tests} configured tests; all required suites and public-default checks enabled")
        return
    files, dependencies = verify_tree(args.root)
    print(f"PASS: {files} source files, {dependencies} pinned archives, MD4C files and notices, public defaults and workflow boundaries")


if __name__ == "__main__":
    try:
        main()
    except (VerificationError, OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit("Repository verification failed: " + str(error))
