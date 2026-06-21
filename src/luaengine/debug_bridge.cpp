// debug_bridge.cpp — Delegates to CBreakpoint (C_DEBUG) or InstrumentationRouter

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if C_LUA

#include "debug_bridge.h"
#include "instrumentation_router.h"

#if C_DEBUG
// Forward-declare CBreakpoint class (defined in debug.cpp)
// Only the static method signatures we need — linker resolves to debug.cpp
// Instance methods (IsActive, Activate) are inline in debug.cpp and not exported
class CBreakpoint {
public:
    static CBreakpoint* AddBreakpoint(uint16_t seg, uint32_t off, bool once);
    static CBreakpoint* AddIntBreakpoint(uint8_t intNum, uint16_t ah, uint16_t al, bool once);
    static CBreakpoint* AddMemBreakpoint(uint16_t seg, uint32_t off);
    static void ActivateBreakpoints();
    static void ActivateBreakpointsExceptAt(uint32_t adr);
    static bool CheckBreakpoint(uint16_t seg, uint32_t off);
    static CBreakpoint* FindPhysBreakpoint(uint16_t seg, uint32_t off, bool once);
    static bool DeleteBreakpoint(uint16_t seg, uint32_t off);
    static void DeleteAll();
};
#endif // C_DEBUG

namespace DebugBridge {

DebugBreakpointHandle AddBreakpoint(uint16_t seg, uint32_t off, bool once) {
#if C_DEBUG
    return static_cast<DebugBreakpointHandle>(CBreakpoint::AddBreakpoint(seg, off, once));
#else
    if (g_instrumentation) {
        auto handle = g_instrumentation->addBreakpoint((uint32_t(seg) << 4) + off);
        return reinterpret_cast<DebugBreakpointHandle>(static_cast<uintptr_t>(handle.id()));
    }
    return {};  // ponytail: null handle — no router available
#endif
}

DebugBreakpointHandle AddIntBreakpoint(uint8_t intNum, uint16_t ah, uint16_t al, bool once) {
#if C_DEBUG
    return static_cast<DebugBreakpointHandle>(CBreakpoint::AddIntBreakpoint(intNum, ah, al, once));
#else
    // ponytail: int breakpoints not supported via router
    (void)intNum; (void)ah; (void)al; (void)once;
    return {};
#endif
}

DebugBreakpointHandle AddMemBreakpoint(uint16_t seg, uint32_t off) {
#if C_DEBUG
    return static_cast<DebugBreakpointHandle>(CBreakpoint::AddMemBreakpoint(seg, off));
#else
    if (g_instrumentation) {
        auto handle = g_instrumentation->addBreakpoint((uint32_t(seg) << 4) + off);
        return reinterpret_cast<DebugBreakpointHandle>(static_cast<uintptr_t>(handle.id()));
    }
    return {};
#endif
}

DebugBreakpointHandle FindPhysBreakpoint(uint16_t seg, uint32_t off, bool once) {
#if C_DEBUG
    return static_cast<DebugBreakpointHandle>(CBreakpoint::FindPhysBreakpoint(seg, off, once));
#else
    (void)seg; (void)off; (void)once;
    return {};
#endif
}

bool CheckBreakpoint(uint16_t seg, uint32_t off) {
#if C_DEBUG
    return CBreakpoint::CheckBreakpoint(seg, off);
#else
    return g_instrumentation && g_instrumentation->checkBreakpointLinear((uint32_t(seg) << 4) + off);
#endif
}

bool HasBreakpoints() {
#if C_DEBUG
    // Let CBreakpoint::CheckBreakpoint do its own empty() check
    return true;
#else
    return g_instrumentation && g_instrumentation->hasBreakpoints();
#endif
}

bool DeleteBreakpoint(uint16_t seg, uint32_t off) {
#if C_DEBUG
    return CBreakpoint::DeleteBreakpoint(seg, off);
#else
    return g_instrumentation && g_instrumentation->removeBreakpointByAddr((uint32_t(seg) << 4) + off);
#endif
}

void ActivateBreakpoints() {
#if C_DEBUG
    CBreakpoint::ActivateBreakpoints();
#endif
    // ponytail: router breakpoints are always active
}

void ActivateBreakpointsExceptAt(uint32_t physAddr) {
#if C_DEBUG
    CBreakpoint::ActivateBreakpointsExceptAt(physAddr);
#endif
}

void DeleteAll() {
#if C_DEBUG
    CBreakpoint::DeleteAll();
#else
    if (g_instrumentation) {
        g_instrumentation->clearAllBreakpoints();
    }
#endif
}

void Activate(DebugBreakpointHandle bp, bool active) {
#if C_DEBUG
    // ponytail: CBreakpoint::Activate is inline in debug.cpp — not exported.
    // Activation is handled by static methods (ActivateBreakpoints etc.)
    (void)bp; (void)active;
#endif
}

bool IsActive(DebugBreakpointHandle bp) {
#if C_DEBUG
    // ponytail: CBreakpoint::IsActive is inline in debug.cpp — not exported.
    // Non-null handle implies the breakpoint exists.
    return bp != nullptr;
#else
    (void)bp;
    return {};
#endif
}

void RunLua(int count, bool skipBreakpoints) {
    // DEBUG_RunLua does not exist in codebase — kept for compatibility
    (void)count; (void)skipBreakpoints;
}

} // namespace DebugBridge

#endif // C_LUA
