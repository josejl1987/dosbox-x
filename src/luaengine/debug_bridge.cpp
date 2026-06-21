// debug_bridge.cpp — No-op stub implementations for CBreakpoint API
// ponytail: All functions are stubs. To activate, move these implementations
// into debug.cpp (where CBreakpoint is visible) and call the real methods.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if C_LUA

#include "debug_bridge.h"

namespace DebugBridge {

DebugBreakpointHandle AddBreakpoint(uint16_t, uint32_t, bool) { return DEBUG_BRIDGE_INVALID; }
DebugBreakpointHandle AddIntBreakpoint(uint8_t, uint16_t, uint16_t, bool) { return DEBUG_BRIDGE_INVALID; }
DebugBreakpointHandle AddMemBreakpoint(uint16_t, uint32_t) { return DEBUG_BRIDGE_INVALID; }

DebugBreakpointHandle FindPhysBreakpoint(uint16_t, uint32_t, bool) { return DEBUG_BRIDGE_INVALID; }
bool CheckBreakpoint(uint16_t, uint32_t) { return false; }
bool HasBreakpoints() { return false; }

bool DeleteBreakpoint(uint16_t, uint32_t) { return false; }
void ActivateBreakpoints() {}
void ActivateBreakpointsExceptAt(uint32_t) {}
void DeleteAll() {}

void Activate(DebugBreakpointHandle, bool) {}
bool IsActive(DebugBreakpointHandle) { return false; }

void RunLua(int, bool) {}

} // namespace DebugBridge

#endif // C_LUA
