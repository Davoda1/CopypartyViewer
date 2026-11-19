# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
"""Offline regression checks for archive and publication trust boundaries."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import struct
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import release_gate as gate
import verify_packages as packages
import verify_repository as repository


def tar_bytes(records):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w:gz") as archive:
        for name, data, kind, uid, mode in records:
            item = tarfile.TarInfo(name)
            item.size = len(data)
            item.type = kind
            item.uid = uid
            item.mode = mode
            if kind == tarfile.SYMTYPE:
                item.linkname = "../outside"
            archive.addfile(item, io.BytesIO(data) if kind == tarfile.REGTYPE else None)
    return output.getvalue()


def synthetic_elf(library=b"libc.so.6", dynamic_tag=1):
    data = bytearray(512)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", data, 18, 62)
    struct.pack_into("<Q", data, 40, 64)
    struct.pack_into("<HH", data, 58, 64, 3)
    strings = b"\0" + library + b"\0"
    data[300:300 + len(strings)] = strings
    struct.pack_into("<IIQQQQIIQQ", data, 128, 0, 3, 0, 0, 300, len(strings), 0, 0, 1, 0)
    struct.pack_into("<IIQQQQIIQQ", data, 192, 0, 6, 0, 0, 400, 32, 1, 0, 8, 16)
    struct.pack_into("<QQQQ", data, 400, dynamic_tag, 1, 0, 0)
    return bytes(data)


class ArchiveTests(unittest.TestCase):
    def test_safe_relative_path(self):
        self.assertEqual(packages.safe_name("./docs/images/example.png"), "docs/images/example.png")

    def test_path_traversal_variants(self):
        for name in ("../outside", "/absolute", "a/../../outside", "C:/outside", "a\\..\\outside", "a\x00b", "a\nb"):
            with self.subTest(name=repr(name)), self.assertRaises(packages.VerificationError):
                packages.safe_name(name)

    def test_regular_tar(self):
        blob = tar_bytes([("README.md", b"example", tarfile.REGTYPE, 0, 0o644)])
        self.assertEqual(packages.read_tar(blob), {"README.md": (b"example", 0o644)})

    def test_tar_rejects_links_and_owner_paths(self):
        for record in (("link", b"", tarfile.SYMTYPE, 0, 0o777),
                       ("file", b"x", tarfile.REGTYPE, 1000, 0o644),
                       ("../file", b"x", tarfile.REGTYPE, 0, 0o644)):
            with self.subTest(record=record[:1]), self.assertRaises(packages.VerificationError):
                packages.read_tar(tar_bytes([record]))

    def test_tar_duplicate_normalized_paths(self):
        blob = tar_bytes([("./file", b"one", tarfile.REGTYPE, 0, 0o644),
                          ("file", b"two", tarfile.REGTYPE, 0, 0o644)])
        with self.assertRaises(packages.VerificationError):
            packages.read_tar(blob)

    def test_tar_expanded_size_limit(self):
        blob = tar_bytes([("file", b"12345", tarfile.REGTYPE, 0, 0o644)])
        with patch.object(packages, "MAX_FILE", 4), self.assertRaises(packages.VerificationError):
            packages.read_tar(blob)

    def test_zip_rejects_symlink(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as archive:
            item = zipfile.ZipInfo("link")
            item.create_system = 3
            item.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(item, "../outside")
        with self.assertRaises(packages.VerificationError):
            packages.read_zip(output.getvalue())

    def test_zip_duplicate_normalized_paths(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as archive:
            archive.writestr("./file", "one")
            archive.writestr("file", "two")
        with self.assertRaises(packages.VerificationError):
            packages.read_zip(output.getvalue())

    def test_runtime_file_set_rejects_launcher_dll_and_settings(self):
        good = {"README.md": (b"current", 0o644), "CopypartyViewer.exe": (b"binary", 0o755)}
        packages.verify_file_set(good, {"README.md": b"current"}, "CopypartyViewer.exe")
        for name in ("launch.cmd", "helper.dll", "settings.json"):
            files = dict(good, **{name: (b"unwanted", 0o644)})
            with self.subTest(name=name), self.assertRaises(packages.VerificationError):
                packages.verify_file_set(files, {"README.md": b"current"}, "CopypartyViewer.exe")

    def test_stale_packaged_document_rejected(self):
        with self.assertRaises(packages.VerificationError):
            packages.verify_file_set({"README.md": (b"stale", 0o644), "app": (b"binary", 0o755)},
                                     {"README.md": b"current"}, "app")

    def test_deb_rejects_truncation_and_non_ar(self):
        for blob in (b"not a package", b"!<arch>\ntruncated"):
            with self.subTest(blob=blob), self.assertRaises(packages.VerificationError):
                packages.read_deb(blob)

    def test_elf_platform_dependencies(self):
        self.assertEqual(packages.elf_imports(synthetic_elf()), ["libc.so.6"])
        self.assertEqual(packages.elf_imports(synthetic_elf(b"ld-linux-x86-64.so.2")), ["ld-linux-x86-64.so.2"])

    def test_elf_rejects_application_library_and_runtime_paths(self):
        for blob in (synthetic_elf(b"libstdc++.so.6"), synthetic_elf(b"libcurl.so.4"), synthetic_elf(dynamic_tag=29)):
            with self.assertRaises(packages.VerificationError):
                packages.elf_imports(blob)

    def test_elf_rejects_invalid_section_bounds(self):
        data = bytearray(synthetic_elf())
        struct.pack_into("<Q", data, 40, 4096)
        with self.assertRaises(packages.VerificationError):
            packages.elf_imports(data)

    def test_pe_rejects_non_executable(self):
        with self.assertRaises(packages.VerificationError):
            packages.pe_imports(b"#!/bin/sh\n")


class RepositoryTests(unittest.TestCase):
    def test_secret_signature_recognized_without_real_credentials(self):
        example = ("gh" + "p_" + "x" * 36).encode()
        self.assertTrue(repository.secret_like(example))
        self.assertFalse(repository.secret_like(b"fixture-user and fixture-password"))

    def test_generic_builder_paths(self):
        self.assertTrue(repository.local_path(("/wor" + "kspace/example/project/source.cpp").encode()))
        self.assertTrue(repository.local_path(("C:\\Us" + "ers\\example\\source.cpp").encode()))
        self.assertFalse(repository.local_path(b"~/.config/copyparty-viewer/settings.json"))

    def test_public_defaults_and_blank_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            definitions = {"URL": repository.PUBLIC_URL, "USERNAME": "", "PASSWORD": ""}
            (root / "src/core.cpp").write_text("\n".join('# define CPV_DEFAULT_' + key + ' "' + value + '"'
                                                         for key, value in definitions.items()))
            cmake = "\n".join('set(CPV_DEFAULT_' + key + ' "' + value + '" CACHE STRING "")'
                              for key, value in definitions.items())
            (root / "CMakeLists.txt").write_text(cmake)
            repository.verify_defaults(root)
            (root / "CMakeLists.txt").write_text(cmake.replace('CPV_DEFAULT_USERNAME ""', 'CPV_DEFAULT_USERNAME "fixture-user"'))
            with self.assertRaises(packages.VerificationError):
                repository.verify_defaults(root)

    def test_md4c_pin_and_modified_parser_rejection(self):
        source = Path(__file__).resolve().parents[1]
        records = json.loads((source / "vendor/dependencies.json").read_text(encoding="utf-8"))
        repository.verify_md4c(source, records)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / "vendor/md4c", root / "vendor/md4c")
            for name in ("md4c.c", "entity.c", "LICENSE.md"):
                path = root / "vendor/md4c" / name
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.subTest(name=name), self.assertRaises(packages.VerificationError):
                    repository.verify_md4c(root, records)
                path.write_bytes(original)

    def test_md4c_missing_pin_and_duplicate_identity_rejection(self):
        source = Path(__file__).resolve().parents[1]
        records = json.loads((source / "vendor/dependencies.json").read_text(encoding="utf-8"))
        incomplete = copy.deepcopy(records)
        md4c = next(item for item in incomplete if item.get("name") == "MD4C")
        del md4c["files"]["md4c/md4c.h"]
        for manifest in (incomplete, records + [md4c],
                         [item for item in records if item.get("name") != "MD4C"]):
            with self.assertRaises(packages.VerificationError):
                repository.verify_md4c(source, manifest)

    def test_required_upstream_notices_are_intact_and_packaged(self):
        source = Path(__file__).resolve().parents[1]
        expected = packages.documentation(source)
        names = ("MD4C-LICENSE.md", "Unicode-LICENSE.txt")
        for name in names:
            self.assertIn("third-party-licenses/" + name, expected)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "packaging/third-party-licenses"
            directory.mkdir(parents=True)
            for name in names:
                (directory / name).write_bytes(expected["third-party-licenses/" + name])
            packages.verify_notices(root)
            for name in names:
                path = directory / name
                original = path.read_bytes()
                path.write_bytes(original[:len(original) // 2])
                with self.subTest(name=name, state="truncated"), self.assertRaises(packages.VerificationError):
                    packages.verify_notices(root)
                path.unlink()
                with self.subTest(name=name, state="missing"), self.assertRaises(packages.VerificationError):
                    packages.verify_notices(root)
                path.write_bytes(original)

    def test_current_workflow_policy_and_unpinned_rejection(self):
        source = Path(__file__).resolve().parents[1]
        repository.verify_workflows(source)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / ".github/workflows", root / ".github/workflows")
            path = root / ".github/workflows/ci.yml"
            text = path.read_text()
            path.write_text(text.replace("actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1", "actions/checkout@v7"))
            with self.assertRaises(packages.VerificationError):
                repository.verify_workflows(root)

    def test_missing_suite_inventory_or_filtered_ctest_rejected(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / ".github/workflows", root / ".github/workflows")
            path = root / ".github/workflows/ci.yml"
            original = path.read_text()
            for before, after in (("python scripts/verify_repository.py --ctest-dir build/ci/app", "echo skipped"),
                                  ("ctest --test-dir build/ci/app --output-on-failure",
                                   "ctest --test-dir build/ci/app --output-on-failure -R core-tests")):
                path.write_text(original.replace(before, after))
                with self.subTest(before=before), self.assertRaises(packages.VerificationError):
                    repository.verify_workflows(root)

    def test_ci_rejects_broadened_or_missing_preflight_triggers(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / ".github/workflows", root / ".github/workflows")
            path = root / ".github/workflows/ci.yml"
            original = path.read_text()
            mutations = [original.replace("tags: ['preflight-*']", "tags: " + tags)
                         for tags in ("['*']", "['v*']", "['preflight-*', 'v*']", "['preflight-**']")]
            mutations.extend((original.replace("    tags: ['preflight-*']\n", ""),
                              original.replace("branches: [master]", "branches: [master, topic]", 1),
                              original.replace("  pull_request:\n    branches: [master]\n", ""),
                              original.replace("on:\n", "on:\n  workflow_dispatch:\n", 1),
                              original.replace("on:\n", "on:\n  release:\n", 1)))
            for index, changed in enumerate(mutations):
                path.write_text(changed)
                with self.subTest(mutation=index), self.assertRaises(packages.VerificationError):
                    repository.verify_workflows(root)

    def test_publication_rejects_automatic_tag_or_release_triggers(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / ".github/workflows", root / ".github/workflows")
            path = root / ".github/workflows/publish.yml"
            original = path.read_text()
            for event in ("  push:\n    tags: ['preflight-*']\n", "  release:\n"):
                path.write_text(original.replace("on:\n", "on:\n" + event, 1))
                with self.subTest(event=event), self.assertRaises(packages.VerificationError):
                    repository.verify_workflows(root)


def configured_ctest_inventory():
    names = ("core-tests", "preview-tests", "document-tests", "rtf-tests", "csv-tests",
             "text-encoding-tests", "http-fixture", "retro-tests")
    return {"tests": [{"name": name,
                       "command": [name] + (["--public-defaults"] if name in ("core-tests", "http-fixture") else []),
                       "properties": []} for name in names]}


class ConfiguredTests(unittest.TestCase):
    def test_all_required_suites_and_additional_tests_are_accepted(self):
        inventory = configured_ctest_inventory()
        repository.validate_ctest_inventory(inventory)
        inventory["tests"].append({"name": "additional-regression", "command": ["additional-regression"]})
        repository.validate_ctest_inventory(inventory)

    def test_omitted_disabled_or_unbuilt_suite_is_rejected(self):
        original = configured_ctest_inventory()
        for index, test in enumerate(original["tests"]):
            for state in ("missing", "disabled", "expects-failure", "unbuilt"):
                inventory = copy.deepcopy(original)
                if state == "missing":
                    del inventory["tests"][index]
                elif state == "unbuilt":
                    del inventory["tests"][index]["command"]
                else:
                    inventory["tests"][index]["properties"].append(
                        {"name": "DISABLED" if state == "disabled" else "WILL_FAIL", "value": True})
                with self.subTest(suite=test["name"], state=state), self.assertRaises(packages.VerificationError):
                    repository.validate_ctest_inventory(inventory)

    def test_public_defaults_must_run_in_core_and_http_suites(self):
        for name in ("core-tests", "http-fixture"):
            inventory = configured_ctest_inventory()
            next(test for test in inventory["tests"] if test["name"] == name)["command"].remove("--public-defaults")
            with self.subTest(suite=name), self.assertRaises(packages.VerificationError):
                repository.validate_ctest_inventory(inventory)


SHA = "a" * 40
HASHES = {name: str(index) * 64 for index, name in enumerate(packages.NAMES.values(), 1)}


class FakeAPI:
    def __init__(self):
        self.calls = []
        self.run = {"id": 7, "workflow_id": 2, "name": "CI", "path": ".github/workflows/ci.yml",
                    "event": "push", "head_branch": "master", "head_sha": SHA,
                    "repository": {"full_name": gate.REPOSITORY},
                    "head_repository": {"full_name": gate.REPOSITORY}, "run_attempt": 1,
                    "status": "completed", "conclusion": "success"}
        self.assets = [{"id": i, "name": name, "state": "uploaded", "size": 10,
                        "digest": "sha256:" + HASHES[name], "created_at": "2026-01-01T00:00:00Z",
                        "updated_at": "2026-01-01T00:00:00Z"}
                       for i, name in enumerate(packages.NAMES.values(), 1)]
        self.release = {"id": 8, "draft": True, "prerelease": False, "tag_name": gate.TAG,
                        "target_commitish": SHA, "name": packages.VERSION, "body": "Release notes", "assets": self.assets}
        self.master = SHA
        self.tag = SHA
        self.alerts = []
        self.analysis = {"id": 9, "ref": "refs/heads/master", "tool": {"name": "CodeQL"},
                         "category": "/language:c-cpp", "analysis_key": ".github/workflows/ci.yml:linux",
                         "commit_sha": SHA, "error": "", "warning": "", "rules_count": 200}

    def json(self, suffix, method="GET", body=None):
        self.calls.append((suffix, method, body))
        if suffix == "actions/workflows/ci.yml":
            return {"id": 2, "path": ".github/workflows/ci.yml", "state": "active"}
        if suffix == "actions/runs/7":
            return copy.deepcopy(self.run)
        if suffix.startswith("actions/runs/7/attempts/"):
            return {"total_count": 3, "jobs": [{"name": name, "status": "completed", "conclusion": "success", "head_sha": SHA}
                                                for name in gate.CHECKS]}
        if suffix == "git/ref/heads/master":
            return {"object": {"type": "commit", "sha": self.master}}
        if suffix == "git/ref/tags/" + gate.TAG:
            return {"object": {"type": "commit", "sha": self.tag}}
        if suffix == "releases/8":
            if method == "PATCH":
                self.release["draft"] = False
            return copy.deepcopy(self.release)
        if suffix.startswith("releases/8/assets?"):
            return copy.deepcopy(self.assets)
        if suffix.startswith("code-scanning/analyses?"):
            return [copy.deepcopy(self.analysis)]
        if suffix.startswith("code-scanning/alerts?"):
            return copy.deepcopy(self.alerts)
        raise AssertionError("Unexpected mock API path")

    def download(self, asset, destination):
        destination.write_bytes(b"mock-package")


class ReleaseTests(unittest.TestCase):
    def test_exact_input_contract(self):
        gate.validate_inputs(SHA, 7, 8, HASHES)
        for sha, run, release, expected in (("master", 7, 8, HASHES), (SHA, 0, 8, HASHES),
                                           (SHA, True, 8, HASHES), (SHA, 7, 8, {})):
            with self.assertRaises(packages.VerificationError):
                gate.validate_inputs(sha, run, release, expected)

    def test_dispatch_context(self):
        environment = {"GITHUB_ACTIONS": "true", "GITHUB_REPOSITORY": gate.REPOSITORY,
                       "GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_REF": "refs/heads/master", "GITHUB_SHA": SHA}
        gate.check_workflow_environment(SHA, environment)
        for key, value in (("GITHUB_REF", "refs/heads/topic"), ("GITHUB_EVENT_NAME", "pull_request"),
                           ("GITHUB_SHA", "b" * 40), ("GITHUB_REPOSITORY", "example/CopypartyViewer")):
            with self.subTest(key=key), self.assertRaises(packages.VerificationError):
                gate.check_workflow_environment(SHA, dict(environment, **{key: value}))

    def test_successful_exact_run(self):
        self.assertEqual(gate.wait_for_ci(FakeAPI(), SHA, 7, 0)["run_attempt"], 1)

    def test_failed_cancelled_and_incomplete_ci_block(self):
        for status, conclusion in (("completed", "failure"), ("completed", "cancelled"),
                                   ("completed", "neutral"), ("queued", None)):
            api = FakeAPI()
            api.run.update(status=status, conclusion=conclusion)
            with self.subTest(status=status, conclusion=conclusion), self.assertRaises(packages.VerificationError):
                gate.wait_for_ci(api, SHA, 7, 0)
            self.assertFalse(any(method != "GET" for _, method, _ in api.calls))

    def test_other_revision_branch_workflow_or_repository_block(self):
        for key, value in (("head_sha", "b" * 40), ("head_branch", "topic"), ("event", "pull_request"),
                           ("head_branch", "preflight-" + packages.VERSION + "-test"), ("event", "workflow_dispatch"),
                           ("workflow_id", 3), ("repository", {"full_name": "example/other"})):
            api = FakeAPI()
            api.run[key] = value
            with self.subTest(key=key), self.assertRaises(packages.VerificationError):
                gate.wait_for_ci(api, SHA, 7, 0)

    def test_missing_or_skipped_required_job_block(self):
        api = FakeAPI()
        real_json = api.json
        def mock_json(suffix, *args):
            result = real_json(suffix, *args)
            if "/jobs?" in suffix:
                result["jobs"][0]["conclusion"] = "skipped"
            return result
        api.json = mock_json
        with self.assertRaises(packages.VerificationError):
            gate.wait_for_ci(api, SHA, 7, 0)

    def test_exact_asset_inventory_and_digests(self):
        api = FakeAPI()
        self.assertEqual(len(gate.asset_snapshot(api.assets, HASHES)), 3)
        extra = dict(api.assets[0], id=20, name="extra.txt")
        for assets in (api.assets[:2], api.assets + [extra], [api.assets[0], api.assets[0], api.assets[2]]):
            with self.assertRaises(packages.VerificationError):
                gate.asset_snapshot(assets, HASHES)

    def test_asset_replacement_or_wrong_digest_block(self):
        for key, value in (("digest", "sha256:" + "f" * 64), ("state", "starter"),
                           ("size", packages.MAX_FILE + 1), ("updated_at", None)):
            assets = copy.deepcopy(FakeAPI().assets)
            assets[0][key] = value
            with self.subTest(key=key), self.assertRaises(packages.VerificationError):
                gate.asset_snapshot(assets, HASHES)

    def test_changed_master_or_tag_block(self):
        for field in ("master", "tag"):
            api = FakeAPI()
            setattr(api, field, "b" * 40)
            with self.subTest(field=field), self.assertRaises(packages.VerificationError):
                gate.snapshot(api, SHA, 7, 8, HASHES)

    def test_already_public_or_wrong_target_block(self):
        for key, value in (("draft", False), ("target_commitish", "master"), ("tag_name", "v9.0.0"), ("prerelease", True)):
            api = FakeAPI()
            api.release[key] = value
            with self.subTest(key=key), self.assertRaises(packages.VerificationError):
                gate.snapshot(api, SHA, 7, 8, HASHES)

    def test_codeql_requires_current_complete_analysis_and_no_open_alerts(self):
        api = FakeAPI()
        self.assertEqual(gate.codeql_analysis(api, SHA)["open_alerts"], 0)
        api.alerts = [{"number": 1, "state": "open"}]
        with self.assertRaises(packages.VerificationError):
            gate.codeql_analysis(api, SHA)
        for key, value in (("commit_sha", "b" * 40), ("error", "analysis failed"),
                           ("warning", "incomplete extraction"), ("rules_count", 0)):
            api = FakeAPI()
            api.analysis[key] = value
            with self.subTest(key=key), self.assertRaises(packages.VerificationError):
                gate.codeql_analysis(api, SHA)

    def test_changed_asset_id_changes_receipt(self):
        api = FakeAPI()
        before = gate.snapshot_digest(gate.snapshot(api, SHA, 7, 8, HASHES))
        api.assets[0]["id"] += 10
        after = gate.snapshot_digest(gate.snapshot(api, SHA, 7, 8, HASHES))
        self.assertNotEqual(before, after)

    def test_changed_ci_attempt_changes_receipt(self):
        api = FakeAPI()
        before = gate.snapshot_digest(gate.snapshot(api, SHA, 7, 8, HASHES))
        api.run["run_attempt"] = 2
        after = gate.snapshot_digest(gate.snapshot(api, SHA, 7, 8, HASHES))
        self.assertNotEqual(before, after)

    def test_readonly_validation_and_package_failure(self):
        client = gate.GitHub("synthetic-token")
        with self.assertRaises(packages.VerificationError):
            client.api_request("/repos/" + gate.REPOSITORY + "/releases/8", "PATCH",
                               {"draft": False, "make_latest": "true"})
        client = gate.GitHub("synthetic-token", allow_publish=True)
        with self.assertRaises(packages.VerificationError):
            client.api_request("/repos/" + gate.REPOSITORY + "/releases/8", "DELETE")
        api = FakeAPI()
        result = {name: {"sha256": sha} for name, sha in HASHES.items()}
        with patch.object(gate, "verify_packages", return_value=result):
            gate.validate_draft(api, Path("."), SHA, 7, 8, HASHES)
        self.assertTrue(all(method == "GET" for _, method, _ in api.calls))
        with patch.object(gate, "verify_packages", side_effect=packages.VerificationError("bad package")):
            with self.assertRaises(packages.VerificationError):
                gate.validate_draft(api, Path("."), SHA, 7, 8, HASHES)

    def test_publication_requires_matching_readonly_receipt(self):
        api = FakeAPI()
        result = {name: {"sha256": sha} for name, sha in HASHES.items()}
        with patch.object(gate, "verify_packages", return_value=result):
            with self.assertRaises(packages.VerificationError):
                gate.publish(api, Path("."), SHA, 7, 8, HASHES, "0" * 64)
        self.assertTrue(all(method == "GET" for _, method, _ in api.calls))

    def test_only_expected_publication_mutation(self):
        api = FakeAPI()
        receipt = gate.snapshot_digest(gate.snapshot(api, SHA, 7, 8, HASHES))
        result = {name: {"sha256": sha} for name, sha in HASHES.items()}
        with patch.object(gate, "verify_packages", return_value=result):
            url = gate.publish(api, Path("."), SHA, 7, 8, HASHES, receipt)
        self.assertTrue(url.endswith("/releases/tag/" + gate.TAG))
        self.assertEqual([(path, method, body) for path, method, body in api.calls if method != "GET"],
                         [("releases/8", "PATCH", {"draft": False, "make_latest": "true"})])

    def test_token_is_not_forwarded_to_asset_storage(self):
        class Response(io.BytesIO):
            def __init__(self, data, code=200, headers=None):
                super().__init__(data)
                self.code = code
                self.headers = headers or {}
        class Opener:
            def __init__(self, host):
                self.requests = []
                self.host = host
            def open(self, request, timeout):
                self.requests.append(request)
                if len(self.requests) == 1:
                    return Response(b"", 302, {"Location": "https://" + self.host + "/asset?signature=synthetic"})
                return Response(b"package")
        api = gate.GitHub("synthetic-token")
        api.opener = Opener("release-assets.githubusercontent.com")
        asset = {"id": 1, "size": 7, "digest": "sha256:" + hashlib.sha256(b"package").hexdigest()}
        with tempfile.TemporaryDirectory() as temporary:
            api.download(asset, Path(temporary) / "package")
        self.assertIn("Authorization", api.opener.requests[0].headers)
        self.assertNotIn("Authorization", api.opener.requests[1].headers)

    def test_foreign_asset_redirect_is_rejected_before_following(self):
        class Response(io.BytesIO):
            code = 302
            headers = {"Location": "https://example.test/asset"}
        class Opener:
            def __init__(self):
                self.count = 0
            def open(self, request, timeout):
                self.count += 1
                return Response(b"")
        api = gate.GitHub("synthetic-token")
        api.opener = Opener()
        with tempfile.TemporaryDirectory() as temporary, self.assertRaises(packages.VerificationError):
            api.download({"id": 1, "size": 7, "digest": "sha256:" + "1" * 64}, Path(temporary) / "package")
        self.assertEqual(api.opener.count, 1)


if __name__ == "__main__":
    unittest.main()
