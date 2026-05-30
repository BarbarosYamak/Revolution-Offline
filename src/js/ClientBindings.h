#pragma once

// UO-specific JS bindings. Installs the live game-state globals (Player, ...)
// onto a fresh JSContext. Kept separate from JsEngine so the engine stays a
// generic QuickJS host. The implementation (ClientBindings.cpp) is the only
// place outside JsEngine.cpp that includes quickjs.h.

struct JSContext;  // quickjs.h forward decl

namespace uo {
class Client;
namespace js {

// Register app globals on ctx, bound to `client`. Called by JsEngine on every
// fresh runtime (see Client ctor -> SetBindingInstaller).
void InstallClientBindings(JSContext* ctx, Client* client);

// Server-driven events the Client emits into the JS event system (Player.on /
// once). Quickjs-free signatures so Client.cpp needn't include quickjs.h.
void EmitTargetEvent(unsigned id, unsigned type);            // 0x6C cursor armed
void EmitJournalEvent(const char* text, unsigned type, unsigned serial,
                      unsigned hue, int ownerKind);          // new journal line
void EmitContainerOpen(unsigned serial, unsigned gump);      // 0x24 container opened
void EmitContainerClose(unsigned serial);                    // container closed / culled out of range
void EmitMobileEvent(unsigned serial);                       // 0x78 mobile appeared (serial only)
void EmitMobileLeave(unsigned serial);                       // mobile removed (0x1D / out-of-range cull)
void EmitAttackedEvent(unsigned serial);                     // 0x2F swing at us (attacker serial)
void EmitCombatEvent(unsigned serial);                       // 0x2F we swing at a foe (defender serial)
void EmitResurrectMenu(unsigned action);                     // 0x2C resurrect menu prompt ({action})
void EmitVendorOffer(unsigned vendorSerial);                 // 0x24 gump 0x30: buy window ready (built from pendingVendor_)
void EmitVendorDone(unsigned vendorSerial, int flag);        // 0x3B vendor transaction closed
void EmitPaperdoll(unsigned serial, const char* title);      // 0x88 paperdoll title learned
void EmitDialogEvent();                                      // 0x7C open dialog/menu (built from activeDialog_)
void TickClientEvents(long long nowMs);                      // dispatch events + sweep timeouts
void DetachClientBindings();                                 // free refs on engine teardown

}  // namespace js
}  // namespace uo
