#!/usr/bin/env bash
set -euo pipefail
mana_root="$(cd "$(dirname "$0")/../.." && pwd)"
dmtcp_root="$mana_root/dmtcp"
patch="$mana_root/contrib/dmtcp-patches/0001-ipc-skip-socket-scan-when-PMI_FD-is-set.patch"

if git -C "$dmtcp_root" apply --reverse --check "$patch" 2>/dev/null; then
  echo "The PMI_FD patch is already applied."
  exit 0
fi
# Fail before modifying files when this exact patch is incompatible.
git -C "$dmtcp_root" apply --check "$patch"
git -C "$dmtcp_root" apply "$patch"
git -C "$dmtcp_root" diff --check
echo "Applied the PMI_FD patch for local validation; rebuild MANA."
echo "The MANA DMTCP gitlink is unchanged."
