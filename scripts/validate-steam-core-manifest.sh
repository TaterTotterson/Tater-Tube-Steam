#!/usr/bin/env bash
# Validate the pinned Steam libretro inventory without building the cores.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${1:-${REPO_ROOT}/packaging/steam/core-manifest.tsv}"
APPROVED="${2:-${REPO_ROOT}/packaging/steam/runtime-notices/APPROVED-CORES.txt}"
NOTICE="${REPO_ROOT}/packaging/steam/runtime-notices/retroarch-cores.txt"

python3 - "${MANIFEST}" "${APPROVED}" "${NOTICE}" <<'PY'
from pathlib import Path
import re
import sys

manifest_path, approved_path, notice_path = map(Path, sys.argv[1:])
for path in (manifest_path, approved_path, notice_path):
    if not path.is_file():
        raise SystemExit(f"Missing Steam core inventory file: {path}")

rows = []
for line_number, raw in enumerate(
        manifest_path.read_text(encoding="utf-8").splitlines(), 1):
    if not raw or raw.startswith("#"):
        continue
    fields = raw.split("|")
    if len(fields) != 9:
        raise SystemExit(
            f"{manifest_path}:{line_number}: expected 9 pipe-delimited fields, "
            f"found {len(fields)}")
    rows.append((line_number, fields))

if not rows:
    raise SystemExit("The Steam core manifest is empty")

allowed_groups = {"standard", "replacement", "arcade"}
restricted = {
    "genesis_plus_gx_libretro.so",
    "picodrive_libretro.so",
    "snes9x_libretro.so",
    "fbneo_libretro.so",
    "mame2000_libretro.so",
    "mame2003_libretro.so",
    "mame2003_plus_libretro.so",
}
seen_ids = set()
seen_files = set()
seen_sources = set()

for line_number, fields in rows:
    group, core_id, filename, repository, revision, license_id, build_dir, \
        makefile, _make_args = fields
    prefix = f"{manifest_path}:{line_number}"
    if group not in allowed_groups:
        raise SystemExit(f"{prefix}: unknown build group {group!r}")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", core_id):
        raise SystemExit(f"{prefix}: invalid core id {core_id!r}")
    if not re.fullmatch(r"[A-Za-z0-9_.+-]+_libretro\.so", filename):
        raise SystemExit(f"{prefix}: invalid core filename {filename!r}")
    if not re.fullmatch(r"https://github\.com/[^/]+/[^/]+\.git", repository):
        raise SystemExit(f"{prefix}: repository must be a GitHub HTTPS .git URL")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise SystemExit(f"{prefix}: revision must be a full lowercase commit SHA")
    if not license_id or not build_dir or not makefile:
        raise SystemExit(f"{prefix}: license, build directory, and makefile are required")
    if core_id in seen_ids:
        raise SystemExit(f"{prefix}: duplicate core id {core_id!r}")
    if filename in seen_files:
        raise SystemExit(f"{prefix}: duplicate core filename {filename!r}")
    source = (repository, revision)
    if source in seen_sources:
        raise SystemExit(f"{prefix}: duplicate repository revision")
    seen_ids.add(core_id)
    seen_files.add(filename)
    seen_sources.add(source)

if restricted & seen_files:
    names = ", ".join(sorted(restricted & seen_files))
    raise SystemExit(f"Restricted cores are present in the Steam manifest: {names}")

approved = [
    line.strip()
    for line in approved_path.read_text(encoding="utf-8").splitlines()
    if line.strip()
]
if approved != sorted(set(approved)):
    raise SystemExit("APPROVED-CORES.txt must be sorted and contain no duplicates")
if set(approved) != seen_files:
    missing = sorted(seen_files - set(approved))
    extra = sorted(set(approved) - seen_files)
    raise SystemExit(
        "Approved core list does not match the manifest"
        + (f"; missing: {', '.join(missing)}" if missing else "")
        + (f"; extra: {', '.join(extra)}" if extra else ""))

notice = notice_path.read_text(encoding="utf-8")
undocumented = sorted(filename for filename in approved if filename not in notice)
if undocumented:
    raise SystemExit(
        "Core notice is missing approved filenames: " + ", ".join(undocumented))

groups = {}
for _line_number, fields in rows:
    groups[fields[0]] = groups.get(fields[0], 0) + 1
print(
    f"Steam core manifest validation passed: {len(rows)} cores "
    f"({', '.join(f'{name}={groups.get(name, 0)}' for name in sorted(allowed_groups))})"
)
PY
