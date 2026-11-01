#!/usr/bin/env python3
"""Start, watch and check the signed TestFlight build (.github/workflows/testflight.yml).

    python scripts/testflight.py status                  # latest run
    python scripts/testflight.py start [--dry-run] [--create-signing] [--again]
    python scripts/testflight.py watch [RUN_ID]          # poll until done, then report

start builds origin/main's head, and refuses unless the `testflight` environment is
locked to main with its secrets in place and, for an upload, `python scripts/gate.py`
passed that commit on this PC. --dry-run signs and checks the IPA without
uploading. --create-signing registers the bundle id and creates the profile (first
build) or renews an expired certificate. --again rebuilds a commit that already shipped.

A run passes once Apple has finished processing the upload, a few minutes before
testers can install it.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = "KaiC5504/OneFourSeven"
WORKFLOW = "testflight.yml"
ENVIRONMENT = "testflight"
SECRETS = {
    "APP_STORE_CONNECT_ISSUER_ID",
    "APP_STORE_CONNECT_KEY_IDENTIFIER",
    "APP_STORE_CONNECT_PRIVATE_KEY",
    "CERT_KEY_B64",
    "SIGNING_NAME",
}
# The workflow's run-name starts with one of these.
UPLOAD = "TestFlight upload"
DRY_RUN = "TestFlight dry run"
FIELDS = "databaseId,displayTitle,status,conclusion,headSha,headBranch,event,url"
# A background watch outlives short network trouble; this many failed polls ends it.
WATCH_RETRIES = 5

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")


def run(*args: str) -> str:
    result = subprocess.run(list(args), capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise SystemExit(f"{' '.join(args[:3])} failed: {result.stderr.strip()}")
    return result.stdout


def git(*args: str) -> str:
    return run("git", "-C", str(ROOT), *args).strip()


def runs(limit: int = 20) -> list[dict]:
    return json.loads(run("gh", "run", "list", "-R", REPO, "--workflow", WORKFLOW,
                          "--limit", str(limit), "--json", FIELDS))


def view(run_id: int | str, fields: str = FIELDS) -> dict:
    return json.loads(run("gh", "run", "view", str(run_id), "-R", REPO, "--json", fields))


def last_shipped() -> dict | None:
    """The newest upload run on main that passed, i.e. a build Apple accepted."""
    return next((r for r in runs() if r["displayTitle"].startswith(UPLOAD)
                 and r["headBranch"] == "main" and r.get("event") == "workflow_dispatch"
                 and r["conclusion"] == "success"), None)


def failed(run_: dict) -> bool:
    return run_["status"] == "completed" and run_["conclusion"] != "success"


def report(run_: dict) -> None:
    state = run_["conclusion"] or run_["status"]
    print(f"run     {run_['databaseId']}  {state}")
    print(f"title   {run_['displayTitle']}")
    print(f"url     {run_['url']}")
    for job in view(run_["databaseId"], "jobs")["jobs"]:
        print(f"\n{job['name']}: {job['conclusion'] or job['status']}")
        for step in job["steps"]:
            step_state = step["conclusion"] or step["status"]
            marker = ("x" if step_state in {"failure", "cancelled", "timed_out"}
                      else "-" if step_state in {"skipped", "pending", "queued"} else "+")
            print(f"  {marker} {step['name']}: {step_state}")
    if failed(run_):
        log = subprocess.run(["gh", "run", "view", str(run_["databaseId"]), "-R", REPO,
                              "--log-failed"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace").stdout
        lines = [line for line in log.splitlines() if line.strip()]
        if lines:
            print(f"\n--- failed steps: last {min(len(lines), 60)} lines ---")
            print("\n".join(lines[-60:]))
        print(f"\nIf only Apple processing failed or timed out: gh run rerun "
              f"{run_['databaseId']} -R {REPO} --failed")


def environment_problems() -> list[str]:
    """The secrets are only as safe as this environment's lock to main."""
    env = json.loads(run("gh", "api", f"repos/{REPO}/environments/{ENVIRONMENT}"))
    policy = env.get("deployment_branch_policy") or {}
    problems = []
    if not policy.get("custom_branch_policies") or policy.get("protected_branches"):
        problems.append(f"environment {ENVIRONMENT} isn't limited to chosen branches")
    else:
        branches = json.loads(run("gh", "api",
                                  f"repos/{REPO}/environments/{ENVIRONMENT}/deployment-branch-policies"))
        names = sorted(p["name"] for p in branches.get("branch_policies", []))
        if names != ["main"]:
            problems.append(f"environment {ENVIRONMENT} allows {names}, not just main")
    names = {s["name"] for s in json.loads(run("gh", "secret", "list", "--env", ENVIRONMENT,
                                                "-R", REPO, "--json", "name"))}
    problems += [f"environment {ENVIRONMENT} has no secret {n}" for n in sorted(SECRETS - names)]
    return problems


def gate_problem(sha: str) -> str | None:
    """gate.py records the commit of its last full pass on a clean tree; anything else is untested."""
    record = ROOT / git("rev-parse", "--git-path", "gate-passed")
    passed = record.read_text(encoding="utf-8").strip() if record.exists() else ""
    if passed == sha:
        return None
    last = f"the last gate pass here was {passed[:8]}" if passed else "no gate pass is recorded here"
    return f"origin/main is {sha[:8]} but {last}: run python scripts/gate.py on main first"


def dispatched_run(output: str, known: set[int], sha: str) -> dict | None:
    """The run `gh workflow run` just made. Its title is the bare workflow name until
    GitHub evaluates run-name, so wait for the sha to show up in it."""
    match = re.search(r"/actions/runs/(\d+)", output)
    for _ in range(40):
        try:
            if match:
                found = view(match.group(1))
                if sha in found["displayTitle"]:
                    return found
            else:
                new = [r for r in runs(10)
                       if r["databaseId"] not in known and sha in r["displayTitle"]]
                if new:
                    return new[0]
        except SystemExit:
            pass
        time.sleep(3)
    return None


def cmd_status() -> int:
    latest = runs(1)
    if not latest:
        print("No TestFlight runs yet.")
        return 0
    report(latest[0])
    return 1 if failed(latest[0]) else 0


def cmd_start(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="testflight.py start")
    parser.add_argument("--dry-run", action="store_true", help="sign and check, upload nothing")
    parser.add_argument("--create-signing", action="store_true",
                        help="register the bundle id / create the profile or certificate")
    parser.add_argument("--again", action="store_true", help="rebuild a commit that already shipped")
    args = parser.parse_args(argv)

    git("fetch", "-q", "origin", "main")
    sha = git("rev-parse", "origin/main")
    untested = None if args.dry_run else gate_problem(sha)
    problems = ([untested] if untested else []) + environment_problems()
    if problems:
        print("Not starting:\n" + "\n".join(f"  - {p}" for p in problems))
        return 1
    shipped = last_shipped()
    if not args.dry_run and not args.again and shipped and shipped["headSha"] == sha:
        print(f"{sha[:8]} already shipped in run {shipped['databaseId']}. "
              "Use --again to build it under a new number.")
        return 1
    before = runs(10)
    busy = [r for r in before if r["status"] != "completed"]
    if busy:
        print(f"run {busy[0]['databaseId']} is still {busy[0]['status']}: {busy[0]['url']}")
        return 1

    output = run("gh", "workflow", "run", WORKFLOW, "-R", REPO, "--ref", "main",
                 "-f", f"sha={sha}",
                 "-f", f"upload={'false' if args.dry_run else 'true'}",
                 "-f", f"create_signing={'true' if args.create_signing else 'false'}")
    new = dispatched_run(output, {r["databaseId"] for r in before}, sha)
    if not new:
        print("Dispatched, but the run hasn't appeared yet; see `testflight.py status`.")
        return 0
    want = DRY_RUN if args.dry_run else UPLOAD
    if not new["displayTitle"].startswith(want):
        run("gh", "run", "cancel", str(new["databaseId"]), "-R", REPO)
        print(f"run {new['databaseId']} came up as {new['displayTitle']!r}, not {want!r}; "
              "cancelled it.")
        return 1
    print(f"started run {new['databaseId']} on {sha[:8]}"
          + (" (dry run, no upload)" if args.dry_run else ""))
    print(new["url"])
    return 0


def cmd_watch(run_id: str | None) -> int:
    if run_id is None:
        latest = runs(1)
        if not latest:
            print("No TestFlight runs yet.")
            return 0
        run_id = str(latest[0]["databaseId"])
    seen, misses = None, 0
    while True:
        try:
            current = view(run_id)
            misses = 0
        except SystemExit as error:
            misses += 1
            if misses >= WATCH_RETRIES:
                print(f"Lost contact with GitHub ({error}). The run may still be going; "
                      "check `testflight.py status`.")
                return 3
            time.sleep(30)
            continue
        state = current["conclusion"] or current["status"]
        if state != seen:
            print(f"  {state}", flush=True)
            seen = state
        if current["status"] == "completed":
            print()
            report(current)
            return 0 if current["conclusion"] == "success" else 1
        time.sleep(30)


def main() -> int:
    command = sys.argv[1] if len(sys.argv) > 1 else "status"
    rest = sys.argv[2:]
    if command == "status":
        return cmd_status()
    if command == "start":
        return cmd_start(rest)
    if command == "watch":
        return cmd_watch(rest[0] if rest else None)
    print(__doc__)
    return 0 if command in {"-h", "--help", "help"} else 2


if __name__ == "__main__":
    raise SystemExit(main())
