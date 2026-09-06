# Survival, recovery and economy corrections — 2026-09-05

Changes in this pass (preserving the existing uncommitted work):
- RecoverCorpse cancels travel only when transitioning from a corpse trip to recovery. Repeated recovery ticks preserve the medical supply journey.
- Self-healing shopping requires the same Healing 30.0 threshold as the medical equipment errand. Veterinary-only bandages do not qualify. Potion users measure proximity to an alchemist instead of a healer.
- Life needs, goal scoring and sale execution use available bank-funded gold for the small-lot sale policy, consistently. A funded character with no pack coins waits for a worthwhile lot; a poor scribe can still sell two scrolls.
- A confirmed bank item deposit clears the per-item retry budget, so successful batches do not accumulate false failures.

Verification: run_build_m1.bat succeeded; run_ctest_m1.bat passed 44/44; git diff --check passed. Added recovery regression cases for fencer, archer, scribe, tamer, fisher and miner_smith, medical purchase eligibility checks, deposit-budget reset and funded/poor sale-lot checks. The harness injects journey ownership and observations; these checks do not prove actual navigation, server transactions or complete live archetype cycles. No live sessions were launched in this pass.