# Required Hydra `PMI_FD` downstream patch

This directory records the exact DMTCP source delta required by the contributor's validated MANA/MPICH/Hydra environment.

Apply locally for testing:

```bash
bash contrib/dmtcp-patches/apply.sh
```

The helper modifies only the checked-out DMTCP submodule source and is idempotent. The resulting dirty submodule contents are for validation only and must be restored before committing this MANA draft branch.

This draft intentionally asks the MANA maintainers to decide whether the final solution should be a temporary downstream patch, an in-tree MANA compatibility fix, a direct DMTCP contribution, or another design.
