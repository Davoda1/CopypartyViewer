#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
"""Validate a draft against its exact CI run and manually verified asset hashes.

The only mutation implemented here is publishing an existing, verified draft.
Asset upload, replacement, deletion and tag creation are intentionally absent.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

from verify_packages import MAX_FILE, NAMES, VERSION, VerificationError, digest, require, verify_packages


REPOSITORY = "Davoda1/CopypartyViewer"
TAG = "v" + VERSION
API = "https://api.github.com"
API_VERSION = "2026-03-10"
CHECKS = {"Repository", "Linux", "Windows"}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


class GitHub:
    def __init__(self, token, allow_publish=False):
        require(bool(token), "GITHUB_TOKEN is required")
        self.token = token
        self.allow_publish = allow_publish
        self.opener = urllib.request.build_opener(NoRedirect)

    def api_request(self, path, method="GET", body=None, accept="application/vnd.github+json"):
        require(path.startswith("/repos/" + REPOSITORY + "/"), "API request is outside the repository")
        require(method == "GET" or
                (self.allow_publish and method == "PATCH" and
                 re.fullmatch(r"/repos/" + re.escape(REPOSITORY) + r"/releases/[1-9][0-9]*", path) and
                 body == {"draft": False, "make_latest": "true"}),
                "This helper cannot mutate GitHub without the explicit publish mode")
        headers = {"Accept": accept, "Authorization": "Bearer " + self.token,
                   "X-GitHub-Api-Version": API_VERSION, "User-Agent": "CopypartyViewer-release-gate"}
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(API + path, data=payload, headers=headers, method=method)
        try:
            return self.opener.open(request, timeout=45)
        except urllib.error.HTTPError as error:
            if method == "GET" and accept == "application/octet-stream" and error.code in (301, 302, 303, 307, 308):
                return error
            raise VerificationError("GitHub API request failed (HTTP " + str(error.code) + ")") from None

    def json(self, suffix, method="GET", body=None):
        with self.api_request("/repos/" + REPOSITORY + "/" + suffix, method, body) as response:
            data = response.read(2 * 1024 * 1024 + 1)
            require(len(data) <= 2 * 1024 * 1024, "GitHub JSON response exceeds the limit")
            return json.loads(data)

    def download(self, asset, destination):
        response = self.api_request("/repos/" + REPOSITORY + "/releases/assets/" + str(asset["id"]),
                                    accept="application/octet-stream")
        if response.code in (301, 302, 303, 307, 308):
            location = response.headers.get("Location", "")
            response.close()
            parsed = urllib.parse.urlsplit(location)
            # Never forward the repository token to the object-storage redirect.
            require(parsed.scheme == "https" and parsed.hostname in
                    {"release-assets.githubusercontent.com", "objects.githubusercontent.com"} and
                    parsed.username is None and parsed.password is None and parsed.port in (None, 443),
                    "Asset redirect is outside GitHub's download hosts")
            request = urllib.request.Request(location, headers={"User-Agent": "CopypartyViewer-release-gate"})
            try:
                response = self.opener.open(request, timeout=45)
            except urllib.error.HTTPError as error:
                raise VerificationError("Asset download failed (HTTP " + str(error.code) + ")") from None
        count = 0
        sha = hashlib.sha256()
        deadline = time.monotonic() + 180
        with response, destination.open("xb") as output:
            require(response.code == 200, "Unexpected asset download response")
            while True:
                require(time.monotonic() < deadline, "Asset download timed out")
                block = response.read(1024 * 1024)
                if not block:
                    break
                count += len(block)
                require(count <= asset["size"] and count <= MAX_FILE, "Asset download exceeds its declared size")
                sha.update(block)
                output.write(block)
        require(count == asset["size"] and "sha256:" + sha.hexdigest() == asset["digest"],
                "Downloaded asset does not match GitHub's size and digest")


def validate_inputs(commit, run_id, release_id, expected):
    require(bool(re.fullmatch(r"[0-9a-f]{40}", commit)), "Commit must be a full lowercase SHA")
    require(all(isinstance(value, int) and not isinstance(value, bool) and value > 0
                for value in (run_id, release_id)), "Run and release IDs must be positive integers")
    require(set(expected) == set(NAMES.values()) and
            all(re.fullmatch(r"[0-9a-f]{64}", value) for value in expected.values()),
            "Exactly three lowercase SHA-256 values are required")


def check_workflow_environment(commit, environment):
    require(environment.get("GITHUB_ACTIONS") == "true" and
            environment.get("GITHUB_REPOSITORY") == REPOSITORY and
            environment.get("GITHUB_EVENT_NAME") == "workflow_dispatch" and
            environment.get("GITHUB_REF") == "refs/heads/master" and
            environment.get("GITHUB_SHA") == commit,
            "Publication must be dispatched from the specified current master revision")


def validate_run(run, workflow, commit, run_id):
    require(run.get("id") == run_id and run.get("workflow_id") == workflow.get("id") and
            workflow.get("path") == ".github/workflows/ci.yml" and workflow.get("state") == "active" and
            run.get("name") == "CI" and run.get("path") == ".github/workflows/ci.yml" and
            run.get("event") == "push" and run.get("head_branch") == "master" and
            run.get("head_sha") == commit and
            run.get("repository", {}).get("full_name") == REPOSITORY and
            run.get("head_repository", {}).get("full_name") == REPOSITORY,
            "CI run does not identify this repository's exact master commit and workflow")
    require(isinstance(run.get("run_attempt"), int) and run["run_attempt"] >= 1, "CI run attempt is missing")


def wait_for_ci(api, commit, run_id, maximum_seconds):
    require(0 <= maximum_seconds <= 1800, "CI wait exceeds the bounded limit")
    deadline = time.monotonic() + maximum_seconds
    workflow = api.json("actions/workflows/ci.yml")
    announced = False
    while True:
        run = api.json("actions/runs/" + str(run_id))
        validate_run(run, workflow, commit, run_id)
        if run.get("status") == "completed":
            require(run.get("conclusion") == "success", "The specified CI run did not succeed")
            jobs = api.json("actions/runs/" + str(run_id) + "/attempts/" + str(run["run_attempt"]) + "/jobs?per_page=100")
            records = jobs.get("jobs", [])
            require(jobs.get("total_count") == len(records) == len(CHECKS) and
                    {job.get("name") for job in records} == CHECKS and
                    all(job.get("status") == "completed" and job.get("conclusion") == "success" and
                        job.get("head_sha") == commit for job in records),
                    "The required native and repository CI jobs did not all succeed")
            return run
        require(run.get("status") in ("queued", "in_progress", "requested", "waiting", "pending"),
                "CI run has an unexpected status")
        require(time.monotonic() < deadline, "Timed out waiting for the specified CI run")
        if not announced:
            print("Waiting for the specified master CI run; publication remains blocked until all checks succeed.", flush=True)
            announced = True
        time.sleep(min(15, max(0, deadline - time.monotonic())))


def codeql_analysis(api, commit, maximum_seconds=0):
    require(0 <= maximum_seconds <= 120, "CodeQL wait exceeds the bounded limit")
    query = "ref=refs%2Fheads%2Fmaster&tool_name=CodeQL&per_page=100"
    deadline = time.monotonic() + maximum_seconds
    while True:
        analyses = api.json("code-scanning/analyses?" + query)
        require(isinstance(analyses, list), "CodeQL analysis metadata is unavailable")
        candidates = [item for item in analyses
                      if item.get("tool", {}).get("name") == "CodeQL" and
                      item.get("category", "").rstrip("/") == "/language:c-cpp" and
                      item.get("analysis_key") == ".github/workflows/ci.yml:linux" and
                      item.get("ref") == "refs/heads/master"]
        # Analyses are returned newest first; an older success cannot stand in
        # for the result of this exact revision.
        if candidates and candidates[0].get("commit_sha") == commit:
            item = candidates[0]
            require(item.get("error") == "" and item.get("warning") == "" and
                    isinstance(item.get("rules_count"), int) and item["rules_count"] > 0 and
                    isinstance(item.get("id"), int), "CodeQL analysis is incomplete or reports a warning/error")
            alerts = api.json("code-scanning/alerts?state=open&" + query)
            require(isinstance(alerts, list) and len(alerts) == 0,
                    "Open CodeQL findings on master must be reviewed and resolved before publication")
            return {"id": item["id"], "commit": commit, "category": item["category"],
                    "rules_count": item["rules_count"], "open_alerts": 0}
        require(time.monotonic() < deadline, "No completed CodeQL analysis exists for the exact master commit")
        time.sleep(min(10, max(0, deadline - time.monotonic())))


def resolve_tag(api):
    item = api.json("git/ref/tags/" + TAG).get("object", {})
    for _ in range(5):
        if item.get("type") == "commit":
            return item.get("sha")
        require(item.get("type") == "tag" and re.fullmatch(r"[0-9a-f]{40}", item.get("sha", "")),
                "Release tag does not resolve to a commit")
        item = api.json("git/tags/" + item["sha"]).get("object", {})
    raise VerificationError("Release tag nesting exceeds the limit")


def asset_snapshot(assets, expected):
    require(isinstance(assets, list) and len(assets) == 3 and
            {asset.get("name") for asset in assets} == set(expected),
            "Draft must have exactly the three approved binary assets")
    records = []
    identifiers = set()
    for asset in assets:
        name = asset["name"]
        require(isinstance(asset.get("id"), int) and asset["id"] > 0 and asset["id"] not in identifiers,
                "Asset identity is invalid or repeated")
        identifiers.add(asset["id"])
        require(asset.get("state") == "uploaded" and isinstance(asset.get("size"), int) and
                0 < asset["size"] <= MAX_FILE and asset.get("digest") == "sha256:" + expected[name],
                "Asset metadata does not match the locally approved digest")
        require(isinstance(asset.get("created_at"), str) and isinstance(asset.get("updated_at"), str),
                "Asset timestamps are missing")
        records.append({key: asset[key] for key in ("id", "name", "size", "digest", "created_at", "updated_at")})
    return sorted(records, key=lambda item: item["name"])


def snapshot(api, commit, run_id, release_id, expected, draft=True):
    run = wait_for_ci(api, commit, run_id, 0)
    analysis = codeql_analysis(api, commit)
    branch = api.json("git/ref/heads/master").get("object", {})
    require(branch.get("type") == "commit" and branch.get("sha") == commit,
            "Master changed after the release was prepared")
    require(resolve_tag(api) == commit, "Release tag does not identify the tested commit")
    release = api.json("releases/" + str(release_id))
    require(release.get("id") == release_id and release.get("tag_name") == TAG and
            release.get("draft") is draft and release.get("prerelease") is False and
            release.get("target_commitish") == commit,
            "Release is not the expected exact-commit draft or published release")
    assets = asset_snapshot(api.json("releases/" + str(release_id) + "/assets?per_page=100"), expected)
    require(asset_snapshot(release.get("assets"), expected) == assets,
            "Release assets changed while metadata was read")
    return {"commit": commit, "run_id": run_id, "run_attempt": run["run_attempt"], "codeql": analysis,
            "release_id": release_id, "tag": TAG, "assets": assets,
            "release_name": release.get("name"), "release_created": release.get("created_at"),
            "release_updated": release.get("updated_at"),
            "release_notes_sha256": digest((release.get("body") or "").encode("utf-8"))}


def snapshot_digest(record):
    return digest(json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8"))


def validate_draft(api, root, commit, run_id, release_id, expected):
    before = snapshot(api, commit, run_id, release_id, expected)
    with tempfile.TemporaryDirectory(prefix="cpv-release-") as temporary:
        directory = Path(temporary)
        for asset in before["assets"]:
            api.download(asset, directory / asset["name"])
        checked = verify_packages(root, directory)
        require({name: item["sha256"] for name, item in checked.items()} == expected,
                "Package verification does not match the approved hashes")
    after = snapshot(api, commit, run_id, release_id, expected)
    require(before == after, "Master, CI attempt, tag or release assets changed during validation")
    return after


def publish(api, root, commit, run_id, release_id, expected, receipt):
    require(bool(re.fullmatch(r"[0-9a-f]{64}", receipt)), "The validation job's receipt is required")
    record = validate_draft(api, root, commit, run_id, release_id, expected)
    require(snapshot_digest(record) == receipt, "Draft differs from the read-only validation job's snapshot")
    # Recheck immediately before the sole mutating request. Branch/tag protection
    # and immutable releases provide the repository-side concurrency safeguards.
    require(snapshot(api, commit, run_id, release_id, expected) == record,
            "Release state changed before publication")
    response = api.json("releases/" + str(release_id), "PATCH", {"draft": False, "make_latest": "true"})
    require(response.get("id") == release_id and response.get("draft") is False and
            response.get("tag_name") == TAG and asset_snapshot(response.get("assets"), expected) == record["assets"],
            "Publication response does not match the approved release")
    after = snapshot(api, commit, run_id, release_id, expected, draft=False)
    require(after["assets"] == record["assets"] and after["run_attempt"] == record["run_attempt"] and
            after["release_notes_sha256"] == record["release_notes_sha256"],
            "Release changed during publication; inspect the release before further actions")
    return "https://github.com/" + REPOSITORY + "/releases/tag/" + TAG


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    commit = os.environ.get("RELEASE_COMMIT", "")
    run_id = int(os.environ.get("RELEASE_CI_RUN", "0"))
    release_id = int(os.environ.get("RELEASE_ID", "0"))
    expected = {NAMES["windows"]: os.environ.get("RELEASE_WINDOWS_SHA256", ""),
                NAMES["deb"]: os.environ.get("RELEASE_DEB_SHA256", ""),
                NAMES["linux"]: os.environ.get("RELEASE_LINUX_SHA256", "")}
    validate_inputs(commit, run_id, release_id, expected)
    check_workflow_environment(commit, os.environ)
    api = GitHub(os.environ.get("GITHUB_TOKEN", ""), allow_publish=args.publish)
    root = Path(__file__).resolve().parents[1]
    if args.publish:
        print("Published " + publish(api, root, commit, run_id, release_id, expected,
                                     os.environ.get("VALIDATION_RECEIPT", "")))
    else:
        wait_for_ci(api, commit, run_id, 1800)
        codeql_analysis(api, commit, 120)
        record = validate_draft(api, root, commit, run_id, release_id, expected)
        receipt = snapshot_digest(record)
        output = os.environ.get("GITHUB_OUTPUT")
        require(bool(output), "GitHub output file is required")
        with open(output, "a", encoding="utf-8") as stream:
            stream.write("receipt=" + receipt + "\n")
        print("PASS: exact master CI run, zero open CodeQL findings, tag, three asset digests and package contents verified.")


if __name__ == "__main__":
    try:
        main()
    except (VerificationError, OSError, ValueError, urllib.error.URLError) as error:
        # Never echo response bodies, authenticated URLs or token-bearing headers.
        message = str(error) if isinstance(error, VerificationError) else type(error).__name__
        raise SystemExit("Release gate failed: " + message)
