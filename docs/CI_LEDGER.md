# CI ledger

Point-in-time record of CI speed changes: what was measured, what changed, and when to look again.

## 2026-11-14: one run per push, Linux on dev/master only

**Constraint.** Solo project, PRs merged every few days to weeks, so the stage that holds work is the
owner's own review and merge, not CI. What CI decides is how long a push waits for a verdict (F) and
the runner minutes behind it. Hosted minutes are free (public repository).

**Before** (15 pushes to `feature/lucky-merge`, 2026-11-07 to 2026-11-14; latest green push dd7491f):

| | push run (43563467606) | PR run (43563469621) |
|---|---|---|
| wall-clock | 13.5 min | 22.2 min |
| runner minutes | 63 | 118 |
| Windows builds | self-hosted incremental, 1.4-2.9 min | hosted from scratch, 9-18 min |
| Linux ccache hits | 66 % | 0 % (a PR run cannot read the head branch's cache) |
| critical path | Linux Dist 13.2 min | Linux Dist 21.9 min |

Every push ran both, so a verdict took the slower PR run: about 22 min, 181 runner minutes.
The repository cache held 10.8 GB against GitHub's 10 GB limit (115 entries; 30 Debug ccache
entries on one branch alone).

**Changes** (`.github/workflows/main.yml`):

1. Same-repository PRs get no run of their own (`version`, `test`, `test-linux` skip them; the build
   jobs follow `version`). The push run reports on the same commit.
2. Linux builds and tests run only on `master` / `dev` pushes, manual runs and fork PRs.
3. Each Linux build deletes its branch's superseded ccache entries after saving the new one.
4. `paths-ignore` skips commits that only touch planning docs, agent config or root `*.md`.
5. `tests/run_local.py`: one command for the GPU suites CI cannot run.

**Predicted after.** Feature push: about 8 min to verdict (Windows chain on the single self-hosted
runner: five jobs queue in turn), about 15 runner minutes. Dev push: about 13 min (Linux Dist, warm
cache), as before. Cache growth limited to one ccache entry per configuration per branch.

**Not taken, and what it waits on:**
- A second self-hosted Windows runner, so the five Windows jobs stop queueing: worth it only if the
  feature-push verdict stays above about 8 min.
- Merging the two Windows `Test:` jobs into the build jobs: saves a runner slot each, waits on the
  measured queue time.

**Flip conditions.** Bring back same-repository PR runs if a merge breaks `dev` that the branch run
passed (the PR run builds the merge with `dev`, the push run builds the branch alone). Bring back
Linux on feature pushes if a `dev` push turns red on Linux after a green feature branch.

**Re-measure:** 2026-11-28, from the next 10 pushes (`gh run list --workflow main.yml`).
