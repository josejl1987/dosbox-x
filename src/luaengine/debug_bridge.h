#ifndef DEBUG_BRIDGE_H
#define DEBUG_BRIDGE_H

//
// debug_bridge.h — Centralized declarations for debug subsystem symbols
// that are internal to debug.cpp and not exposed in any header.
//
// This bridge replaces scattered extern declarations and #if 0 blocks
// with a clean API surface. When CBreakpoint is eventually extracted to
// a proper header, these stubs can be replaced with direct calls.
//

#include <cstdint>

#if C_LUA

// Forward declarations of debug subsystem functions (defined in debug.cpp)
extern bool ParseCommand(char* str);
extern uint64_t GetAddress(uint16_t seg, uint32_t offset);

// Global LuaEngine instance (defined in LuaEngine.cpp)
class LuaEngine;
extern LuaEngine luaEngine;

// ponytail: BPINT_ALL is #define'd inside debug.cpp as 0x100.
// Replicated here so luaengine code can use it without including debug.cpp internals.
#define DEBUG_BRIDGE_BPINT_ALL 0x100

// Opaque handle replacing CBreakpoint* in luaengine code.
// Real CBreakpoint* can be cast to/from this safely.
using DebugBreakpointHandle = void*;
static constexpr DebugBreakpointHandle DEBUG_BRIDGE_INVALID = nullptr;

//
// No-op stubs for CBreakpoint static methods.
// These mirror the real CBreakpoint API (debug.cpp lines 575-850).
// Currently return null/false/void — no breakpoints are created.
// To activate: implement in debug_bridge.cpp with real CBreakpoint calls.
//
namespace DebugBridge {

// Breakpoint creation — return INVALID handle (no breakpoint created)
DebugBreakpointHandle AddBreakpoint(uint16_t seg, uint32_t off, bool once);
DebugBreakpointHandle AddIntBreakpoint(uint8_t intNum, uint16_t ah, uint16_t al, bool once);
DebugBreakpointHandle AddMemBreakpoint(uint16_t seg, uint32_t off);

// Breakpoint lookup
DebugBreakpointHandle FindPhysBreakpoint(uint16_t seg, uint32_t off, bool once);
bool CheckBreakpoint(uint16_t seg, uint32_t off);
bool HasBreakpoints();

// Breakpoint management
bool DeleteBreakpoint(uint16_t seg, uint32_t off);
void ActivateBreakpoints();
void ActivateBreakpointsExceptAt(uint32_t physAddr);
void DeleteAll();

// Instance methods (operate on handles from Add*/FindPhys*)
void Activate(DebugBreakpointHandle bp, bool active);
bool IsActive(DebugBreakpointHandle bp);

// DEBUG_RunLua — does not exist in codebase, stub for compatibility
void RunLua(int count, bool skipBreakpoints);

} // namespace DebugBridge

#endif // C_LUA

#endif // DEBUG_BRIDGE_H
