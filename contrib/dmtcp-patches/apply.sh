#!/usr/bin/env bash
set -euo pipefail
mana_root="$(cd "$(dirname "$0")/../.." && pwd)"
patch="$mana_root/contrib/dmtcp-patches/0001-ipc-skip-socket-scan-when-PMI_FD-is-set.patch"
source_file="$(git -C "$mana_root/dmtcp" grep -l 'HYDI_CONTROL_FD' -- '*socketconnlist.cpp' | head -n 1)"
[[ -n "$source_file" ]] || { echo "ERROR: socketconnlist.cpp not found" >&2; exit 1; }
if git -C "$mana_root/dmtcp" grep -q 'getenv("PMI_FD")' -- "$source_file"; then
  echo "PMI_FD is already present in dmtcp/$source_file"
  exit 0
fi
git -C "$mana_root/dmtcp" apply "$patch"
git -C "$mana_root/dmtcp" diff --check
echo "Applied $patch to the local DMTCP submodule checkout."
echo "Do not commit dirty submodule contents in this draft PR."
