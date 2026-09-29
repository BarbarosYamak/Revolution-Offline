# uo-protocol memory

- [Sphere world save freezes all clients](sphere-world-save-freezes-all-clients.md) — ~5.3s global stall, packets queue then flush; fleet-wide timeouts firing at one wall clock mean this, not load
- [Late acks after a movement reset](late-acks-after-reset.md) — orphan 0x22s still arrive after clearing pending; consuming one desyncs the flight window silently
- [Bank box open-tile rule](bank-box-open-tile-rule.md) — the box only answers from the exact tile it was opened on; one step and every drop/lift bounces silently, no 0x27
- [The split echo is not an answer](move-split-echo-is-not-an-answer.md) — a partial lift 0x25s the ORIGINAL serial back in the source container; a bounce is byte-identical, only order tells them apart
