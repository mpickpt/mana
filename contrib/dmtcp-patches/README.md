# Hydra PMI_FD patch

This patch addresses a problem found during multi-node MPICH/Hydra checkpoint/restart tests with MANA. Those tests required adding `PMI_FD` to the existing condition in `SocketConnList::scanForPreExisting()`.

With this change, processes with `PMI_FD` set skip the pre-existing socket scan, just as they already do when `HYDI_CONTROL_FD` or the listed Slurm variables are present.

The patch applies to DMTCP commit
`97ab11e406977a2564f8da59c9d8a3e36bd5f505`, bundled with MANA v1.5.0.

Related discussions: #485 and #491.

## Apply and rebuild

From a configured MANA source directory, run:

```bash
bash contrib/dmtcp-patches/apply.sh
make -j"$(nproc)" mana
```

The helper checks whether the patch is already applied, so it can be run more than once. It changes the local DMTCP source without changing the submodule revision recorded by MANA.

## Remove the patch

From the MANA source directory, run:

```bash
git -C dmtcp apply --reverse \
  ../contrib/dmtcp-patches/0001-ipc-skip-socket-scan-when-PMI_FD-is-set.patch
```

Rebuild afterward if you also want to restore the unpatched runtime.

## Upstream integration

The patch is included here so the change can be tested and reviewed.
It must be applied explicitly (MANA's normal build does not apply it).

The final approach remains open for discussion, including submitting the change to DMTCP and updating MANA to use the resulting upstream revision.
