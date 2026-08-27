# RevolutionUO Anti-Macro — Historical Specification and Adaptation

Date: 2026-08-26 (M3.5). Profile: `revolution_2009_2010`
(see `REVOLUTION_RULESET_PROFILE.md`).

**Status: SPECIFIED. NOT IMPLEMENTED, and deliberately so.** §5 explains why an
offline bot shard needs the *pressure* this system created, not the CAPTCHA it
used to create it.

---

## 1. The question

M2.5 and M3 found no anti-macro system anywhere in Source-X or the runtime
scripts, against a clear memory that Revolution had one. That is a genuine
compatibility gap, and the archive settles it.

---

## 2. Historical evidence

Verbatim from `https://www.revolutionuo.net/guncellemeler`
(`OFFICIAL_REVOLUTION_UPDATE`) unless noted.

| Date | Turkish | English |
|---|---|---|
| **15.04.2008** | "Anti-macro modülünde oluşan karakter doğrulama hatası giderildi." | "The character-verification bug in the anti-macro module was fixed." |
| **22.02.2011** | "Anti-makro modülü aktif edildi, sürekli üretim yaptığınız takdirde 2-3 saatte bir açılacak ekrana onay kodunu doğru girerek kimliğinizi doğrulamalısınız." | "The anti-macro module was activated: **if you produce continuously**, you must verify your identity by correctly entering the confirmation code on a screen that will open **every 2–3 hours**." |
| **21.01.2016** | "Anti-macro kodunu bir dakika boyunca girmeyen oyuncular oyundan düşecektir." | "Players who do not enter the anti-macro code within one minute will be dropped from the game." |

From `https://www.revolutionuo.net/genel_kurallar` (`OFFICIAL_REVOLUTION_GUIDE`):

> "oyuna oyunla ilgilenebilecek kişi sayısından fazla istemciyle bağlanılması
> yasaktır."
> *"Connecting with more clients than the number of people who can attend to
> the game is prohibited."*
> First offence: 72-hour jail on all characters. Four offences: account
> deletion.

### 2.1 What the dates actually establish

* A verification module existed and was already being **bug-fixed by April
  2008** — so it predates our profile window, not just the 2011 entry.
* The 2011 entry describes **re-activation** and gives the only concrete
  numbers we have: **2–3 hours**, on **continuous production**.
* The one-minute kick is **2016** and belongs to Revolution16. Excluded.

### 2.2 The trigger is narrower than "playing a long time"

The 2011 wording is specific: **"sürekli üretim"** — *continuous production*.
Not combat, not walking, not casting. Crafting and gathering loops are what
tripped it. That distinction matters: it means Revolution's anti-macro targeted
exactly the unattended **economic** grind, and left ordinary play alone.

Nothing in the archive says the verification altered skill-gain *formulas*. It
gated **unattended operation**, not learning.

---

## 3. Current runtime state

| Layer | Finding |
|---|---|
| Source-X | No verification, no macro detection, no idle-kick of this kind. |
| Runtime scripts | Nothing in `runtime/scripts`. |
| Related but different | `MaxConnectRequestsPerIP` / `NetTTL` guard *connections*, not macroing. That is a separate system and M3.5 handles it in the fleet controller. |

**Gap confirmed:** the reconstruction has no anti-macro of any kind.

---

## 4. Measured consequence in our own reconstruction

M3 ran a Magery training loop for 1 h 45 m: 214 casts, 245 meditation attempts,
246 out-of-mana refusals, ~214 spider silk and sulfurous ash consumed, Magery
50.0 → 50.9. Nothing interrupted it and nothing ever would.

On historical Revolution that session would have been legal — it is casting,
not production — but a **crafting** session of the same length would have hit a
verification screen once or twice.

So the honest statement is: **our reconstruction currently permits infinite
unattended production, and Revolution did not.**

---

## 5. Adaptation decision

This is the part that needs stating plainly rather than implementing quietly.

**A CAPTCHA is the wrong mechanism for this project.** Revolution's screen
existed to prove *a human was present*. Revolution Offline's entire purpose is
a shard populated by characters with **no human present**, playing by the same
rules. Implementing the literal mechanism would either:

* require a human to answer codes forever — defeating the project, or
* be auto-answered by the bots — reproducing the mechanism while destroying its
  meaning, which is worse than not having it, because it would look like
  fidelity.

**What must be preserved is the pressure, not the prompt.** Everything the
system actually achieved on the shard economy:

| Historical effect | Why it matters to a simulated economy |
|---|---|
| Unattended production had a ceiling | Goods stayed scarce; crafters could not print supply overnight |
| A player had to return every 2–3 hours | Production came in **sessions**, not a flat stream |
| Failing to answer cost you the session | Sessions ended untidily, sometimes mid-loop |
| It applied to **production**, not to combat or travel | Fighting and exploring were never throttled |

**Proposed adaptation (not implemented):** a `ProductionSession` bound to the
same trigger the historical system used.

```text
continuous production run
  -> after 2-3 hours (profile value, jittered per character)
  -> the session ENDS rather than a code being demanded
  -> the character does something else: bank, restock, sell, travel, rest
  -> production may resume after a break
```

That reproduces every economic effect in the table without a prompt, without
touching skill-gain formulas, and without a bot pretending to be a human
answering a challenge. It is also, not incidentally, what the character
schedules in the Bible's lifecycles already describe.

**Explicitly rejected alternatives**

| Option | Why not |
|---|---|
| Implement the CAPTCHA and auto-answer it | Reproduces the shape, destroys the meaning, and hard-codes a lie into the simulation |
| Reduce skill-gain rates to "simulate" anti-macro | No source says the system touched gain rates; it would be inventing a mechanic |
| Do nothing | Leaves a documented Revolution constraint absent from the economy |
| Implement it server-side in Scripts-X | Would need a Source-X-side prompt/kick path this reconstruction does not have, and would break every bot |

---

## 6. Open questions

| Question | State |
|---|---|
| Exact 2010 cadence | **UNKNOWN.** The 2–3 hour figure is dated 22.02.2011, four months after our client anchor. Used as the best description of the mechanism, with its date attached. |
| What counted as "continuous production" | Unknown in detail — crafting only, or gathering too? |
| Whether combat/casting ever triggered it | No source says so. Assumed not. |
| The 2008 form of the module | Only its bug-fix is recorded; behaviour unknown. |
| Penalty in 2010 (kick? jail? nothing?) | Unknown; the one-minute kick is 2016. |

---

## 7. Bottom line

| | |
|---|---|
| Did Revolution have anti-macro? | **Yes** — module bug-fixed by 15.04.2008, re-activated 22.02.2011, hardened 2016. |
| Was the player memory right? | **Yes.** |
| What triggered it? | **Continuous production**, every 2–3 hours (2011 wording). |
| Did it change skill gain? | **No evidence that it did.** It gated unattended operation. |
| Present in our runtime? | **No. Confirmed gap.** |
| Implemented in M3.5? | **No** — and the reason is a design decision recorded above, not an omission. |
