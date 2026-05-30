#include "js/ClientBindings.h"

#include "Client.h"
#include "uo/world.h"

#include "quickjs.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace uo::js {
    // ClientBindings is a friend of Client (declared in Client.h), so its getters
    // read the live private player fields directly — no snapshot, no per-access
    // allocation. One Client per engine, so static binding state is enough.
    struct ClientBindings {
        static Client *client;
        static JSContext *context; // current runtime's context (reset each Run)

        // Pending promises: the goto op (PK_GOTO, settled by the trip callback)
        // plus one-shot event awaiters (PK_ONCE, keyed by event name, used by
        // Player.once / waitFor*). Each is settled exactly once, by its event or
        // by timeout.
        enum PendingKind { PK_GOTO, PK_ONCE };

        struct Pending {
            int kind;
            std::string event;    // PK_ONCE: event name to wait for
            JSValue resolve;
            JSValue reject;
            long long deadlineMs; // 0 = no timeout
            std::string stack;    // JS call-site stack for PK_ONCE rejections
        };

        static std::vector<Pending> pending;

        // Persistent Player.on(name, cb) subscriptions.
        struct Handler {
            std::string name;
            JSValue cb;
        };

        static std::vector<Handler> handlers;

        // Server-driven events are queued by Emit() and dispatched only in
        // TickEvents() (during the main tick), never synchronously inside packet
        // parsing — so a handler can't mutate caches mid-dispatch.
        struct QueuedEvent {
            std::string name;
            JSValue payload;
        };

        static std::vector<QueuedEvent> eventQueue;

        // ---- promise / error helpers ----------------------------------------

        // Capture JS stack from the current context.
        static JSValue CaptureStack(JSContext *ctx) {
            JSValue err = JS_NewError(ctx);
            JSValue stack = JS_GetPropertyStr(ctx, err, "stack");
            JS_FreeValue(ctx, err);
            return stack; // may be JS_UNDEFINED
        }

        static JSValue NewPending(JSContext *ctx, int kind, std::string event,
                                  long long deadlineMs) {
            JSValue funcs[2];
            JSValue promise = JS_NewPromiseCapability(ctx, funcs);
            if (JS_IsException(promise))
                return promise;
            JSValue stack = CaptureStack(ctx);
            std::string stackStr;
            if (JS_IsString(stack)) {
                const char *s = JS_ToCString(ctx, stack);
                if (s) { stackStr = s; JS_FreeCString(ctx, s); }
            }
            JS_FreeValue(ctx, stack);
            pending.push_back(Pending{kind, std::move(event), funcs[0], funcs[1], deadlineMs, std::move(stackStr)});
            return promise;
        }

        // Invoke resolve/reject with one optional arg (JS_UNDEFINED = no arg) and
        // free the entry's stored functions. Does NOT free `arg` (caller owns it).
        static void Settle(Pending &p, bool resolve, JSValueConst arg) {
            const JSValue fn = resolve ? p.resolve : p.reject;
            const int argc = JS_IsUndefined(arg) ? 0 : 1;
            JS_FreeValue(context, JS_Call(context, fn, JS_UNDEFINED, argc, &arg));
            JS_FreeValue(context, p.resolve);
            JS_FreeValue(context, p.reject);
            p.resolve = JS_UNDEFINED;
            p.reject = JS_UNDEFINED;
        }

        static JSValue MakeError(const char *reason, const std::string &stack = std::string()) {
            const char *msg = reason && *reason ? reason : "interrupted";
            JSValue err = JS_NewError(context);
            JS_SetPropertyStr(context, err, "message", JS_NewString(context, msg));
            JS_SetPropertyStr(context, err, "reason", JS_NewString(context, msg));
            if (!stack.empty()) {
                JS_SetPropertyStr(context, err, "stack", JS_NewString(context, stack.c_str()));
            }
            return err;
        }

        // ---- goto op-promise (separate from events) -------------------------

        // Settle the in-flight goto trip. On success also broadcasts `arrival`.
        static void SettleGoto(bool ok, const char *reason) {
            if (!context)
                return;
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (pending[i].kind != PK_GOTO)
                    continue;
                if (ok) {
                    Settle(pending[i], true, JS_UNDEFINED);
                } else {
                    JSValue err = MakeError(reason, pending[i].stack);
                    Settle(pending[i], false, err);
                    JS_FreeValue(context, err);
                }
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
            if (ok && client) {
                JSValue p = JS_NewObject(context);
                JS_SetPropertyStr(context, p, "x", JS_NewInt32(context, client->playerX_));
                JS_SetPropertyStr(context, p, "y", JS_NewInt32(context, client->playerY_));
                JS_SetPropertyStr(context, p, "z", JS_NewInt32(context, client->playerZ_));
                Emit("arrival", p);
            }
        }

        // ---- event system ---------------------------------------------------

        // Queue an event for dispatch in the next TickEvents(). Takes ownership
        // of `payload`. Callers must ensure context != nullptr.
        static void Emit(const char *name, JSValue payload) {
            eventQueue.push_back(QueuedEvent{name, payload});
        }

        static void ReportHandlerError(const char *name) {
            JSValue e = JS_GetException(context);
            const char *s = JS_ToCString(context, e);
            std::fprintf(stderr, "[js error] event handler '%s': %s\n", name, s ? s : "?");
            std::fflush(stderr);
            if (s) JS_FreeCString(context, s);
            JS_FreeValue(context, e);
        }

        // Dispatch queued events: persistent handlers (run synchronously, here in
        // the tick — the safe point) then one-shot awaiters (resolved). Events
        // emitted by a handler are processed on the next tick (we drain a batch).
        static void DrainEvents() {
            if (!context || eventQueue.empty())
                return;
            std::vector<QueuedEvent> batch;
            batch.swap(eventQueue);
            for (QueuedEvent &ev : batch) {
                // Snapshot matching handlers so on()/off() during a handler is safe.
                std::vector<JSValue> cbs;
                for (Handler &h : handlers)
                    if (h.name == ev.name)
                        cbs.push_back(JS_DupValue(context, h.cb));
                for (JSValue cb : cbs) {
                    JSValueConst arg = ev.payload;
                    JSValue r = JS_Call(context, cb, JS_UNDEFINED, 1, &arg);
                    if (JS_IsException(r))
                        ReportHandlerError(ev.name.c_str());
                    JS_FreeValue(context, r);
                    JS_FreeValue(context, cb);
                }
                for (std::size_t i = 0; i < pending.size();) {
                    if (pending[i].kind == PK_ONCE && pending[i].event == ev.name) {
                        Settle(pending[i], true, ev.payload);
                        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                    } else {
                        ++i;
                    }
                }
                JS_FreeValue(context, ev.payload);
            }
        }

        // Reject any pending op whose deadline passed (Player.once timeouts).
        static void SweepTimeouts(long long nowMs) {
            if (!context)
                return;
            for (std::size_t i = 0; i < pending.size();) {
                Pending &p = pending[i];
                if (p.deadlineMs > 0 && nowMs >= p.deadlineMs) {
                    JSValue err = MakeError("timeout", p.stack);
                    Settle(p, false, err);
                    JS_FreeValue(context, err);
                    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    ++i;
                }
            }
        }

        static void TickEvents(long long nowMs) {
            DrainEvents();
            SweepTimeouts(nowMs);
        }

        // ---- player getters -------------------------------------------------

        enum PlayerField {
            PF_SERIAL, PF_NAME, PF_X, PF_Y, PF_Z, PF_FACING, PF_RUNNING, PF_WARMODE,
            PF_ALIVE, PF_DEAD, PF_HP, PF_HPMAX, PF_MANA, PF_MANAMAX, PF_STAM, PF_STAMMAX,
            PF_WEIGHT, PF_MAXWEIGHT, PF_EQUIPMENT, PF_DIALOG
        };

        // Build { serial, items:[{serial,graphic,amount,hue,name}] } for a worn
        // container layer (e.g. backpack). items is empty until the container has
        // been opened (the server sends its 0x3C contents then).
        static JSValue ContainerView(JSContext *ctx, const Client *c, u8 layer) {
            JSValue o = JS_NewObject(ctx);
            const u32 serial = c->PlayerEquipSerialAt(layer);
            JS_SetPropertyStr(ctx, o, "serial", JS_NewInt64(ctx, serial));
            JSValue items = JS_NewArray(ctx);
            auto it = serial ? c->containerItems_.find(serial) : c->containerItems_.end();
            if (it != c->containerItems_.end()) {
                uint32_t i = 0;
                for (const Client::ContainerItem &ci : it->second) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "serial", JS_NewInt64(ctx, ci.serial));
                    JS_SetPropertyStr(ctx, e, "graphic", JS_NewInt32(ctx, ci.graphic));
                    JS_SetPropertyStr(ctx, e, "amount", JS_NewInt32(ctx, ci.amount));
                    JS_SetPropertyStr(ctx, e, "hue", JS_NewInt32(ctx, ci.hue));
                    const std::string nm = c->ItemNameLower(ci.graphic);
                    JS_SetPropertyStr(ctx, e, "name", JS_NewString(ctx, nm.c_str()));
                    JS_SetPropertyUint32(ctx, items, i++, e);
                }
            }
            JS_SetPropertyStr(ctx, o, "items", items);
            return o;
        }

        // Build { id, menuId, question, options:[{index,model,hue,text}] } from the
        // active 0x7C dialog, or JS null when none is open. Shared by the
        // Player.dialog getter and the `dialog` event payload.
        static JSValue DialogToJS(JSContext *ctx) {
            const Client *c = client;
            if (!c) return JS_NULL;
            const Client::ActiveDialog &d = c->Dialog();
            if (!d.active) return JS_NULL;
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "id", JS_NewInt64(ctx, d.id));
            JS_SetPropertyStr(ctx, o, "menuId", JS_NewInt32(ctx, d.menuId));
            JS_SetPropertyStr(ctx, o, "question", JS_NewString(ctx, d.question.c_str()));
            JSValue opts = JS_NewArray(ctx);
            uint32_t i = 0;
            for (const Client::DialogOption &opt : d.options) {
                JSValue e = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, e, "index", JS_NewInt32(ctx, static_cast<int32_t>(i + 1)));
                JS_SetPropertyStr(ctx, e, "model", JS_NewInt32(ctx, opt.model));
                JS_SetPropertyStr(ctx, e, "hue", JS_NewInt32(ctx, opt.hue));
                JS_SetPropertyStr(ctx, e, "text", JS_NewString(ctx, opt.text.c_str()));
                JS_SetPropertyUint32(ctx, opts, i++, e);
            }
            JS_SetPropertyStr(ctx, o, "options", opts);
            return o;
        }

        // Build { vendor, items:[{serial,graphic,amount,price,layer,name}] } from
        // the just-assembled pendingVendor_ rows (joined 0x3C + 0x74). Used for the
        // `vendor_buy` event payload. Member so it can read Client's private state
        // (ClientBindings is a friend of Client).
        static JSValue VendorOfferToJS(JSContext *ctx, unsigned vendor) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "vendor", JS_NewInt64(ctx, vendor));
            JSValue items = JS_NewArray(ctx);
            if (client) {
                uint32_t i = 0;
                for (const Client::VendorItem &vi : client->pendingVendor_) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "serial", JS_NewInt64(ctx, vi.serial));
                    JS_SetPropertyStr(ctx, e, "graphic", JS_NewInt32(ctx, vi.graphic));
                    JS_SetPropertyStr(ctx, e, "amount", JS_NewInt32(ctx, vi.amount));
                    JS_SetPropertyStr(ctx, e, "price", JS_NewInt64(ctx, vi.price));
                    JS_SetPropertyStr(ctx, e, "layer", JS_NewInt32(ctx, vi.layer));
                    JS_SetPropertyStr(ctx, e, "name", JS_NewString(ctx, vi.name.c_str()));
                    JS_SetPropertyUint32(ctx, items, i++, e);
                }
            }
            JS_SetPropertyStr(ctx, o, "items", items);
            return o;
        }

        static JSValue PlayerGet(JSContext *ctx, JSValueConst /*this_val*/, int magic) {
            const Client *c = client;
            if (!c) return JS_UNDEFINED;
            switch (magic) {
                case PF_SERIAL: return JS_NewInt64(ctx, c->playerSerial_);
                case PF_NAME: return JS_NewString(ctx, c->player_.name.c_str());
                case PF_X: return JS_NewInt32(ctx, c->playerX_);
                case PF_Y: return JS_NewInt32(ctx, c->playerY_);
                case PF_Z: return JS_NewInt32(ctx, c->playerZ_);
                case PF_FACING: return JS_NewInt32(ctx, c->playerFacing_);
                case PF_RUNNING: return JS_NewBool(ctx, c->playerRunning_);
                case PF_WARMODE: return JS_NewBool(ctx, c->playerWarMode_);
                // Death == ghost body (Mobile_IsGhostForm @0x4c6930), authoritative
                // over hp (which can read 0 transiently before stats arrive).
                case PF_ALIVE: return JS_NewBool(ctx, !c->PlayerIsGhost());
                case PF_DEAD: return JS_NewBool(ctx, c->PlayerIsGhost());
                case PF_HP: return JS_NewInt32(ctx, c->player_.hpCur);
                case PF_HPMAX: return JS_NewInt32(ctx, c->player_.hpMax);
                case PF_MANA: return JS_NewInt32(ctx, c->player_.manaCur);
                case PF_MANAMAX: return JS_NewInt32(ctx, c->player_.manaMax);
                case PF_STAM: return JS_NewInt32(ctx, c->player_.stamCur);
                case PF_STAMMAX: return JS_NewInt32(ctx, c->player_.stamMax);
                case PF_WEIGHT: return JS_NewInt32(ctx, c->player_.weight);
                case PF_MAXWEIGHT: return JS_NewInt32(ctx, c->player_.maxWeight);
                case PF_EQUIPMENT: {
                    JSValue eq = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, eq, "backpack",
                                      ContainerView(ctx, c, 0x15));  // kLayerBackpack
                    return eq;
                }
                case PF_DIALOG: return DialogToJS(ctx);
                default: return JS_UNDEFINED;
            }
        }

        // ---- actions --------------------------------------------------------

        // Player.goto(x, y[, z]) -> Promise. Resolves on arrival (and emits the
        // `arrival` event), rejects on any abort with an Error whose `.reason`
        // carries the C++ abort reason.
        static JSValue Goto(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.goto: no client");
            int32_t x, y, z = 0;
            bool hasZ = false;
            bool terrain = true;
            if (argc < 2 || JS_ToInt32(ctx, &x, argv[0]) || JS_ToInt32(ctx, &y, argv[1]))
                return JS_ThrowTypeError(ctx, "Player.goto(x, y[, z][, opts]) expects numbers");
            // Trailing args: a number is z, an object is opts ({ terrain: bool }).
            for (int i = 2; i < argc; ++i) {
                if (JS_IsNull(argv[i]) || JS_IsUndefined(argv[i]))
                    continue;
                if (JS_IsObject(argv[i])) {
                    JSValue t = JS_GetPropertyStr(ctx, argv[i], "terrain");
                    if (!JS_IsUndefined(t))
                        terrain = JS_ToBool(ctx, t) > 0;
                    JS_FreeValue(ctx, t);
                } else {
                    if (JS_ToInt32(ctx, &z, argv[i]))
                        return JS_EXCEPTION;
                    hasZ = true;
                }
            }
            client->BotSetDoneCb([](bool ok, const char *reason) { SettleGoto(ok, reason); });
            JSValue promise = NewPending(ctx, PK_GOTO, std::string(), 0);
            if (JS_IsException(promise))
                return promise;
            client->BotStartGoto(x, y, hasZ, z, terrain);
            return promise;
        }

        // Player.say(text): speak a line (0x03), e.g. "bank".
        static JSValue Say(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.say(text)");
            const char *s = JS_ToCString(ctx, argv[0]);
            if (!s) return JS_EXCEPTION;
            client->SayAscii(s);
            JS_FreeCString(ctx, s);
            return JS_UNDEFINED;
        }

        // Player.attack(serial): send 0x05 attack request at a mobile.
        static JSValue Attack(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.attack(serial)");
            int64_t s = 0;
            if (JS_ToInt64(ctx, &s, argv[0]))
                return JS_EXCEPTION;
            client->SendAttack(static_cast<u32>(s));
            return JS_UNDEFINED;
        }

        // Player.follow(serial[, distance]): track a mobile via the bot's path
        // follower (keeps us within `distance`, default 1 = melee). Used during a
        // fight to pursue the foe and hold facing on it. Player.follow(false|0|null)
        // stops following. Reuses BotStartFollow/BotStopFollow.
        static JSValue Follow(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.follow(serial[, distance])");
            int64_t serial = 0;
            if (argc >= 1 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]) &&
                !JS_IsBool(argv[0])) {
                if (JS_ToInt64(ctx, &serial, argv[0]))
                    return JS_EXCEPTION;
            }
            if (serial <= 0) {                 // follow(false) / follow(0) -> stop
                client->BotStopFollow("follow off (js)");
                return JS_UNDEFINED;
            }
            int32_t dist = 1;
            if (argc >= 2 && !JS_IsUndefined(argv[1]) && JS_ToInt32(ctx, &dist, argv[1]))
                return JS_EXCEPTION;
            client->BotStartFollow(static_cast<u32>(serial) & 0x7FFFFFFFu,
                                   dist > 0 ? static_cast<u32>(dist) : 1u);
            return JS_UNDEFINED;
        }

        // Player.requestStatus(serial): send a 0x34 status query for a mobile. The
        // server replies with its HP (0x11) and then auto-pushes 0xA1 HP updates
        // for it — so afterwards Mobiles.get(serial).hp/.hpPct stay live. Passive
        // (does not aggro the target), unlike attack.
        static JSValue RequestStatus(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.requestStatus(serial)");
            int64_t s = 0;
            if (JS_ToInt64(ctx, &s, argv[0]))
                return JS_EXCEPTION;
            client->SendStatusRequest(static_cast<u32>(s));
            return JS_UNDEFINED;
        }

        // Player.stop(): abort any in-flight goto path and stop following. This is
        // the JS-side cancel primitive — it makes a parked Player.goto() reject, so
        // a behaviour step can be preempted cleanly. Mirrors the `stop` console cmd.
        static JSValue Stop(JSContext *ctx, JSValueConst, int, JSValueConst *) {
            if (!client) return JS_ThrowTypeError(ctx, "Player.stop()");
            client->BotStopFollow("stop (js)");
            client->BotAbortPath("stop (js)");
            return JS_UNDEFINED;
        }

        // Player.setWarMode(on): toggle war mode (0x72). Read state via Player.warMode.
        static JSValue SetWarMode(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.setWarMode(on)");
            const bool on = argc >= 1 && JS_ToBool(ctx, argv[0]) > 0;
            client->SetWarMode(on);
            return JS_UNDEFINED;
        }

        // Player.resurrect([choice]): answer the 0x2C resurrection menu. Default
        // choice 1 = resurrect; pass 2 to remain a ghost.
        static JSValue Resurrect(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.resurrect()");
            int32_t choice = 1;
            if (argc >= 1 && JS_ToInt32(ctx, &choice, argv[0]))
                return JS_EXCEPTION;
            client->SendResurrectChoice(static_cast<u8>(choice));
            return JS_UNDEFINED;
        }

        // Player.dialogRespond(index): answer the active 0x7C dialog by 1-based
        // option index (0 = cancel). model/hue are taken from the stored option.
        static JSValue DialogRespond(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.dialogRespond(index)");
            int32_t index = 0;
            if (argc >= 1 && JS_ToInt32(ctx, &index, argv[0]))
                return JS_EXCEPTION;
            client->AnswerDialog(static_cast<u16>(index));
            return JS_UNDEFINED;
        }

        // Player.drop(target, container): move a bag item (by serial / graphic /
        // name) into a container serial. Reuses the console drop handler.
        static JSValue Drop(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 2)
                return JS_ThrowTypeError(ctx, "Player.drop(target, container)");
            const char *tgt = JS_ToCString(ctx, argv[0]);
            if (!tgt) return JS_EXCEPTION;
            int64_t container = 0;
            if (JS_ToInt64(ctx, &container, argv[1])) {
                JS_FreeCString(ctx, tgt);
                return JS_EXCEPTION;
            }
            char arg[160];
            std::snprintf(arg, sizeof(arg), "%s 0x%X", tgt,
                          static_cast<unsigned>(static_cast<uint32_t>(container)));
            JS_FreeCString(ctx, tgt);
            client->HandleDropCommand(arg);
            return JS_UNDEFINED;
        }

        // Player.use(target): double-click an item by serial, graphic id, or name.
        static JSValue Use(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.use(target)");
            const char *s = JS_ToCString(ctx, argv[0]);
            if (!s) return JS_EXCEPTION;
            client->HandleUseCommand(s);
            JS_FreeCString(ctx, s);
            return JS_UNDEFINED;
        }

        // Player.doubleClick(serial): raw 0x06 by serial (no item resolution).
        // Use for mobiles — double-clicking an NPC opens its paperdoll, which
        // arrives as 0x88 and fires the `paperdoll` event with its title.
        static JSValue DoubleClick(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.doubleClick(serial)");
            int64_t s = 0;
            if (JS_ToInt64(ctx, &s, argv[0]))
                return JS_EXCEPTION;
            client->SendDoubleClick(static_cast<u32>(s));
            return JS_UNDEFINED;
        }

        // Player.take(serial[, qty]): lift `qty` (default 0 = whole stack) of item
        // `serial` from whatever open container holds it and drop into the backpack.
        // Use to withdraw gold or other items from the bank box.
        static JSValue Take(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.take(serial[, qty])");
            int64_t serial = 0;
            if (JS_ToInt64(ctx, &serial, argv[0]))
                return JS_EXCEPTION;
            int32_t qty = 0;
            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
                JS_ToInt32(ctx, &qty, argv[1]);
            client->SendTakeToBackpack(static_cast<u32>(serial),
                                       static_cast<u16>(qty < 0 ? 0 : qty));
            return JS_UNDEFINED;
        }

        // Player.containerItems(serial): items array from any open container (e.g.
        // the bank box after saying "bank"). Returns [] when not open / no items yet.
        static JSValue ContainerItems(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_NewArray(ctx);
            int64_t serial = 0;
            if (JS_ToInt64(ctx, &serial, argv[0]))
                return JS_EXCEPTION;
            JSValue arr = JS_NewArray(ctx);
            const u32 ser = static_cast<u32>(serial);
            auto it = client->containerItems_.find(ser);
            if (it != client->containerItems_.end()) {
                uint32_t i = 0;
                for (const Client::ContainerItem &ci : it->second) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "serial", JS_NewInt64(ctx, ci.serial));
                    JS_SetPropertyStr(ctx, e, "graphic", JS_NewInt32(ctx, ci.graphic));
                    JS_SetPropertyStr(ctx, e, "amount", JS_NewInt32(ctx, ci.amount));
                    JS_SetPropertyStr(ctx, e, "hue", JS_NewInt32(ctx, ci.hue));
                    const std::string nm = client->ItemNameLower(ci.graphic);
                    JS_SetPropertyStr(ctx, e, "name", JS_NewString(ctx, nm.c_str()));
                    JS_SetPropertyUint32(ctx, arr, i++, e);
                }
            }
            return arr;
        }

        // Player.equip(serial|name): wear an item from the backpack (or world).
        // A number is treated as an object serial; a string as a graphic/name
        // token (same rules as the `equip` console command). Reuses HandleEquipCommand.
        static JSValue Equip(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 1)
                return JS_ThrowTypeError(ctx, "Player.equip(serial|name)");
            char arg[160];
            if (JS_IsNumber(argv[0])) {
                int64_t s = 0;
                if (JS_ToInt64(ctx, &s, argv[0]))
                    return JS_EXCEPTION;
                std::snprintf(arg, sizeof(arg), "0x%X",
                              static_cast<unsigned>(static_cast<uint32_t>(s)));
                client->HandleEquipCommand(arg);
            } else {
                const char *s = JS_ToCString(ctx, argv[0]);
                if (!s) return JS_EXCEPTION;
                client->HandleEquipCommand(s);
                JS_FreeCString(ctx, s);
            }
            return JS_UNDEFINED;
        }

        // Player.target(serial)        -> object reply;
        // Player.target(x, y[, z])      -> ground tile reply (modelID 0);
        // Player.target(x, y, z, graphic) -> static reply (tree etc.).
        static JSValue Target(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "Player.target: no client");
            if (argc == 1) {
                int64_t serial = 0;
                if (JS_ToInt64(ctx, &serial, argv[0]))
                    return JS_EXCEPTION;
                client->TargetRespondObject(static_cast<u32>(serial));
                return JS_UNDEFINED;
            }
            if (argc >= 2) {
                int32_t x = 0, y = 0, z = 0, graphic = 0;
                if (JS_ToInt32(ctx, &x, argv[0]) || JS_ToInt32(ctx, &y, argv[1]))
                    return JS_EXCEPTION;
                const bool hasZ = (argc >= 3 && !JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]));
                if (hasZ && JS_ToInt32(ctx, &z, argv[2]))
                    return JS_EXCEPTION;
                if (argc >= 4 && !JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3])) {
                    if (JS_ToInt32(ctx, &graphic, argv[3]))
                        return JS_EXCEPTION;
                    client->TargetRespondStatic(x, y, static_cast<i8>(z),
                                                static_cast<u16>(graphic));
                    return JS_UNDEFINED;
                }
                client->TargetRespondGround(x, y, hasZ, static_cast<i8>(z));
                return JS_UNDEFINED;
            }
            return JS_ThrowTypeError(ctx, "Player.target(serial) | (x,y[,z]) | (x,y,z,graphic)");
        }

        // Vendor.buy(vendorSerial, [{serial, qty[, layer]}]) -> number of rows
        // sent. Builds the 0x3B buy packet (layer defaults to 0x1A = stock).
        // `serial` is a stock item serial from a `vendor_buy` event row.
        static JSValue VendorBuy(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client || argc < 2)
                return JS_ThrowTypeError(ctx, "Vendor.buy(vendorSerial, items)");
            int64_t vendor = 0;
            if (JS_ToInt64(ctx, &vendor, argv[0]))
                return JS_EXCEPTION;
            uint32_t len = 0;
            JSValue lenv = JS_GetPropertyStr(ctx, argv[1], "length");
            JS_ToUint32(ctx, &len, lenv);
            JS_FreeValue(ctx, lenv);
            std::vector<Client::VendorBuyReq> reqs;
            for (uint32_t i = 0; i < len; ++i) {
                JSValue e = JS_GetPropertyUint32(ctx, argv[1], i);
                int64_t serial = 0;
                int32_t qty = 0, layer = 0;
                JSValue sv = JS_GetPropertyStr(ctx, e, "serial");
                JS_ToInt64(ctx, &serial, sv); JS_FreeValue(ctx, sv);
                JSValue qv = JS_GetPropertyStr(ctx, e, "qty");
                JS_ToInt32(ctx, &qty, qv); JS_FreeValue(ctx, qv);
                JSValue lv = JS_GetPropertyStr(ctx, e, "layer");
                if (!JS_IsUndefined(lv)) JS_ToInt32(ctx, &layer, lv);
                JS_FreeValue(ctx, lv);
                JS_FreeValue(ctx, e);
                if (serial && qty > 0)
                    reqs.push_back(Client::VendorBuyReq{static_cast<u32>(serial),
                                                        static_cast<u16>(qty),
                                                        static_cast<u8>(layer)});
            }
            client->SendVendorBuy(static_cast<u32>(vendor), reqs);
            return JS_NewInt32(ctx, static_cast<int32_t>(reqs.size()));
        }

        // ---- events: on / off / once ----------------------------------------

        static JSValue On(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1]))
                return JS_ThrowTypeError(ctx, "Player.on(name, callback)");
            const char *name = JS_ToCString(ctx, argv[0]);
            if (!name) return JS_EXCEPTION;
            handlers.push_back(Handler{name, JS_DupValue(ctx, argv[1])});
            JS_FreeCString(ctx, name);
            return JS_UNDEFINED;
        }

        static JSValue Off(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (argc < 1 || !JS_IsString(argv[0]))
                return JS_ThrowTypeError(ctx, "Player.off(name[, callback])");
            const char *name = JS_ToCString(ctx, argv[0]);
            if (!name) return JS_EXCEPTION;
            const bool hasCb = (argc >= 2 && JS_IsFunction(ctx, argv[1]));
            for (std::size_t i = 0; i < handlers.size();) {
                if (handlers[i].name == name &&
                    (!hasCb || JS_IsSameValue(ctx, handlers[i].cb, argv[1]))) {
                    JS_FreeValue(ctx, handlers[i].cb);
                    handlers.erase(handlers.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    ++i;
                }
            }
            JS_FreeCString(ctx, name);
            return JS_UNDEFINED;
        }

        // Player.once(name[, ms]) -> Promise resolved with the next `name` event's
        // payload, rejected on timeout. (waitForTarget/waitForJournal sugar.)
        static JSValue Once(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (argc < 1 || !JS_IsString(argv[0]))
                return JS_ThrowTypeError(ctx, "Player.once(name[, ms])");
            const char *name = JS_ToCString(ctx, argv[0]);
            if (!name) return JS_EXCEPTION;
            int32_t ms = 0;
            if (argc >= 2 && !JS_IsUndefined(argv[1]))
                JS_ToInt32(ctx, &ms, argv[1]);
            const long long dl = (ms > 0 && client) ? client->NowMs() + ms : 0;
            JSValue promise = NewPending(ctx, PK_ONCE, std::string(name), dl);
            JS_FreeCString(ctx, name);
            return promise;
        }

        // ---- world queries --------------------------------------------------

        // World.statics(x, y[, radius=8]) -> [{x,y,z,graphic,name}] static tiles
        // in the square around (x,y). `name` is the lowercased tiledata name
        // (for identifying trees etc.); graphic is the raw tile id.
        static JSValue WorldStatics(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "World.statics: no client");
            int32_t x, y, radius = 8;
            if (argc < 2 || JS_ToInt32(ctx, &x, argv[0]) || JS_ToInt32(ctx, &y, argv[1]))
                return JS_ThrowTypeError(ctx, "World.statics(x, y[, radius])");
            if (argc >= 3 && !JS_IsUndefined(argv[2]))
                JS_ToInt32(ctx, &radius, argv[2]);
            if (radius < 0) radius = 0;

            JSValue arr = JS_NewArray(ctx);
            if (!client->EnsureWorldLoaded() || !client->world_)
                return arr;

            std::vector<world::StaticHit> hits;
            client->world_->CollectStatics(x, y, radius, hits);
            uint32_t i = 0;
            for (const world::StaticHit &h : hits) {
                JSValue o = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, h.x));
                JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, h.y));
                JS_SetPropertyStr(ctx, o, "z", JS_NewInt32(ctx, h.z));
                JS_SetPropertyStr(ctx, o, "graphic", JS_NewInt32(ctx, h.itemId));
                const std::string nm = client->ItemNameLower(h.itemId);
                JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, nm.c_str()));
                JS_SetPropertyUint32(ctx, arr, i++, o);
            }
            return arr;
        }

        // World.markStump(x, y, z, graphic[, ttlMs]) -> mark a chopped tree as a
        // stump in the world view for ttlMs (default 10min). The stump graphic
        // is chosen by the client; `graphic` is the live tree static to replace.
        static JSValue WorldMarkStump(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (!client)
                return JS_ThrowTypeError(ctx, "World.markStump: no client");
            int32_t x, y, z, graphic;
            if (argc < 4 ||
                JS_ToInt32(ctx, &x, argv[0]) || JS_ToInt32(ctx, &y, argv[1]) ||
                JS_ToInt32(ctx, &z, argv[2]) || JS_ToInt32(ctx, &graphic, argv[3]))
                return JS_ThrowTypeError(ctx, "World.markStump(x, y, z, graphic[, ttlMs])");
            int32_t ttlMs = 0;
            if (argc >= 5 && !JS_IsUndefined(argv[4]))
                JS_ToInt32(ctx, &ttlMs, argv[4]);
            client->MarkStump(x, y, static_cast<i8>(z),
                              static_cast<u16>(graphic), ttlMs);
            return JS_UNDEFINED;
        }

        // ---- mobiles (live handles) -----------------------------------------

        enum MobileField {
            MOB_X, MOB_Y, MOB_Z, MOB_DIR, MOB_BODY, MOB_HUE, MOB_NOTO,
            MOB_RUNNING, MOB_WARMODE, MOB_NAME, MOB_EXISTS, MOB_LASTANIM, MOB_ANIMAGO,
            MOB_HP, MOB_HPMAX, MOB_HPPCT, MOB_TITLE
        };

        // A Mobile handle stores only its serial; every field re-resolves the
        // live cache, so the handle never dangles. Read `.exists` for liveness.
        static JSValue MobileGet(JSContext *ctx, JSValueConst this_val, int magic) {
            if (!client) return JS_UNDEFINED;
            JSValue sv = JS_GetPropertyStr(ctx, this_val, "serial");
            int64_t s = 0;
            JS_ToInt64(ctx, &s, sv);
            JS_FreeValue(ctx, sv);
            const Client::MobileObj *m = client->FindMobileBySerial(static_cast<u32>(s));
            if (!m) return (magic == MOB_EXISTS) ? JS_FALSE : JS_UNDEFINED;
            switch (magic) {
                case MOB_X: return JS_NewInt32(ctx, m->x);
                case MOB_Y: return JS_NewInt32(ctx, m->y);
                case MOB_Z: return JS_NewInt32(ctx, m->z);
                case MOB_DIR: return JS_NewInt32(ctx, m->dir);
                case MOB_BODY: return JS_NewInt32(ctx, m->body);
                case MOB_HUE: return JS_NewInt32(ctx, m->hue);
                case MOB_NOTO: return JS_NewInt32(ctx, m->noto);
                case MOB_RUNNING: return JS_NewBool(ctx, m->running);
                case MOB_WARMODE: return JS_NewBool(ctx, m->warMode);
                case MOB_NAME: {
                    const char *nm = client->MobileName(m->serial);
                    return JS_NewString(ctx, nm ? nm : "");
                }
                // Paperdoll title ("<name> the <job>"), known only after we
                // double-click (Player.use) the mobile. "" until then. Used to
                // pick the right vendor by job.
                case MOB_TITLE: {
                    const char *t = client->PaperdollTitle(m->serial);
                    return JS_NewString(ctx, t ? t : "");
                }
                case MOB_EXISTS: return JS_TRUE;
                // Last action animation (0x6E): swing/cast/get-hit etc. lastAnim is
                // the action code (-1 if none); animMsAgo is ms since it played
                // (-1 if none) — a fresh combat animation is a strong threat signal.
                case MOB_LASTANIM:
                    return JS_NewInt32(ctx, m->lastAnimMs ? m->lastAnimAction : -1);
                case MOB_ANIMAGO:
                    return JS_NewInt64(ctx, m->lastAnimMs ? (client->NowMs() - m->lastAnimMs) : -1);
                // Health, learned from 0xA1/0x2D once the server sends this mob's bar
                // (usually only while we are fighting/targeting it). -1 = unknown.
                // hpPct is 0..1 (cur/max), or -1 when unknown.
                case MOB_HP: return JS_NewInt32(ctx, m->hpCur);
                case MOB_HPMAX: return JS_NewInt32(ctx, m->hpMax);
                case MOB_HPPCT:
                    return (m->hpCur >= 0 && m->hpMax > 0)
                               ? JS_NewFloat64(ctx, static_cast<double>(m->hpCur) / m->hpMax)
                               : JS_NewInt32(ctx, -1);
                default: return JS_UNDEFINED;
            }
        }

        // Build a live handle: a `serial` data field plus the re-resolving getters.
        static JSValue NewMobileHandle(JSContext *ctx, u32 serial) {
            static const JSCFunctionListEntry api[] = {
                JS_CGETSET_MAGIC_DEF("x", MobileGet, nullptr, MOB_X),
                JS_CGETSET_MAGIC_DEF("y", MobileGet, nullptr, MOB_Y),
                JS_CGETSET_MAGIC_DEF("z", MobileGet, nullptr, MOB_Z),
                JS_CGETSET_MAGIC_DEF("dir", MobileGet, nullptr, MOB_DIR),
                JS_CGETSET_MAGIC_DEF("body", MobileGet, nullptr, MOB_BODY),
                JS_CGETSET_MAGIC_DEF("hue", MobileGet, nullptr, MOB_HUE),
                JS_CGETSET_MAGIC_DEF("notoriety", MobileGet, nullptr, MOB_NOTO),
                JS_CGETSET_MAGIC_DEF("running", MobileGet, nullptr, MOB_RUNNING),
                JS_CGETSET_MAGIC_DEF("warMode", MobileGet, nullptr, MOB_WARMODE),
                JS_CGETSET_MAGIC_DEF("name", MobileGet, nullptr, MOB_NAME),
                JS_CGETSET_MAGIC_DEF("exists", MobileGet, nullptr, MOB_EXISTS),
                JS_CGETSET_MAGIC_DEF("lastAnim", MobileGet, nullptr, MOB_LASTANIM),
                JS_CGETSET_MAGIC_DEF("animMsAgo", MobileGet, nullptr, MOB_ANIMAGO),
                JS_CGETSET_MAGIC_DEF("hp", MobileGet, nullptr, MOB_HP),
                JS_CGETSET_MAGIC_DEF("hpMax", MobileGet, nullptr, MOB_HPMAX),
                JS_CGETSET_MAGIC_DEF("hpPct", MobileGet, nullptr, MOB_HPPCT),
                JS_CGETSET_MAGIC_DEF("title", MobileGet, nullptr, MOB_TITLE),
            };
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "serial", JS_NewInt64(ctx, serial));
            JS_SetPropertyFunctionList(ctx, o, api, static_cast<int>(std::size(api)));
            return o;
        }

        // Mobiles.get(serial) -> live handle (always; check .exists for liveness).
        static JSValue MobilesGet(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
            if (argc < 1)
                return JS_ThrowTypeError(ctx, "Mobiles.get(serial)");
            int64_t s = 0;
            if (JS_ToInt64(ctx, &s, argv[0]))
                return JS_EXCEPTION;
            return NewMobileHandle(ctx, static_cast<u32>(s));
        }

        // Mobiles.all() -> array of live handles for every cached mobile (not us).
        static JSValue MobilesAll(JSContext *ctx, JSValueConst, int, JSValueConst *) {
            JSValue arr = JS_NewArray(ctx);
            if (!client) return arr;
            uint32_t i = 0;
            for (const Client::MobileObj &m : client->mobileCache_) {
                if (m.serial == client->playerSerial_) continue;
                JS_SetPropertyUint32(ctx, arr, i++, NewMobileHandle(ctx, m.serial));
            }
            return arr;
        }

        // ---- lifecycle ------------------------------------------------------

        // Teardown (Stop / hot-reload): free every JSValue we hold while the
        // context is still alive, drop the Client callback, and null the context
        // so any later event/settle becomes a no-op.
        static void Detach() {
            if (context) {
                for (Pending &p : pending) {
                    JS_FreeValue(context, p.resolve);
                    JS_FreeValue(context, p.reject);
                }
                for (Handler &h : handlers)
                    JS_FreeValue(context, h.cb);
                for (QueuedEvent &e : eventQueue)
                    JS_FreeValue(context, e.payload);
            }
            pending.clear();
            handlers.clear();
            eventQueue.clear();
            if (client)
                client->BotClearDoneCb();
            context = nullptr;
        }

        // Rebind to a fresh runtime (already Detached, or first run).
        static void Reset(JSContext *newCtx, Client *newClient) {
            client = newClient;
            context = newCtx;
            pending.clear();
            handlers.clear();
            eventQueue.clear();
            if (newClient)
                newClient->BotClearDoneCb();
        }
    };

    Client *ClientBindings::client = nullptr;
    JSContext *ClientBindings::context = nullptr;
    std::vector<ClientBindings::Pending> ClientBindings::pending;
    std::vector<ClientBindings::Handler> ClientBindings::handlers;
    std::vector<ClientBindings::QueuedEvent> ClientBindings::eventQueue;

    namespace {
        const JSCFunctionListEntry kPlayerApi[] = {
            JS_CGETSET_MAGIC_DEF("serial", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_SERIAL),
            JS_CGETSET_MAGIC_DEF("name", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_NAME),
            JS_CGETSET_MAGIC_DEF("x", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_X),
            JS_CGETSET_MAGIC_DEF("y", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_Y),
            JS_CGETSET_MAGIC_DEF("z", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_Z),
            JS_CGETSET_MAGIC_DEF("facing", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_FACING),
            JS_CGETSET_MAGIC_DEF("running", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_RUNNING),
            JS_CGETSET_MAGIC_DEF("warMode", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_WARMODE),
            JS_CGETSET_MAGIC_DEF("alive", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_ALIVE),
            JS_CGETSET_MAGIC_DEF("dead", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_DEAD),
            JS_CGETSET_MAGIC_DEF("dialog", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_DIALOG),
            JS_CGETSET_MAGIC_DEF("hp", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_HP),
            JS_CGETSET_MAGIC_DEF("hpMax", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_HPMAX),
            JS_CGETSET_MAGIC_DEF("mana", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_MANA),
            JS_CGETSET_MAGIC_DEF("manaMax", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_MANAMAX),
            JS_CGETSET_MAGIC_DEF("stam", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_STAM),
            JS_CGETSET_MAGIC_DEF("stamMax", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_STAMMAX),
            JS_CGETSET_MAGIC_DEF("weight", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_WEIGHT),
            JS_CGETSET_MAGIC_DEF("maxWeight", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_MAXWEIGHT),
            JS_CGETSET_MAGIC_DEF("equipment", ClientBindings::PlayerGet, nullptr, ClientBindings::PF_EQUIPMENT),

            // Actions
            JS_CFUNC_DEF("goto", 2, ClientBindings::Goto),
            JS_CFUNC_DEF("use", 1, ClientBindings::Use),
            JS_CFUNC_DEF("doubleClick", 1, ClientBindings::DoubleClick),
            JS_CFUNC_DEF("take", 1, ClientBindings::Take),
            JS_CFUNC_DEF("containerItems", 1, ClientBindings::ContainerItems),
            JS_CFUNC_DEF("equip", 1, ClientBindings::Equip),
            JS_CFUNC_DEF("target", 1, ClientBindings::Target),
            JS_CFUNC_DEF("say", 1, ClientBindings::Say),
            JS_CFUNC_DEF("drop", 2, ClientBindings::Drop),
            JS_CFUNC_DEF("attack", 1, ClientBindings::Attack),
            JS_CFUNC_DEF("requestStatus", 1, ClientBindings::RequestStatus),
            JS_CFUNC_DEF("follow", 1, ClientBindings::Follow),
            JS_CFUNC_DEF("stop", 0, ClientBindings::Stop),
            JS_CFUNC_DEF("setWarMode", 1, ClientBindings::SetWarMode),
            JS_CFUNC_DEF("resurrect", 0, ClientBindings::Resurrect),
            JS_CFUNC_DEF("dialogRespond", 1, ClientBindings::DialogRespond),

            // Events: Player.on(name, cb) / off(name[, cb]) / once(name[, ms])
            JS_CFUNC_DEF("on", 2, ClientBindings::On),
            JS_CFUNC_DEF("off", 1, ClientBindings::Off),
            JS_CFUNC_DEF("once", 1, ClientBindings::Once),
        };

        const JSCFunctionListEntry kWorldApi[] = {
            JS_CFUNC_DEF("statics", 2, ClientBindings::WorldStatics),
            JS_CFUNC_DEF("markStump", 4, ClientBindings::WorldMarkStump),
            // Same shared event registry as Player.on — events are global by
            // name; World.on('container_open'/...) reads naturally for world events.
            JS_CFUNC_DEF("on", 2, ClientBindings::On),
            JS_CFUNC_DEF("off", 1, ClientBindings::Off),
            JS_CFUNC_DEF("once", 1, ClientBindings::Once),
        };

        const JSCFunctionListEntry kVendorApi[] = {
            JS_CFUNC_DEF("buy", 2, ClientBindings::VendorBuy),
            // Shared event registry: Vendor.once('vendor_buy') / .on('vendor_done').
            JS_CFUNC_DEF("on", 2, ClientBindings::On),
            JS_CFUNC_DEF("off", 1, ClientBindings::Off),
            JS_CFUNC_DEF("once", 1, ClientBindings::Once),
        };
    } // namespace

    void InstallClientBindings(JSContext *ctx, Client *client) {
        ClientBindings::Reset(ctx, client); // rebind to this fresh runtime

        const JSValue global = JS_GetGlobalObject(ctx);
        const JSValue player = JS_NewObject(ctx);

        JS_SetPropertyFunctionList(ctx, player, kPlayerApi, std::size(kPlayerApi));
        JS_SetPropertyStr(ctx, global, "Player", player); // consumes player

        const JSValue world = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, world, kWorldApi, std::size(kWorldApi));
        JS_SetPropertyStr(ctx, global, "World", world); // consumes world

        const JSValue mobiles = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mobiles, "get",
                          JS_NewCFunction(ctx, ClientBindings::MobilesGet, "get", 1));
        JS_SetPropertyStr(ctx, mobiles, "all",
                          JS_NewCFunction(ctx, ClientBindings::MobilesAll, "all", 0));
        JS_SetPropertyStr(ctx, global, "Mobiles", mobiles); // consumes mobiles

        const JSValue vendor = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, vendor, kVendorApi, std::size(kVendorApi));
        JS_SetPropertyStr(ctx, global, "Vendor", vendor); // consumes vendor

        JS_FreeValue(ctx, global);
    }

    void EmitTargetEvent(unsigned id, unsigned type) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "id", JS_NewInt64(ctx, id));
        JS_SetPropertyStr(ctx, p, "type", JS_NewInt32(ctx, static_cast<int32_t>(type)));
        ClientBindings::Emit("target", p);
    }

    void EmitJournalEvent(const char *text, unsigned type, unsigned serial,
                          unsigned hue, int ownerKind) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "text", JS_NewString(ctx, text ? text : ""));
        JS_SetPropertyStr(ctx, p, "type", JS_NewInt32(ctx, static_cast<int32_t>(type)));
        JS_SetPropertyStr(ctx, p, "serial", JS_NewInt64(ctx, serial));
        JS_SetPropertyStr(ctx, p, "hue", JS_NewInt32(ctx, static_cast<int32_t>(hue)));
        JS_SetPropertyStr(ctx, p, "system", JS_NewBool(ctx, ownerKind == 0));
        ClientBindings::Emit("journal", p);
    }

    void EmitContainerOpen(unsigned serial, unsigned gump) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "serial", JS_NewInt64(ctx, serial));
        JS_SetPropertyStr(ctx, p, "gump", JS_NewInt32(ctx, static_cast<int32_t>(gump)));
        ClientBindings::Emit("container_open", p);
    }

    void EmitContainerClose(unsigned serial) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "serial", JS_NewInt64(ctx, serial));
        ClientBindings::Emit("container_close", p);
    }

    void EmitMobileEvent(unsigned serial) {
        if (!ClientBindings::context)
            return;
        // Payload is just the serial; the script tracks it live via Mobiles.get.
        ClientBindings::Emit("mobile", JS_NewInt64(ClientBindings::context, serial));
    }

    void EmitMobileLeave(unsigned serial) {
        if (!ClientBindings::context)
            return;
        // The mobile's record is already gone (0x1D delete or out-of-range cull);
        // Mobiles.get(serial) now reads exists=false. Serial-only payload lets a
        // tracker drop it promptly instead of waiting to notice it vanished.
        ClientBindings::Emit("mobile_leave", JS_NewInt64(ClientBindings::context, serial));
    }

    void EmitAttackedEvent(unsigned serial) {
        if (!ClientBindings::context)
            return;
        // Attacker serial only; the script follows it live via Mobiles.get.
        ClientBindings::Emit("attacked", JS_NewInt64(ClientBindings::context, serial));
    }

    void EmitCombatEvent(unsigned serial) {
        if (!ClientBindings::context)
            return;
        // The foe we are swinging at (0x2F with us as attacker). Same live-serial
        // contract as 'attacked'; lets the bot react to a fight however it started.
        ClientBindings::Emit("combat", JS_NewInt64(ClientBindings::context, serial));
    }

    void EmitResurrectMenu(unsigned action) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "action", JS_NewInt32(ctx, static_cast<int32_t>(action)));
        ClientBindings::Emit("resurrect_menu", p);
    }

    void EmitVendorOffer(unsigned vendorSerial) {
        if (!ClientBindings::context)
            return;
        // Build the payload now (pendingVendor_ is fresh and about to be cleared).
        ClientBindings::Emit("vendor_buy",
                             ClientBindings::VendorOfferToJS(ClientBindings::context, vendorSerial));
    }

    void EmitVendorDone(unsigned vendorSerial, int flag) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "vendor", JS_NewInt64(ctx, vendorSerial));
        JS_SetPropertyStr(ctx, p, "flag", JS_NewInt32(ctx, flag));
        ClientBindings::Emit("vendor_done", p);
    }

    void EmitPaperdoll(unsigned serial, const char *title) {
        if (!ClientBindings::context)
            return;
        JSContext *ctx = ClientBindings::context;
        JSValue p = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, p, "serial", JS_NewInt64(ctx, serial));
        JS_SetPropertyStr(ctx, p, "title", JS_NewString(ctx, title ? title : ""));
        ClientBindings::Emit("paperdoll", p);
    }

    void EmitDialogEvent() {
        if (!ClientBindings::context)
            return;
        // Build the payload now (activeDialog_ is fresh); dispatched next tick.
        ClientBindings::Emit("dialog", ClientBindings::DialogToJS(ClientBindings::context));
    }

    void TickClientEvents(long long nowMs) { ClientBindings::TickEvents(nowMs); }

    void DetachClientBindings() { ClientBindings::Detach(); }
} // namespace uo::js
