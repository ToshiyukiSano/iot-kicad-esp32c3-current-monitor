#!/usr/bin/env bash
# Copy selected, non-secret KiCad/Arduino source files from a local project.
# Review the copied files before committing to a public repository.
set -euo pipefail
src="${1:-$HOME/Projects/current-monitor}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -d "$src" ]]; then
  echo "Source directory does not exist: $src" >&2; exit 1
fi
hd="$root/examples/current-monitor/hardware"
fw="$root/examples/current-monitor/firmware"
mkdir -p "$hd" "$fw"
for name in current-monitor.kicad_sch current-monitor.kicad_pcb current-monitor.kicad_pro current-monitor.kicad_sym fp-lib-table sym-lib-table; do
  [[ -f "$src/$name" ]] && { cp -p "$src/$name" "$hd/"; echo "copied $name"; }
done
if [[ -d "$src/current-monitor.pretty" ]]; then
  cp -R "$src/current-monitor.pretty" "$hd/"
  echo "copied current-monitor.pretty"
fi
if [[ -f "$src/firmware/current_monitor/current_monitor.ino" ]]; then
  mkdir -p "$fw/current_monitor"
  cp -p "$src/firmware/current_monitor/current_monitor.ino" "$fw/current_monitor/"
  echo "copied current_monitor.ino (UNVERIFIED until compiled/tested)"
fi
if [[ -f "$src/firmware/TEST_PLAN.md" ]]; then
  cp -p "$src/firmware/TEST_PLAN.md" "$fw/"
  echo "copied firmware/TEST_PLAN.md"
fi
if [[ -f "$src/export/current-monitor-BOM.csv" ]]; then
  cp -p "$src/export/current-monitor-BOM.csv" "$hd/bom-from-claude.csv"
  echo "copied BOM; verify all part numbers"
fi
if [[ -f "$src/export/current-monitor-final-check.md" ]]; then
  cp -p "$src/export/current-monitor-final-check.md" "$hd/final-check-from-claude.md"
fi
printf '
Files copied. Before git add: check any absolute model paths, credentials, third-party licenses, safety risks, and test status.
'
