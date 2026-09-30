# M5.13: Runebooks: scribes craft them, mages order them

Date: 2026-09-30. **STATUS: BOT SIDE BUILT. BLOCKED on one server menu line.**

## The chain

* **Recipe** (`revolution_runebook.scp`, `Production.cpp`): Inscription 45,
  pen and ink. Inputs:
  * 8 blank scrolls (carpenter-made, or bought);
  * 1 blank rune (mage shop, owner ruling 2026-09-30);
  * 1 Recall scroll;
  * 1 Gate Travel scroll.

  **Scribes already make both scrolls**; they are on the scribe's `produces`
  list.
* **Scribe:** `i_spellbook_runebook` is now the **last** entry of the
  scribe's `produces`, so it never displaces the scroll ladder that trains
  Inscription. The rune is on its `consumes`. A scribe makes a runebook **to
  order**, through the existing order system (`Orders.cpp CanMakeOrder`).
* **Mage:** a spell-combat life with Magery 40 or more and no runebook orders
  one from a scribe, at the crafted-good ask it has seen, or 1,500 gold
  without one, within its reserve. It pays and collects by secure trade, like
  any other order.
* The recipe is now `PlayerCrafted`, the menu path is `"Runebook"` at the top
  of the Inscription menu (`Identity.cpp CraftMenuFor`), and graphic 0x22C5
  maps to the item.

## The one server change needed

The item is in **no skill menu** on this tree (M3.7 §8), so no scribe can
make one yet. Revolution's own changelog puts runebook work in the
Inscription menu (13.05.2009: *"Runebook kopyalama inscription menüsüne
eklendi"*).

Add these two lines to the **top level of `sm_inscription`** in your
Inscription skill-menu script (the `sm_legacy_*.scp` family, not in this
repo):

```
ON=i_spellbook_runebook Runebook
MAKEITEM=i_spellbook_runebook
```

Until then, runebook orders stay silent (`Orderable()` needs a real way to
make the item), and nothing else changes.

## Verified

* `m5_professions` confirms the scribe's new input list is complete: the rune
  is on `consumes`.
* The full ctest suite still passes; the known missing TSV is the one failure.
* Live: blocked on the menu line.
