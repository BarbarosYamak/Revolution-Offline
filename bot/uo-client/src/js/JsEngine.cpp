#include "js/JsEngine.h"

#include "uo/log.h"

#include "quickjs.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace uo::js {

namespace {
constexpr const char* kBootstrapName = "bootstrap.js";

// The bootstrap lives alongside the script being run, so `run <path>/foo.js`
// finds `<path>/bootstrap.js` regardless of the process's working directory
// (e.g. when launched from build/). Loaded fresh on every Run so a hot-reloaded
// script always starts from the same clean surface.
std::string SiblingPath(const std::string& scriptPath, const char* name) {
    const std::size_t pos = scriptPath.find_last_of("/\\");
    if (pos == std::string::npos) return std::string(name);
    return scriptPath.substr(0, pos + 1) + name;
}
}  // namespace

struct JsEngine::Impl {
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    int nextTimerId = 1;
    struct Timer { int id; long long dueMs; JSValue cb; };
    std::vector<Timer> timers;
    BindingInstaller installer;
    std::function<void()> teardown;

    ~Impl() { Stop(); }

    static long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void Stop() {
        if (!rt) return;
        // Let the host free its JSValues and drop refs while ctx is still alive.
        if (teardown) teardown();
        // Free outstanding values while the context is still alive.
        for (auto& t : timers) JS_FreeValue(ctx, t.cb);
        timers.clear();
        if (ctx) { JS_FreeContext(ctx); ctx = nullptr; }
        JS_FreeRuntime(rt);
        rt = nullptr;
    }

    bool Start() {
        Stop();  // clean slate
        rt = JS_NewRuntime();
        if (!rt) { Fail("JS_NewRuntime failed"); return false; }
        ctx = JS_NewContext(rt);
        if (!ctx) {
            JS_FreeRuntime(rt);
            rt = nullptr;
            Fail("JS_NewContext failed");
            return false;
        }
        JS_SetContextOpaque(ctx, this);
        JS_SetHostPromiseRejectionTracker(rt, &Impl::OnPromiseRejection, this);
        RegisterGlobals();
        if (installer) installer(ctx);  // app-specific globals (Player, ...)
        return true;
    }

    void RegisterGlobals() {
        JSValue g = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, g, "__stdout",
                          JS_NewCFunction(ctx, &Impl::CbStdout, "__stdout", 1));
        JS_SetPropertyStr(ctx, g, "__stderr",
                          JS_NewCFunction(ctx, &Impl::CbStderr, "__stderr", 1));
        JS_SetPropertyStr(ctx, g, "__setTimeout",
                          JS_NewCFunction(ctx, &Impl::CbSetTimeout, "__setTimeout", 2));
        JS_SetPropertyStr(ctx, g, "__clearTimeout",
                          JS_NewCFunction(ctx, &Impl::CbClearTimeout, "__clearTimeout", 1));
        JS_FreeValue(ctx, g);
    }

    // Eval every `lib/*.js` sibling of the script (sorted, deterministic order)
    // after bootstrap but before the script, so shared libraries (e.g. lib/bt.js)
    // are available as globals. A parse error in a lib is fatal, like bootstrap.
    bool EvalLibs(const std::string& scriptPath) {
        namespace fs = std::filesystem;
        const std::string libDir = SiblingPath(scriptPath, "lib");
        std::error_code ec;
        std::vector<std::string> files;
        for (fs::directory_iterator it(libDir, ec), end; !ec && it != end;
             it.increment(ec)) {
            const fs::path p = it->path();
            if (p.extension() == ".js") files.push_back(p.string());
        }
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) {
            const std::string name = fs::path(f).filename().string();
            if (!EvalFile(f, name.c_str())) return false;
        }
        return true;
    }

    bool EvalFile(const std::string& path, const char* name) {
        std::string code;
        if (!ReadFile(path, code)) {
            std::fprintf(stderr, "[js error] cannot read script: %s\n", path.c_str());
            std::fflush(stderr);
            uo::LogMsg(uo::LogLevel::Error, uo::LogSink::File,
                       "[js] cannot read script: %s\n", path.c_str());
            return false;
        }
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), name, JS_EVAL_TYPE_GLOBAL);
        const bool ok = !JS_IsException(v);
        if (!ok) ReportException(ctx);
        JS_FreeValue(ctx, v);
        return ok;
    }

    void Tick() {
        if (!ctx) return;
        FireDueTimers();
        DrainJobs();
    }

    void FireDueTimers() {
        if (timers.empty()) return;
        const long long now = NowMs();
        std::vector<JSValue> due;
        for (std::size_t i = 0; i < timers.size();) {
            if (timers[i].dueMs <= now) {
                due.push_back(timers[i].cb);
                timers.erase(timers.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        for (JSValue cb : due) {
            JSValue ret = JS_Call(ctx, cb, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(ret)) ReportException(ctx);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, cb);
        }
    }

    void DrainJobs() {
        if (!rt) return;
        JSContext* c = nullptr;
        for (;;) {
            int r = JS_ExecutePendingJob(rt, &c);
            if (r == 0) break;          // no more pending jobs
            if (r < 0) ReportException(c ? c : ctx);  // job threw; keep draining
        }
    }

    // ----- error routing -------------------------------------------------
    static void ReportException(JSContext* c) {
        JSValue exc = JS_GetException(c);
        ReportValue(c, exc, "[js error] ");
        JS_FreeValue(c, exc);
    }

    static void ReportValue(JSContext* c, JSValueConst v, const char* prefix) {
        std::string out;
        const char* s = JS_ToCString(c, v);
        out = s ? s : "<unknown error>";
        if (s) JS_FreeCString(c, s);
        if (JS_IsError(v)) {
            JSValue stack = JS_GetPropertyStr(c, v, "stack");
            if (!JS_IsUndefined(stack)) {
                const char* st = JS_ToCString(c, stack);
                if (st) { out += "\n"; out += st; JS_FreeCString(c, st); }
            }
            JS_FreeValue(c, stack);
        }
        std::fprintf(stderr, "%s%s\n", prefix, out.c_str());
        std::fflush(stderr);
        uo::LogMsg(uo::LogLevel::Error, uo::LogSink::File, "[js] %s%s\n", prefix,
                   out.c_str());
    }

    static void Fail(const char* msg) {
        std::fprintf(stderr, "[js error] %s\n", msg);
        std::fflush(stderr);
        uo::LogMsg(uo::LogLevel::Error, uo::LogSink::File, "[js] %s\n", msg);
    }

    static bool ReadFile(const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
        return true;
    }

    // ----- console + timer natives --------------------------------------
    // Raw stream write (prelude supplies the joined message, no newline) plus a
    // mirror to the file log tagged [js]; log/info/warn/debug -> stdout, error
    // -> stderr.
    static void WriteConsole(std::FILE* stream, const char* text, bool isErr) {
        std::fputs(text, stream);
        std::fputc('\n', stream);
        std::fflush(stream);
        uo::LogMsg(isErr ? uo::LogLevel::Error : uo::LogLevel::Info,
                   uo::LogSink::File, "[js] %s\n", text);
    }

    static JSValue CbStdout(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
        if (argc >= 1) {
            const char* s = JS_ToCString(ctx, argv[0]);
            if (s) { WriteConsole(stdout, s, false); JS_FreeCString(ctx, s); }
        }
        return JS_UNDEFINED;
    }

    static JSValue CbStderr(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
        if (argc >= 1) {
            const char* s = JS_ToCString(ctx, argv[0]);
            if (s) { WriteConsole(stderr, s, true); JS_FreeCString(ctx, s); }
        }
        return JS_UNDEFINED;
    }

    static JSValue CbSetTimeout(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        if (!self || argc < 1 || !JS_IsFunction(ctx, argv[0]))
            return JS_ThrowTypeError(ctx, "setTimeout: callback function required");
        int32_t ms = 0;
        if (argc >= 2) JS_ToInt32(ctx, &ms, argv[1]);
        if (ms < 0) ms = 0;
        Timer t;
        t.id = self->nextTimerId++;
        t.dueMs = NowMs() + ms;
        t.cb = JS_DupValue(ctx, argv[0]);
        self->timers.push_back(t);
        return JS_NewInt32(ctx, t.id);
    }

    static JSValue CbClearTimeout(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv) {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        if (!self || argc < 1) return JS_UNDEFINED;
        int32_t id = -1;
        JS_ToInt32(ctx, &id, argv[0]);
        for (auto it = self->timers.begin(); it != self->timers.end(); ++it) {
            if (it->id == id) {
                JS_FreeValue(ctx, it->cb);
                self->timers.erase(it);
                break;
            }
        }
        return JS_UNDEFINED;
    }

    static void OnPromiseRejection(JSContext* ctx, JSValueConst /*promise*/,
                                   JSValueConst reason, bool is_handled,
                                   void* /*opaque*/) {
        if (is_handled) return;
        ReportValue(ctx, reason, "[js error] unhandled promise rejection: ");
    }
};

JsEngine::JsEngine() : impl_(std::make_unique<Impl>()) {}
JsEngine::~JsEngine() = default;

bool JsEngine::Run(const std::string& scriptPath) {
    if (!impl_->Start()) return false;
    const std::string bootstrapPath = SiblingPath(scriptPath, kBootstrapName);
    if (!impl_->EvalFile(bootstrapPath, "bootstrap.js")) {
        impl_->Stop();
        return false;
    }
    if (!impl_->EvalLibs(scriptPath)) {   // shared libs (scripts/js/lib/*.js)
        impl_->Stop();
        return false;
    }
    const bool ok = impl_->EvalFile(scriptPath, scriptPath.c_str());
    impl_->DrainJobs();  // run microtasks queued by the top-level eval
    if (ok) uo::LogInfo("[js] running %s\n", scriptPath.c_str());
    return ok;
}

void JsEngine::SetBindingInstaller(BindingInstaller fn) {
    impl_->installer = std::move(fn);
}

void JsEngine::SetBindingTeardown(std::function<void()> fn) {
    impl_->teardown = std::move(fn);
}

void JsEngine::Stop() { impl_->Stop(); }

void JsEngine::Tick() { impl_->Tick(); }

bool JsEngine::Running() const { return impl_->ctx != nullptr; }

}  // namespace uo::js
