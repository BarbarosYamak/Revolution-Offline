# M3.7.1 — Mounts

Date: 2026-08-27. **STATUS: PASS.** A bot tames nothing it was given, rides a
horse it found in the world, and covers the same road in **1.82× less time**,
measured on a live server in a single run.

Source-X modifications: **0**. Scripts-X: **0**. Runtime script modifications: **0**.

---

## 1. Why mounts

M3.7's slices spent most of their wall-clock walking. Slice A's tailor walked
twelve minutes each way to a sheep field; the tamer built for this milestone
spawned 1760 tiles and 44 legs from Britain. Travel was the tax on every proof.

Source-X halves the minimum time per step for a mounted character.
`Event_CheckWalkBuffer` states the table outright
(`src/game/clients/CClientEvent.cpp:750-762`):

```
         RUN / Walk
  Mount  100 / 200
  foot   200 / 400

  if (m_pChar->IsStatFlag(STATF_ONHORSE | STATF_HOVERING))
      iTimeMin = 100;
  else
      iTimeMin = 200;
```

**Which copy of that table is live had to be checked, not assumed.** `Event_Walk`
carries a second one (`:915-919`) behind `IsSetEF(EF_FastWalkPrevention)`, and
this shard leaves that flag off — `sphere.ini:836` comments it with *"Not work
at the moment don't use"*. So the walkbuffer is the rule in force, and the
walkbuffer is the one that reads `STATF_ONHORSE`.

---

## 2. The measurement

One run, one character, one stretch of road, walked twice — mounted first, then
on foot, with a dismount in between so the gait is *known* rather than inherited.
Everything that could bias one leg (route, terrain, server load, stamina) biases
both. `m371mount11`, `finished (59 steps)`, 0 errors.

| leg | mounted | on foot | ratio |
|---|---:|---:|---:|
| (985,1770) → (1049,1737) | **15.14 s** | **27.92 s** | 1.84× |
| (1049,1737) → (985,1770) | **13.69 s** | **24.55 s** | 1.79× |
| **total** | **28.83 s** | **52.47 s** | **1.82×** |

```
event mount_state: mounted                         14:29:56.827
[scenario] expect mounted: ok
travel_done ok=1 at=(1049,1737)                    14:30:11.968
travel_done ok=1 at=(985,1770)                     14:30:25.719

[ACTION] dismount (double-click self 0x0000A2A5)   14:30:29.350
[move] dismounted; step cadence back to 200/400ms
[scenario] expect on foot: ok                      14:30:32.400
travel_done ok=1 at=(1049,1737)                    14:31:00.323
travel_done ok=1 at=(986,1771)                     14:31:24.936
```

### 1.82×, not 2.00×, and the gap is not error

The step cadence really does halve — 100 ms against 200 ms — but a *journey* is
not only steps. Route planning, replans, turn-in-place moves and server ack
latency are fixed costs that a mount does not touch, so they dilute the ratio.

**The honest claim is therefore two claims**: cadence halves exactly, and
end-to-end travel improves by about 1.8× on a leg of this length. Longer legs
should trend closer to 2×, since the fixed overhead is amortised — untested.

---

## 3. What the client had to learn

### 3.1 Mounting and dismounting are the same gesture at different targets

There is no mount packet. `CClient::Event_DoubleClick`
(`CClientEvent.cpp:2362`):

```cpp
if ( pChar == m_pChar )
    if ( pChar->IsStatFlag(STATF_ONHORSE) )
        ... else if ( pChar->Horse_UnMount() ) return true;

if ( pChar->m_pNPC && (pChar->GetNPCBrainGroup() != NPCBRAIN_HUMAN) )
    if ( m_pChar->Horse_Mount(pChar) ) return true;
```

Double-click the animal to mount; double-click **yourself** to dismount. A
revision that dismounted by clicking the mount item on layer 25 — reasoning that
the animal no longer exists, so the item must be the handle — was accepted by the
server and left the character mounted.

The same block carries a guard worth knowing: without
`COMBAT_DCLICKSELF_UNMOUNTS`, a character in war mode holding a fight memory
gets its paperdoll instead of a dismount, so nobody falls off mid-fight.

### 3.2 Mounted state is persistent, and invisible to a mobile scan

A mounted animal is **not a mobile in the world**. Sphere deletes it and equips a
mount item on layer 25. Two consequences the scenario had to be built around:

* **Mounts survive logout.** A run that ends mounted begins the next one
  mounted. A baseline that assumed a fresh character walks on its own feet
  failed with `EXPECT on foot but the character is mounted` before moving a tile.
* **Scanning cannot find the horse you are sitting on.** One revision logged
  `event mount_state: mounted` at login and then died two seconds later on
  `REQUIRED 'horse' is not bound` — standing on the animal it was looking for.

This single fact produced four failures that looked like four different bugs: a
stale binding, a failed assertion, a horse that "wandered off", and a horse that
was never missing.

### 3.3 The client detects mounts from layer 25

`PlayerIsMounted()` reads equipment layer 25 — the same fact the server acts on
when it prices a step at 100 ms, not a guess from an animation or body id.
`Client::SubmitStep` then paces on it via `sphere::MountedStepMs`.

`ForgetEquippedItem()` on `0x1D` is what makes dismounting land:
`SetMobileEquipLayer` only ever upserts, so without removal the layer-25 entry
would persist for the session and the bot would keep pacing at mounted speed on
foot — sending steps the server prices as too fast.

---

## 4. The bug this milestone actually found

**`ActionScanMobiles` was mounting animals as a side effect.**

It double-clicked every nearby mobile to harvest paperdoll trade titles. But a
double-click is not an inspection — it is whatever that mobile does when clicked,
and by `CClientEvent.cpp:2378` a non-human NPC answers by being ridden.

Caught because a dismount refused to stick:

```
event mount_state: dismounted     14:27:37.570
event mount_state: mounted        14:27:40.604   <- scan_mobiles ran here
```

It also explains an earlier run that mounted a horse it had never been told to
ride, which was recorded as unexplained at the time rather than guessed at.

Fixed by restricting the click to human bodies (`sphere::IsHumanBody`). Trade
titles only exist on humans, so nothing is lost.

**Why this matters beyond mounts:** a bot is supposed to observe the world
without changing it, and this one was changing it. A bot walking a decorated
town would have silently climbed onto every llama, ostard and horse it passed.
Any future "look at everything nearby" behaviour needs the same audit — reading
by clicking is not reading.

---

## 5. The economics of a mount

**No NPC on this shard sells a live animal.** There is not one `BUY=c_*` or
`SELL=c_*` line anywhere in `runtime/scripts`. A mount is tamed, or bought from
another player — the same shape as M3.7's finding that a smith's first hammer
must come from a Tinker.

Taming a horse costs real character budget, which makes mounts a genuine
Revolution trade-off rather than a free upgrade:

| source | horse taming requirement |
|---|---|
| Revolution `/binek_bilgileri` | **53.1** |
| this runtime, `c_horse_gray` CHARDEF 0e2 | **29.1** |

**That divergence is recorded, not closed.** 29.1 is stock Sphere's number and
53.1 is Revolution's; `runtime/scripts` is not modified to reconcile them. The
tamer used here was built by ordinary character creation — Animal Taming 50.0,
Animal Lore 30.0, Veterinary 20.0, trained sum 626.4, inside the 700 cap — and
was granted nothing. It clears 29.1 comfortably and would **fail** Revolution's
published 53.1.

So this character proves the **mount mechanic** and may never be cited as
evidence about how hard taming is on Revolution.

At Revolution's real 53.1, a mount costs a crafter over 7% of its entire 700
budget — which is precisely why a Tamer selling horses is a viable profession.

---

## 6. Tests

`sphere_regression` covers the rule rather than the scenario:

* mounted running is 100 ms, half the on-foot floor
* mounted walking is 200 ms
* on foot both cadences are untouched
* the mount layer is 25
* the divisor is exactly 2 — if Source-X's table ever stops being a clean
  halving, this check fails first

**8/8 suites, 0 failures.**

---

## 7. Open

1. **The 1.82× is one leg length.** Fixed per-journey overhead dilutes it;
   longer routes should trend toward 2×. Unmeasured.
2. **WalkBuffer is a budget, not a per-step gate** (`sphere.ini` 218/221, here 15
   and 25). Sitting exactly on `iTimeMin` is what the server prices as free;
   sustained bursts may still accumulate points. No sustained-load test has run.
3. **Nothing uses mounts yet.** The cadence is wired in and the ops exist, but no
   travel or economic behaviour asks for a horse. Making bots acquire and keep
   mounts — and lose them on death — is M4 work.
4. **Pets are not modelled.** A tamed animal that is not being ridden follows,
   wanders, eats and can be stolen or killed. None of that is represented.
