#!/usr/bin/env python3
"""One-off: purge a poisoned PriceBook observation from bot_data state.json.

Context (2026-09-07): a trade-window funding bug (fixed in
src/life/runner/Economy.cpp, "a buyer pays for what is in the window") let a
buyer offer gold for the full quantity/price it had SHOUTED rather than what
the seller actually delivered and asked. That taught four characters' price
books an i_bandage observation multiples above the shard's real 2-4gp band:

    Baelos  47/unit (who=Aelia)      Aelia  47/unit (who=Baelos)
    Wren    60/unit (who=Calar)      Calar  60/unit (who=Wren)

The fix stops new poison at the source (Economy.cpp's kPoisonPriceFactor
guard, see DoTradeWithPlayer's Completed-phase price observation code). This
script only cleans up the four PlayerTraded observations already written to
disk before the fix existed -- it does not touch anything else in the file
(ledger/memory/goal/session history are left exactly as they were; the trade
itself really happened and is not being erased, only the bad price BELIEF
that came out of it).

Usage:
    python tools/purge_poisoned_price_observations.py [--apply] [--factor 3] \
        [state.json ...]

Without --apply this only prints what it would remove (dry run). The removal
rule matches the runtime's own poison guard: a PlayerTraded (source=3)
observation for an item priced more than `--factor` times every OTHER
observed price this same book holds for that item is dropped. Where no other
observation exists for the item (all four books here have exactly one), the
known-bad price is named explicitly below as a belt-and-braces check, so this
script cannot be pointed at an unrelated file and silently delete an honest
observation that happens to be the only one on record.
"""
import argparse
import json
import sys

# The exact poisoned observations this incident produced (item, price,
# source, who). Every removal is required to match one of these rows AS WELL
# AS the general >factor-x rule above -- belt and braces, so a typo in the
# generic rule cannot widen this into "purge every single-observation price".
KNOWN_POISONED = {
    ("i_bandage", 47, 3, "Aelia"),
    ("i_bandage", 47, 3, "Baelos"),
    ("i_bandage", 60, 3, "Calar"),
    ("i_bandage", 60, 3, "Wren"),
}


def other_prices_for_item(prices, item, skip_index):
    return [p["price"] for i, p in enumerate(prices)
            if i != skip_index and p.get("item") == item]


def find_poisoned(prices, factor):
    """Indices of entries that are both in KNOWN_POISONED and priced more
    than `factor`x any other observation this book holds for the same item
    (or, with no other observation to compare against, exactly the recorded
    incident price -- never a price this script has not been told about)."""
    out = []
    for i, p in enumerate(prices):
        key = (p.get("item"), p.get("price"), p.get("source"), p.get("who"))
        if key not in KNOWN_POISONED:
            continue
        others = other_prices_for_item(prices, p["item"], i)
        if others:
            if p["price"] <= factor * min(others):
                continue  # not actually an outlier here; leave it alone
        out.append(i)
    return out


def process(path, factor, apply_):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    prices = data.get("prices", [])
    idx = find_poisoned(prices, factor)
    if not idx:
        print(f"{path}: no poisoned observation found")
        return
    for i in idx:
        p = prices[i]
        print(f"{path}: {'removing' if apply_ else 'would remove'} "
              f"{p['item']} {p['price']}/unit from {p.get('who')} "
              f"(source={p.get('source')}, when_ms={p.get('when_ms')})")
    if not apply_:
        return
    data["prices"] = [p for i, p in enumerate(prices) if i not in idx]
    # Same shape the client's own writer uses (life/State.cpp): two-space
    # indent, keys sorted. Matching it keeps this a one-line diff per file.
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", help="state.json files to clean")
    ap.add_argument("--apply", action="store_true",
                     help="write changes; default is dry-run")
    ap.add_argument("--factor", type=int, default=3,
                     help="poison-guard multiple, matches Economy.cpp's "
                          "kPoisonPriceFactor")
    args = ap.parse_args()
    for path in args.paths:
        process(path, args.factor, args.apply)


if __name__ == "__main__":
    sys.exit(main())
