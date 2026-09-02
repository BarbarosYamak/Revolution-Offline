---
name: action-timeout-means-unrecognised-answer
description: A repeating action_result timeout almost always means the server DID answer and the client did not recognise the answer shape, not that the server ignored the request
metadata:
  type: project
---

An action that times out at exactly its deadline, over and over, has almost
never been ignored by Sphere. The server answered in a shape the action did
not accept.

**Why:** four separate instances now, all found the same way — read the
journal lines between the `[ACTION]` line and the `[ACTION_RESULT] timeout`
and the server's answer is sitting right there:

- craft menu: answer was a 0x7C dialog (fixed by `ActionOnMenuOpened`)
- learning a spell from a scroll: answer was the scroll's 0x1D delete, never
  a 0x25 into the book
- spell/vendor refusals: answer was refusal TEXT ("You lack ...", "You are
  selling too fast.")
- eating (2026-09-02): answer was text and nothing else — Sphere's
  `CChar::Use_Eat` sends one system message, no gump, no cursor, and no 0x1D
  at all when a stack merely shrinks by one

**How to apply:** before touching the action's timeout, retry policy or the
goal that issues it, slice the console log around ONE timeout and read what
arrived in the 50 ms after the request. Add the confirmation shape to
`Client::ActionOnSysMessage` / the matching `ActionOn*` hook, and put the pure
text classification in `uo/actions.h` (`act::`) so ctest covers it without a
socket. See also [[state-flags-need-the-latest-statement]].
