"""Compare complete GitHub run attempts, including matrix wall time and summed job time."""

import argparse
from datetime import datetime
import json
from pathlib import Path
import subprocess


def duration(started, completed):
    if not started or not completed:
        return None
    return (
        datetime.fromisoformat(completed.replace("Z", "+00:00"))
        - datetime.fromisoformat(started.replace("Z", "+00:00"))
    ).total_seconds()


def report(run, jobs):
    completed = [job for job in jobs if job.get("completed_at")]
    return {
        "run_id": run["id"],
        "attempt": run["run_attempt"],
        "commit": run["head_sha"],
        "status": run["status"],
        "conclusion": run["conclusion"],
        "url": run["html_url"],
        "elapsed_seconds": duration(
            run["run_started_at"], max((job["completed_at"] for job in completed), default=None)
        ),
        "summed_job_seconds": sum(
            duration(job["started_at"], job["completed_at"]) or 0 for job in completed
        ),
        "jobs": [
            {
                "name": job["name"],
                "conclusion": job["conclusion"],
                "seconds": duration(job["started_at"], job["completed_at"]),
            }
            for job in jobs
        ],
    }


def api(path):
    return json.loads(subprocess.check_output(["gh", "api", path], text=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=int)
    parser.add_argument("--repo")
    parser.add_argument("--attempt", type=int)
    parser.add_argument("--output", type=Path, default=Path("build/ci-reports"))
    args = parser.parse_args()
    repository = (
        args.repo
        or json.loads(
            subprocess.check_output(["gh", "repo", "view", "--json", "nameWithOwner"], text=True)
        )["nameWithOwner"]
    )
    path = f"repos/{repository}/actions/runs/{args.run}"
    run = api(path + (f"/attempts/{args.attempt}" if args.attempt else ""))
    jobs = api(path + f"/attempts/{run['run_attempt']}/jobs?per_page=100")["jobs"]
    if run["status"] != "completed":
        raise SystemExit("Wait until the run attempt completes before recording timing evidence")
    data = report(run, jobs)
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / f"run-{args.run}-attempt-{run['run_attempt']}.json"
    target.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
