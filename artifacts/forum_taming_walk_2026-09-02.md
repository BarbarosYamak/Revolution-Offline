# Taming/horse forum walk — board-by-board, 2026-09-02

Method: guest-readable board topic lists walked directly (no search endpoint),
per docs/FORUM_SWEEP_2026_08_30.md board ids. `curl -sL -A "Mozilla/5.0"`.

## Boards walked (topic-list pages fetched)

- board,176.0 (single page, 20 topics, 2011)
- board,190.0 / .20 / .40 / .60 / .80 (2012-2016 "Pazar Alanı")
- board,206.0 / .20 / .40 (2016 "Pazar Alanı")
- board,225.0 ("Alınacaklar" / buy board)
- board,226.0 ("Satılacaklar" / sell board)
- board,218.0 ("Esnaf Odası" — returned empty/no topics for guest)

All titles grepped case-insensitively for: at/horse, taming, tamer, evcil,
hayvan, ostard, llama, nightmare, wyrm, dragon, ejderha, mount, binek, steed,
"53".

## Topics opened (mount/ostard-titled threads, full content fetched)

topic,86763 / 86769 / 86745 (board 176, 2011) — "Satılık Binek(ler)",
"Satılır Ostard'lar ve Villa", "Desert ve Forest Ostard"
topic,89458 (board 190, 2012) — "Kaliteli Binekler (Fiyatlarda Pazarlık
Payı Yoktur)"
topic,90403 / 90398 (board 190, 2012) — desert/Frenzied Ostard sale
topic,94135 (board 206, 2016) — Frenzied Ostard buy request
topic,91062 (board 190, 2012) — "Water Steed"

## Verbatim price content found (all FORUM, all pre-tamed animal resale — NO skill requirement stated)

- topic,86763 (12 Tem 2011, msg_933398): "Mid Ostard (Blood) 549,999 gp /
  Mid Ostard (Golden) 449,999 gp / Mid Ostard (Silver) 349,999 gp /
  Frenzied Ostard 311,111 gp (2 Adet) / Forest Ostard 64,999 gp (struck out)"
- topic,86769 (msg_933484): "Frenzied Ostard (300k) / Desert Ostard (80k) /
  Two Story Villa (300k)"
- topic,86745 (msg_933285): "Desert Ostard / Forest Ostard satılıktır... Sabit
  bir fiyat olmamakla birlikte..." (no fixed price, PM negotiation)
- topic,89458 (21 Ağu 2012, msg_954557/954613/954620, repeated): "Steed 12m /
  Mare tane 4m / Uni tane 1.7m" (vergisiz/no-tax prices); msg_954610: "dark
  steede 8 m vereyim"; msg_954618: "Hepicigine 14 m verebilirim" (bulk offer)
- topic,91062 (msg_957228/957237): "300m veren alır bineği" / "290 m var
  uyarsa alayım?" (a single very expensive, presumably rare-hued Water Steed)
- topic,90403/90398/94135: prices/asks only ("30k desert ostard satılır",
  "120k Frenzied Ostard Satılır!", buy request "fiyatları pm ile ulaştırınız")

**No thread, across any board or year walked, states a Taming skill number
required to control/own any mount.** Every mount-sale/buy thread is a pure
price negotiation for an already-tamed animal; sellers never mention the
buyer needing a specific Taming value. This is consistent in all ~15 threads
opened this pass and the prior pass — the pattern appears structural (players
don't advertise skill gates on livestock ads), not a gap in search coverage.

## Changelog (board 177, topic 15324) — re-checked, same 3 hits as prior pass

- "Stealing, snooping, animal lore, animal taming, stealth, hiding,
  lockpicking hariç bütün yeteneklerin artışı %200-250 kolaylaştırıldı."
  (gain-rate change, no numeric requirement) — FORUM, ~2009 context.
- "Spawntakip listesi güncellendi, binekler ölene yada evcilleştirilene
  kadar listede gözükmeyecektir." — FORUM, 28.02.2016.
- "Takipçi sayısı sınırdayken hayvanların kontrattan çıkarılması veya yeni
  hayvan evcilleştirilmesi mümkün olmaz." — FORUM, 26.02.2012.

No "53", "53.1", "29.1" or any taming-difficulty digit found anywhere in the
changelog megathread or any opened market thread.

## Verdict

**UNKNOWN (forum-side).** Horse Taming requirement is not stated anywhere in
guest-readable forum content across 12 board-list pages and 8 individual
topics opened this pass (plus 3 reused from board 176 in the prior sweep).
The only concrete numeric source remains Scripts-X:
`c_horse_tan`/`c_horse_brown_dk`/`c_horse_gray`/`c_horse_brown_lt`/
`c_horse_pack` all carry `TAMING=29.1` (runtime\scripts\npcs\c_monster_classic.scp,
lines 4256/4468/5106/5147/5470 — CURRENT_SCRIPT). Owner's remembered 53.1
still has no corroborating source of any kind (forum, changelog, or script).
