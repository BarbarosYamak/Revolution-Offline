---
name: late-acks-after-reset
description: Moves already on the wire are still acked after the client clears its pending queue; consuming those orphan 0x22s pops a newer unacked move and desyncs the flight window permanently
metadata:
  type: project
---

The client resets fastwalk state (clear `pending`, seq back to 0) on watchdog
abort, threat interrupt and replan. Moves already sent are **still acked by the
server** afterwards. Those orphan 0x22s are not an anomaly — they are the
expected tail of the reset.

Two distinct outcomes, only one of which is loud:
- pending empty when the orphan lands → logged "unsolicited ack seq=N" (noisy
  but harmless). This is what `run_gates/wave10` shows.
- pending **non-empty** (a new step already re-pumped) → the orphan pops the
  new move's entry, freeing an in-flight slot the server never granted. The
  queue is then permanently one ack ahead of the wire. Silent and timing-
  dependent; wave10 got lucky on the ~1.2s gap.

Rejects are different: after a 0x21 the server holds MovePrevented and answers
the rest of the queue with **rejects**, not acks, so a reject-driven reset
leaves no acks owed.

**Why:** "unsolicited ack" reads like a server fault and invites raising a
timeout or ignoring the warning. It is really the client's own discarded move
coming home, and the dangerous variant of it never logs anything at all.

**How to apply:** when movement goes subtly wrong after an abort/replan (steps
pacing oddly, flight window off by one), suspect an orphan ack consumed as a
fresh one before suspecting the server. Match acks by sequence rather than
blindly popping the front. Related: [[sphere-world-save-freezes-all-clients]].
