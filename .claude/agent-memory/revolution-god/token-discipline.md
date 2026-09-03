---
name: token-discipline
description: Token-reduction stack chosen 2026-08-31 — subagent output contracts, world_query/log_slice slicers; Serena/lnav/tshark deferred, Graphify report never read wholesale
metadata:
  type: project
---

Token-reduction decisions (2026-08-31, owner-approved after ChatGPT proposal review):

1. All 7 specialist agent defs carry an "Output contract" section: final response ≤1,200 tokens, FINDINGS/EVIDENCE/CHANGES/BLOCKERS/VERDICT structure, large evidence goes to `artifacts/<task>_evidence.md`, parent gets the path. revolution-god.md has the counterpart: read artifacts only when disputed.
2. Slicers over raw reads: `tools/world_query.py` (char/item/near/count over the world save) and `tools/log_slice.py` (grep/time-window/dedup over .console.txt) — built so agents consume queries, not dumps.
3. Deferred: Serena MCP (schema cost + clangd flakiness on Source-X), ast-grep, tshark (our packet traces are text, not pcap). Rejected: lnav (not native Windows), Aider repo-map (overlaps memory+specialists), reading graphify-out/GRAPH_REPORT.md or graph.json wholesale (171KB/45MB).

**Why:** dominant token sinks measured by workload are subagent essays and raw run logs, not code discovery; expected 40-60% per-milestone reduction, unproven until a full cycle runs with it.

**How to apply:** when delegating, never ask specialists to inline logs/dumps; when verifying, reach for the slicers before Read/Grep on saves or console files; revisit Serena only if cold-start C++ digs become frequent.
