---
name: material-buy-rows-commented
description: The runtime's tm_vend.scp has the log/board/ore/iron-ingot/hide BUY rows commented out, so most materials have NO NPC buyer on this shard whatever the policy permits
metadata:
  type: project
---

Every material `BUY=` row a bot would want in
`runtime/scripts/templates/tm_vend.scp` is **commented out** (`//` or `////`):
i_log, i_board, i_ingot_iron, i_hide, i_hides_cut across CARPENTER, TINKER,
PROVISIONER, JEWELER, BLACKSMITH, WEAPONS_BLADED/BLUNT, BOWYER, COBBLER,
FURTRADER, TANNER. i_ore_*, i_cloth, i_cloth_bolt, i_wool, i_yarn_ball have no
row at all.

What DOES have a live row: i_feather (bowyer, and provisioner via the
`BUY=VENDOR_B_BOWYER` include), i_cotton (weaver, tailor), i_thread (tailor),
i_flax_bundle (tailor), i_ingot_copper/gold/silver (jeweler; gold+silver also
provisioner), and all the fish (fisher, cook).

Live rows for i_log and i_ingot_iron survive only in
`sphere_template_vend_gargish.scp`, whose VENDOR_B_GARGISH_* templates belong
to the `c_*_gargoyle` chardefs — and the world save spawns **zero** gargoyle
vendors. The file IS loaded (`sphere.ini:55 ScpFiles=scripts/`); the gap is
spawns, not loading.

**Why:** verified 2026-09-02 while implementing the NPC price floor. The
citations in `src/economy/Market.cpp` (`:167 CARPENTER`, `:1935 BLACKSMITH`)
were doubly stale — the TNS shop-list swap re-numbered the file *and* disabled
the rows. Believing them would have sent a lumberjack across town to be
refused.

**How to apply:** never cite a tm_vend line number from a code comment or from
memory — re-grep, and check the row is not commented. Sphere ignores `//`.
Before promising any NPC sale route, resolve template→template includes
transitively and confirm the owning chardef is actually spawned in
`runtime/save/*.scp`. Re-enabling those rows is a server-side change and
belongs to `sphere-expert`.

See [[npc-price-floor-design]], [[vendor-payout-rules]].
