# uo-protocol memory

- [Sphere world save freezes all clients](sphere-world-save-freezes-all-clients.md) — ~5.3s global stall, packets queue then flush; fleet-wide timeouts firing at one wall clock mean this, not load
- [Late acks after a movement reset](late-acks-after-reset.md) — orphan 0x22s still arrive after clearing pending; consuming one desyncs the flight window silently
