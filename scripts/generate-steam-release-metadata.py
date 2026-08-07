#!/usr/bin/env python3
"""Generate the desktop update manifest and human-readable GitHub release notes."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess


def git_output(repo_root: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True)
    parser.add_argument("--release-version", required=True)
    parser.add_argument("--app-version", required=True)
    parser.add_argument("--archive-name", required=True)
    parser.add_argument("--template", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--notes", type=pathlib.Path, required=True)
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path.cwd())
    args = parser.parse_args()

    template = json.loads(args.template.read_text(encoding="utf-8"))
    highlights = [
        str(note).strip() for note in template.get("notes", []) if str(note).strip()
    ]

    release_base = (
        f"https://github.com/{args.repository}/releases/download/"
        f"{args.release_version}"
    )
    manifest = {
        "version": args.release_version,
        "app_version": args.app_version,
        "channel": "main",
        "repo": f"https://github.com/{args.repository}",
        "manifest_url": (
            f"https://github.com/{args.repository}/releases/latest/download/"
            "update-manifest.json"
        ),
        "binary_archive": f"{release_base}/{args.archive_name}",
        "source_archive": (
            f"https://github.com/{args.repository}/archive/refs/tags/"
            f"{args.release_version}.tar.gz"
        ),
        "notes": highlights,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    previous_tag = git_output(
        args.repo_root, "describe", "--tags", "--abbrev=0", "HEAD^"
    )
    revision_range = f"{previous_tag}..HEAD" if previous_tag else "HEAD"
    commits = git_output(
        args.repo_root,
        "log",
        "--pretty=format:- %s (`%h`)",
        revision_range,
    )

    lines = ["## What's changed", ""]
    if highlights:
        lines.extend(f"- {highlight}" for highlight in highlights)
    elif commits:
        lines.extend(commits.splitlines())
    else:
        lines.append("- Maintenance and packaging updates.")

    download_base = (
        f"https://github.com/{args.repository}/releases/download/"
        f"{args.release_version}"
    )
    lines.extend(
        [
            "",
            "## Downloads",
            "",
            "| Package | File |",
            "|---|---|",
            (
                "| Steam Deck / Linux x86_64 | "
                f"[`{args.archive_name}`]({download_base}/{args.archive_name}) |"
            ),
            (
                "| One-click installer | "
                f"[`Install-Tater-Tube.desktop`]({download_base}/Install-Tater-Tube.desktop) |"
            ),
            (
                "| Shell installer | "
                f"[`install-steam-deck.sh`]({download_base}/install-steam-deck.sh) |"
            ),
            (
                "| Checksums | "
                f"[`SHA256SUMS.txt`]({download_base}/SHA256SUMS.txt) |"
            ),
            "",
            "## Install or update",
            "",
            "New installations can use the one-click desktop installer. Once this "
            "version is installed, future releases can be installed inside Tater Tube "
            "from **Settings > System > Check For Updates**.",
        ]
    )

    if commits and highlights:
        heading = f"Commits since {previous_tag}" if previous_tag else "Included commits"
        lines.extend(
            [
                "",
                "<details>",
                f"<summary>{heading}</summary>",
                "",
                *commits.splitlines(),
                "",
                "</details>",
            ]
        )

    args.notes.parent.mkdir(parents=True, exist_ok=True)
    args.notes.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
