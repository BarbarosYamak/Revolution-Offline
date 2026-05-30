# JavaScript code writing principles for UO bot scripts running on QuickJS NG:

* Write minimal, direct, self-explanatory code. Prefer clear names over comments. Variables, parameters, functions, and methods must have descriptive names; avoid cryptic abbreviations.

* Use standard JavaScript naming conventions: `camelCase` for variables, parameters, functions, and methods; `PascalCase` for classes and constructors; `UPPER_SNAKE_CASE` only for true constants.

* Bot behavior should be expressed as a flat priority-ordered list of behaviors where possible. Prefer readable imperative steps over generic behavior-tree, framework, event-bus, or enterprise-style architecture.

* Behavior guards (`when`) must be cheap, synchronous, and side-effect free. They should inspect current state only.

* Behavior steps must be cooperative-cancellable. Every long-running step must accept a `token`, wrap meaningful awaits with `token.wait(...)`, `token.sleep(...)`, or token-aware helpers, and must not swallow `CANCELLED`.

* If a waited engine operation has external side effects, cancellation must also stop the underlying action through `token.onCancel(...)`, `onPreempt`, or a token-aware engine API.

* Prefer linear `async/await` control flow. Avoid deeply nested callbacks, hidden concurrent loops, timer-driven state machines, and implicit background work unless the behavior explicitly requires it.

* Keep bot state explicit and small. Store only state that is required across steps. Prefer deriving current facts from the client/world state when possible.

* Do not build a second hidden state machine inside a behavior step unless required. Prefer priority behaviors plus explicit local control flow.

* Behavior steps are allowed to be separate methods even when used once, because they represent named bot actions. Avoid tiny wrapper helpers, but keep top-level behaviors explicit.

* Comments are allowed when they explain intent, constraints, non-obvious behavior, runtime limitations, protocol details, game-specific assumptions, cancellation behavior, or important invariants. Do not write comments that merely repeat what the code already says.

* JSDoc comments are allowed when they improve local readability or IDE support. Prefer `global.d.ts` for stable engine-provided globals, shared APIs, and reusable type definitions.

* `onTransition` should be used for logging and diagnostics only. Game logic must live in behavior guards and behavior steps.

* Avoid premature optimization. Prefer simple, obvious code and follow the KISS principle.

* Do not introduce extra abstractions, helper functions, classes, interfaces, or layers unless they clearly reduce complexity, improve readability, hide noisy runtime/protocol details, prevent real duplication, or represent a meaningful domain concept.

* Follow Occam's Razor: choose the simplest solution that fully satisfies the requirements.

* Fail loudly for programming errors, but recover gracefully from expected game/runtime conditions: missing targets, pathfinding failure, journal timeout, death, disconnects, cooldowns, blocked movement, and temporary server delays.

* The final code should be compact, readable, practical, and suitable for embedded scripting, not "enterprise-style".