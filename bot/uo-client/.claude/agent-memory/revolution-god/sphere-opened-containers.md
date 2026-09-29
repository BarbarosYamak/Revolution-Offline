---
name: sphere-opened-containers
description: Sphere refuses dclick on pack contents unless that client opened the pack successfully; a ghost login's open fails silently, so reopen on resurrection (fixed f65225b)
metadata:
  type: project
---

Sphere `Cmd_Use_Item` (CClientUse.cpp:36-89) rejects a double-click on any item
inside a container the *client* has not opened this connection
("You can't use this where it is." = DEFMSG_REACH_UNABLE). The opened set
(`m_openedContainers`) is filled only when the open succeeds
(CClientMsg.cpp addContainerSetup). A dead character's open answers
"Your ghostly hand passes through the object" and registers nothing.

**Why:** Faustus 2026-09-05 logged in dead, got resurrected, bought 5 heal
potions and could not drink them for 10 minutes; HP crawled 6->19 at Regen0=40.
Fixed by reopening the backpack in `Client::ActionOnBodyChange` on Alive.

**How to apply:** any "can't use this where it is" on a pack item = the pack
(or sub-bag) is not in the opened set for this connection, not a bad serial.
Check `[backpack] opening` was followed by `[0x24] open container` in the
console. Same rule for bags inside the pack and for reconnects.

Related: [[world-save-stalls-server]]
