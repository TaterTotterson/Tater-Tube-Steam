#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
setup_file="${repo_root}/scripts/lib/pi-setup.sh"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

awk -v out="$tmp_dir" '
function begin(marker_name) {
    marker = marker_name
    count++
    file = sprintf("%s/helper-%02d-%s.sh", out, count, marker)
}

$0 ~ /<<.?HELPER.?$/ {
    begin("HELPER")
    next
}

$0 ~ /<<.?STOP_HELPER.?$/ {
    begin("STOP_HELPER")
    next
}

$0 ~ /<<.?IR_SETUP.?$/ {
    begin("IR_SETUP")
    next
}

$0 ~ /<<.?IR_LEARN.?$/ {
    begin("IR_LEARN")
    next
}

file != "" && $0 == marker {
    file = ""
    marker = ""
    next
}

file != "" {
    print > file
}

END {
    if (file != "")
        exit 2
    count_file = out "/count"
    print count + 0 > count_file
}
' "$setup_file"

count="$(cat "$tmp_dir/count")"
if [ "$count" -eq 0 ]; then
    echo "No generated shell helpers found in ${setup_file}" >&2
    exit 1
fi

for helper in "$tmp_dir"/helper-*; do
    shebang="$(head -n 1 "$helper")"
    case "$shebang" in
        *python*)
            python3 -m py_compile "$helper"
            ;;
        *)
            bash -n "$helper"
            ;;
    esac
done

if [ -f "${repo_root}/scripts/240mp-bluetooth-control" ]; then
    python3 -m py_compile "${repo_root}/scripts/240mp-bluetooth-control"
fi

echo "Generated helper syntax OK (${count} helpers)."
