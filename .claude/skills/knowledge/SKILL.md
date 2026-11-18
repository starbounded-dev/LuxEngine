---
name: knowledge
description: LuxEngine knowledge lookup. Answers any question about how the engine, editor, or runtime works or where something lives (systems, ownership, call paths, "what uses X", "where is Y handled") by querying the graphify knowledge graph first, then confirming against the source and .claude/docs/. Use before answering architecture or code-location questions, and before planning work that touches an unfamiliar system.
---

# knowledge — graph first, source to confirm

`/knowledge <question>` answers a question about the engine (`Core/`), editor (`Editor/`), or runtime
(`Lux-Runtime/`, `RuntimeAssetManager`, packs). With no argument, ask the question.

The graph is a fast map, not the truth: edges marked INFERRED can be wrong. The answer is graph for
discovery, **source for confirmation**.

## Step 1 — Locate the graph

`graphify-out/` is gitignored and lives only in the main checkout (`M:\Git\luxengine`). In a worktree
it is absent, so point at the main one:

```bash
G=/m/Git/luxengine/graphify-out/graph.json   # or ./graphify-out/graph.json when it exists
```

Missing entirely: say so and offer `graphify update .` (code only, no LLM, a few minutes). Do not
rebuild unasked in the middle of an answer.

## Step 2 — Query

```bash
graphify query "<question>" --graph $G            # broad context (BFS)
graphify query "<question>" --graph $G --dfs      # trace one path
graphify explain "<Symbol>" --graph $G            # a node and its neighbors
graphify path "<A>" "<B>" --graph $G              # how two things connect
```

Use `explain`/`path` once `query` names the symbols. Prefer a few targeted calls over one huge one.

## Step 3 — Confirm in source

For every claim you will state, open the cited file (Read/Grep) and check the symbol exists and does
what the graph implies. Then read the matching authority in `.claude/docs/`
(`Architecture-LuxEngine.md` for boundaries and ownership, `Threading.md`, `Rendering.md`,
`Conventions.md`, `Building.md`). If the graph and source disagree, source wins; say so.

## Step 4 — Answer

- Cite `path:line`. Mark anything you did not verify as unverified.
- Say what the graph could not answer instead of guessing (shaders `.glsl`, docs and YAML are weakly
  covered by the code-only graph).

## Freshness

Check `graphify-out/manifest.json`/`GRAPH_REPORT.md` date against `git log -1 --format=%cd`. If many
commits or the subsystem changed since, say the graph may be stale and offer to refresh:

```bash
graphify update .      # AST only, free, re-extracts changed code
```

Docs, papers, and community labels need the LLM path: `/graphify . --update` then `graphify label`.
Ask first; it spends tokens.

## Boundaries

Read-only except the graph refresh. Does not edit engine code, commit, or push.
