---
name: world-save-stalls-server
description: Sphere world save freezes ALL clients ~5s; watchdog now discriminates server-stall vs lost move; affects any timing assertion in live runs
metadata:
  type: project
---

Sphere Source-X world save runs on the main thread and stops servicing every
client for ~5.3s (measured wave10, 2026-08-31 17:34:04). All in-flight move
acks arrive late but correct and in order.

**Why:** 19/33 bots aborted paths on a flat 5s move-unacked watchdog during
one save; looked like load/desync but was one global stall. Client-side fix:
watchdog aborts at 5s only if inbound traffic seen within 1.5s (server alive
→ move truly lost), else waits to 20s ceiling; orphaned late acks classified,
never blind-popped (blind pop was a latent flight-window desync).

**How to apply:** any live-run timing assertion (action timeouts, watchdogs,
scenario deadlines) must tolerate a ~5-20s global freeze at save time. A
longer save on a bigger world may need the 20s ceiling revisited. Live
verification across a real save still outstanding for the fix.
