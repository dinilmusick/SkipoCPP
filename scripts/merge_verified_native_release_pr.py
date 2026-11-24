#!/usr/bin/env python3
"""Fail-closed merger for release-please version PRs after exact-head independent CI success.

Runs exclusively from default-branch GitHub Actions with a short-lived GitHub
App token. Never checks out or executes the pull request's content.
"""
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.github.com"


def github(path, token, method="GET", body=None):
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": "Bearer " + token,
        "User-Agent": "ULTRST-release-please-verified-merge/1",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    data = None if body is None else json.dumps(body).encode()
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(API + path, headers=headers, data=data, method=method)
    try:
        with urllib.request.urlopen(request, timeout=25) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        raise RuntimeError("GitHub API rejected %s %s: HTTP %s" %
                           (method, path.split("?")[0], exc.code)) from None


def release_file_allowed(path):
    return path in ("version.txt", ".release-please-manifest.json", "CHANGELOG.md")


def eligible_pr(pr, repository, main_sha):
    if pr.get("state") != "open" or pr.get("draft"):
        return False
    if pr.get("base", {}).get("ref") != "main" or pr["base"].get("sha") != main_sha:
        return False
    head = pr.get("head", {})
    if not head.get("ref", "").startswith("release-please--"):
        return False
    if head.get("repo") is None or head["repo"].get("full_name") != repository:
        return False
    user = pr.get("user", {})
    if user.get("type") != "Bot" or not user.get("login", "").endswith("[bot]"):
        return False
    if not any(mark in (pr.get("body") or "").lower() for mark in ("release-please", "release please")):
        return False
    return True


def checks_pass(runs, head_sha, head_ref, required):
    for name in required:
        relevant = [run for run in runs
                    if run.get("name") == name
                    and run.get("head_sha") == head_sha
                    and run.get("head_branch") == head_ref
                    and run.get("event") == "pull_request"]
        # Missing, duplicate, queued or failed checks must never count as success.
        if len(relevant) != 1 or relevant[0].get("status") != "completed" or relevant[0].get("conclusion") != "success":
            return False
    return True


def merge_one(repository, token, required):
    base = "/repos/" + repository
    ref = github(base + "/git/ref/heads/main", token)
    main_sha = ref["object"]["sha"]
    pulls = github(base + "/pulls?state=open&base=main&per_page=100", token)
    candidates = [pr for pr in pulls if eligible_pr(pr, repository, main_sha)]
    if len(candidates) > 1:
        raise RuntimeError("Multiple release-please version PRs: refusing to choose between them")
    if not candidates:
        print("No eligible release-plz pull request")
        return
    pr = candidates[0]
    num = pr["number"]
    head = pr["head"]
    sha = head["sha"]
    if not isinstance(sha, str) or len(sha) != 40:
        raise RuntimeError("Invalid immutable PR head SHA")

    # Reject edits to source code, CI, credentials and build/release scripts.
    files = github(base + "/pulls/%s/files?per_page=100" % num, token)
    if not files or len(files) >= 100 or not all(
            release_file_allowed(entry["filename"]) for entry in files):
        print("Release PR contains unexpected files; human review required")
        return

    # Check actual PR-triggered runs (a GITHUB_TOKEN-created release PR lacks them).
    query = urllib.parse.urlencode({"head_sha": sha, "event": "pull_request", "per_page": 100})
    result = github(base + "/actions/runs?" + query, token)
    runs = result.get("workflow_runs", [])
    if result.get("total_count", 101) > 100 or not checks_pass(runs, sha, head["ref"], required):
        print("Missing, ambiguous, failed or pending exact-head PR checks; no merge")
        return

    # TOCTOU: compare the exact PR head/base again immediately before REST merge.
    latest = github(base + "/pulls/%s" % num, token)
    current_main = github(base + "/git/ref/heads/main", token)["object"]["sha"]
    if (current_main != main_sha or latest["head"]["sha"] != sha
            or not eligible_pr(latest, repository, current_main)
            or latest.get("mergeable") is not True):
        print("PR or main changed since verification; no merge")
        return
    merged = github(base + "/pulls/%s/merge" % num, token, "PUT", {
        "sha": sha, "merge_method": "squash",
        "commit_title": "chore(release): merge CI-verified release-please version PR",
    })
    if merged.get("merged") is not True:
        raise RuntimeError("GitHub did not certify a successful merge")
    print("Merged verified release-please version PR #%s at %s" % (num, sha))


def main():
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    token = os.environ.get("GH_TOKEN", "")
    required = json.loads(os.environ.get("REQUIRED_RELEASE_CHECKS", "[]"))
    if (not repo.startswith("dinilmusick/") or not token or not required
            or not all(isinstance(x, str) and x for x in required)):
        raise RuntimeError("Missing trusted repository, GitHub App token or required CI checks")
    if os.environ.get("GITHUB_REF") != "refs/heads/main":
        raise RuntimeError("Auto-merger must run from main")
    merge_one(repo, token, required)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("::error::Verified release PR auto-merge failed: %s" % exc, file=sys.stderr)
        sys.exit(1)
