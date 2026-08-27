#pragma once

#include <functional>
#include <memory>
#include <string>

struct JSContext;  // quickjs.h forward decl; full type lives only in the .cpp

namespace uo::js {

// Embedded QuickJS-NG runtime that drives bot scripts. One engine per Client;
// it ticks on the main loop right after BotTick. All QuickJS details live in
// the .cpp (pimpl) so quickjs.h never leaks into the widely-included Client.h.
// Script/JS errors are caught and routed to stderr + the file log ([js] tag);
// they never propagate into the C++ loop.
class JsEngine {
public:
    JsEngine();
    ~JsEngine();
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    // Host hook for installing app-specific globals (e.g. the Player object).
    // Invoked on every fresh runtime, after the core natives and before the
    // prelude/script eval, with the raw JSContext. Keeps the engine generic.
    using BindingInstaller = std::function<void(JSContext*)>;
    void SetBindingInstaller(BindingInstaller fn);

    // Called during teardown (Stop / hot-reload) while the context is still
    // alive, so the host can free any JSValues it holds and drop dangling refs.
    void SetBindingTeardown(std::function<void()> fn);

    // Tear down any running script and start fresh: new runtime, reload the
    // prelude, eval scriptPath (global). Returns false if the file can't be
    // read or fails to load (already reported to stderr/[js]).
    bool Run(const std::string& scriptPath);

    // Destroy the runtime (clean slate). Safe to call when idle.
    void Stop();

    // Pump due timers and drain the microtask/job queue. Call once per tick.
    void Tick();

    bool Running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace uo::js
