---
name: dismount-reads-as-horselessness
description: Any deliberate dismount is read by the need model as "this character has no horse" unless the observation says otherwise; the mount/dismount gestures also never confirm
metadata:
  type: project
---

A deliberate dismount is not observably different from being horseless unless we
say so. `NeedMount` gates on `!obs.mounted` alone, so the first mining dismount
scored BUY_MOUNT 204.0 against MINE 78.8, the remount backstop put the character
straight back up, BUY_MOUNT "completed" with progress=1, and the pair repeated
six times in five minutes (Kharain, 2026-09-04 18:55). The carrier is
`Observation::dismountedForWork`, set from the runner's own flag.

**Why:** on-foot-ness is a client fact with two very different causes, and only
the runner knows which one it is. Every consumer of `!obs.mounted` is really
asking one of "can I ride away" or "do I own a horse", and the second one needs
the extra bit.

**How to apply:** whenever a bot-core action deliberately changes a visible
character state that a need also reads (mounted, war mode, hands empty,
poisoned), add the "on purpose" bit to Observation in the same change — and
smoke it, because the spin only shows at runtime.

Two related runtime facts, both verified 2026-09-04 in
`run_gates/g_Kharain.console.txt`:
- Dismount is a double-click on YOURSELF, remount a double-click on the ANIMAL
  (a ridden horse is an item on layer 25 and no mobile at all, so it can only be
  found again AFTER the dismount lands — see [[a-station-is-a-dupelist-not-one-graphic]]
  for the same "find it before you touch it" shape).
- Neither gesture is ever confirmed: both book an `ACTION_RESULT use_object
  timeout` ~4 s later while `event mount_state:` proves they landed. Judge them
  by `obs.mounted`, never by the action result — see
  [[action-timeout-means-unrecognised-answer]].
