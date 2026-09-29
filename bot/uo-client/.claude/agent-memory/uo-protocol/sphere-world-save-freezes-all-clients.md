---
name: sphere-world-save-freezes-all-clients
description: Sphere's world save stops servicing every client for ~5.3s; all packets (incl. 0x22 move acks) queue and then flush at once, so per-client timeouts near 5s misfire fleet-wide
metadata:
  type: project
---

Source-X saves the world on the main thread. For the duration of the save the
server services nobody: nothing inbound arrives on any session, then everything
flushes together. Measured on RevolutionUO 2026-08-31, 33-client fleet
(`run_gates/wave10`): `System: World save has been initiated.` at 17:34:04.598,
held-back 0x22 acks delivered at 17:34:09.90 — a **~5.3s global stall**.
The save is announced in-band first (`World save in 10 seconds`, then
`World save has been initiated.`).

The acks were correct and in order — nothing was lost, dropped, or desynced.

**Why:** this masquerades as a load or protocol bug. 18 of 33 bots tripped a
flat 5s move-ack deadline at the same wall-clock instant and aborted healthy
paths; the natural (wrong) reading was "server can't keep up with 33 clients"
or "sequence desync". The tell that it is neither: **every affected client
fires within ~300ms of the same absolute timestamp**, and the awaited packet
arrives intact moments later.

**How to apply:**
- Any client-side timeout in the 1–10s range is in the blast radius. Before
  blaming per-session protocol state, check whether the deadline fired at the
  same wall clock across many characters and whether a world save preceded it.
- Distinguish "server stalled" from "our request was dropped" by whether
  *anything at all* arrived inbound during the window — global silence is the
  server's, not the request's. See [[late-acks-after-reset]].
- Fleet log triage: correlating one timestamp column across all
  `run_gates/<wave>/*.console.txt` files settles this in one command; per-file
  reading does not.
