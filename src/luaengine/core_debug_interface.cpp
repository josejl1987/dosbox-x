#include "core_debug_interface.h"
#include "event_manager.h"
#include "trace_logger.h"  // For FastRegisterSnapshot
#include "dosbox.h"
#include "cpu.h"
#include "mem.h"
#include "regs.h"
#include "debug.h"
#include "menu.h"
#include "../debug/debug_inc.h"
#include "luaengine.h"  // For enabling instruction hooks when breakpoints are added
#include <mutex>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>

// Forward declarations for DOSBox functions
extern void On_Software_CPU_Reset();
extern Bitu DEBUG_EnableDebugger(void);
extern void DOSBOX_SetNormalLoop(void);
extern LoopHandler* DOSBOX_GetLoop(void);
extern void DOSBOX_SetLoop(LoopHandler* handler);
extern Bitu DEBUG_Loop(void);

// Forward declarations for debugger state variables
// Note: debugging and debug_running are now declared in debug.h

// PHASE 4 LOCK-FREE HOT PATH: Define thread-local static variables
// Each thread maintains its own cached copy of breakpoints to eliminate locking
thread_local uint32_t LuaEngineDebug::DosBoxCoreDebugger::t_local_version_ = 0;
thread_local std::unordered_map<uint32_t, LuaEngineDebug::Breakpoint> LuaEngineDebug::DosBoxCoreDebugger::t_local_breakpoints_;

// Forward declaration for debug data structure from debug.cpp
struct SCodeViewData {
    int     cursorPos;
    uint16_t  firstInstSize;
    uint16_t  useCS;
    uint32_t  useEIPlast, useEIPmid;
    uint32_t  useEIP;
    uint16_t  cursorSeg;
    uint32_t  cursorOfs;
    bool    ovrMode;
    char    inputStr[256];
    char    suspInputStr[256];
    int     inputPos;
};
extern SCodeViewData codeViewData;

// Keep it simple - don't interfere with original debugger internals


namespace LuaEngineDebug {

    // Global instance
    DosBoxCoreDebugger* g_core_debugger = nullptr;

    //=============================================================================
    // DosBoxCoreDebugger Implementation
    //=============================================================================

    DosBoxCoreDebugger::DosBoxCoreDebugger()
        : event_manager_(nullptr) {
        active_breakpoint_count_.store(0);
        // Initialize current address to actual CPU state
        syncWithExistingDebugger();
    }

    DosBoxCoreDebugger::~DosBoxCoreDebugger() {
    }

    void DosBoxCoreDebugger::initialize(EventManager* event_mgr) {
        event_manager_ = event_mgr;

        // Don't automatically activate debugger on init to avoid console popup
        // We'll activate it only when needed
    }

    void DosBoxCoreDebugger::onInstructionExecuted(uint32_t address) {
        // Update current address from the actual executed address
        current_address_ = address;
        follow_address_ = address;

        // Clear skip tracking when we move to a different address
        // This handles the case where checkBreakpoints() is called multiple times per instruction
        if(has_ignore_breakpoint_once_ && address != ignore_breakpoint_once_) {
            // Address changed from %08X to %08X, clearing skip_once flags
            (void)ignore_breakpoint_once_; (void)address;
            has_ignore_breakpoint_once_ = false;
            skip_was_used_this_instruction_ = false;
            ignore_breakpoint_once_ = 0;
        }

        // Synchronize with existing debugger state
        syncWithExistingDebugger();

        // Track calls and returns for trace-based call stack
        if(trace_call_stack_enabled_) {
            uint8_t opcode = mem_readb(address);

            // Detect CALL instructions
            if(opcode == 0xE8) {
                // Near relative call (length depends on code size)
                uint32_t call_length = cpu.code.big ? 5u : 3u;
                int32_t offset = cpu.code.big
                    ? static_cast<int32_t>(mem_readd(address + 1))
                    : static_cast<int32_t>(static_cast<int16_t>(mem_readw(address + 1)));
                uint32_t target = address + call_length + offset;
                onCallInstruction(address, target, false);
            }
            else if(opcode == 0x9A) {
                // Far direct call (CALL ptr16:16 or ptr16:32)
                uint32_t offset = cpu.code.big ? mem_readd(address + 1) : mem_readw(address + 1);
                uint16_t segment = mem_readw(address + (cpu.code.big ? 5 : 3));
                onCallInstruction(address, (segment << 16) | offset, true);
            }
            else if(opcode == 0xFF) {
                // CALL r/m (need to decode ModR/M)
                uint8_t modrm = mem_readb(address + 1);
                uint8_t reg = (modrm >> 3) & 0x07;
                if(reg == 2) {
                    // CALL r/m16/32 (near indirect)
                    // Target is in register or memory - capture return on RET
                }
                else if(reg == 3) {
                    // CALL m16:16/32 (far indirect)
                    // Target will be resolved at runtime
                }
            }

            // Detect RET instructions
            else if(opcode == 0xC3 || opcode == 0xC2) {
                // Near return (C2 = RET imm16, C3 = RET)
                onReturnInstruction(reg_eip);
            }
            else if(opcode == 0xCB || opcode == 0xCA) {
                // Far return (CA = RETF imm16, CB = RETF)
                onReturnInstruction(reg_eip);
            }
        }

        // Check if we need to pause for step commands
        processStepCommands();

        // Check breakpoints (this will handle skip_once internally)
        checkBreakpoints(address);

        // Don't block here - let the main loop handle the pause state
    }

    //=============================================================================
    // CPU State Management
    //=============================================================================

    std::map<std::string, uint32_t> DosBoxCoreDebugger::getCpuRegisters() {
        std::map<std::string, uint32_t> registers;

        // General purpose registers
        registers["EAX"] = reg_eax;
        registers["EBX"] = reg_ebx;
        registers["ECX"] = reg_ecx;
        registers["EDX"] = reg_edx;
        registers["ESI"] = reg_esi;
        registers["EDI"] = reg_edi;
        registers["ESP"] = reg_esp;
        registers["EBP"] = reg_ebp;

        // Segment registers
        registers["CS"] = SegValue(cs);
        registers["DS"] = SegValue(ds);
        registers["ES"] = SegValue(es);
        registers["FS"] = SegValue(fs);
        registers["GS"] = SegValue(gs);
        registers["SS"] = SegValue(ss);

        // Control registers
        registers["EIP"] = reg_eip;  // Match original debugger
        registers["EFLAGS"] = reg_flags;

        return registers;
    }

    void DosBoxCoreDebugger::getCpuRegistersFast(LuaEngineTraceLogger::FastRegisterSnapshot& snapshot) {
        // OPTIMIZATION: Direct struct population - zero allocations, cache-friendly
        snapshot.eax = reg_eax;
        snapshot.ebx = reg_ebx;
        snapshot.ecx = reg_ecx;
        snapshot.edx = reg_edx;
        snapshot.esi = reg_esi;
        snapshot.edi = reg_edi;
        snapshot.esp = reg_esp;
        snapshot.ebp = reg_ebp;

        snapshot.cs = SegValue(cs);
        snapshot.ds = SegValue(ds);
        snapshot.es = SegValue(es);
        snapshot.fs = SegValue(fs);
        snapshot.gs = SegValue(gs);
        snapshot.ss = SegValue(ss);

        snapshot.eip = reg_eip;
        snapshot.eflags = reg_flags;
        snapshot.valid_mask = 0xFFFFFFFF;  // All registers valid
    }

    CPUState DosBoxCoreDebugger::getCpuState() {
        CPUState state;

        // Read actual CPU registers from DOSBox
        state.eax = reg_eax;
        state.ebx = reg_ebx;
        state.ecx = reg_ecx;
        state.edx = reg_edx;
        state.esi = reg_esi;
        state.edi = reg_edi;
        state.esp = reg_esp;
        state.ebp = reg_ebp;

        // Segment registers
        state.cs = SegValue(cs);
        state.ds = SegValue(ds);
        state.es = SegValue(es);
        state.fs = SegValue(fs);
        state.gs = SegValue(gs);
        state.ss = SegValue(ss);

        // Instruction pointer and flags (match original debugger)
        state.ip = reg_eip;  // Use reg_eip like original debugger
        state.flags = reg_flags;

        return state;
    }

    void DosBoxCoreDebugger::setCpuRegister(const std::string& name, uint32_t value) {
        if(name == "EAX") reg_eax = value;
        else if(name == "EBX") reg_ebx = value;
        else if(name == "ECX") reg_ecx = value;
        else if(name == "EDX") reg_edx = value;
        else if(name == "ESI") reg_esi = value;
        else if(name == "EDI") reg_edi = value;
        else if(name == "ESP") reg_esp = value;
        else if(name == "EBP") reg_ebp = value;
        else if(name == "EIP") reg_eip = value;  // Match original debugger
        else if(name == "EFLAGS") reg_flags = value;
        // Segment registers are more complex to set and might need special handling
    }

    uint32_t DosBoxCoreDebugger::getCurrentEIP() {
        // Always use the current register value for reliability
        return reg_eip;
    }

    uint32_t DosBoxCoreDebugger::getCurrentAddress() const {
        // Always return the most up-to-date address
        // If current_address_ is 0, calculate it from CPU state
        if(current_address_ == 0) {
            return (SegValue(cs) << 4) + reg_eip;
        }
        return current_address_;
    }

    uint16_t DosBoxCoreDebugger::getCurrentCS() {
        return SegValue(cs);
    }

    //=============================================================================
    // Execution Control
    //=============================================================================

    void DosBoxCoreDebugger::pause(BreakReason reason) {
        uint32_t pause_addr = (SegValue(cs) << 4) + reg_eip;
        // Pause requested at address %08X (reason: %d)
        (void)pause_addr; (void)reason;

        is_paused_.store(true);
        last_break_reason_ = reason;
        last_paused_address_ = pause_addr;

        // NOTE: We do NOT clear has_ignore_breakpoint_once_ here anymore!
        // The skip_once flag needs to persist across the pause/resume boundary
        // so that resume() can set it and checkBreakpoints() can use it.
        // The flag is cleared in checkBreakpoints() when it's actually used.

        // Set debugging state like the original debugger
        debugging = true;
        debug_running = false;

#if C_DEBUG
        // Use existing debugger infrastructure for proper pause functionality
        extern bool tohide;
        extern void DEBUG_DrawScreen(void);

        // Force console to be visible temporarily for debugging
        bool old_tohide = tohide;
        tohide = false;

        // Set the debug loop to enter pause mode properly
        DOSBOX_SetLoop(&DEBUG_Loop);

        // Draw the debug screen to show current state
        DEBUG_DrawScreen();

        tohide = old_tohide;
#endif

        if(pause_callback_) {
            pause_callback_();
        }
        notifyPaused(reason, last_paused_address_);
    }

    void DosBoxCoreDebugger::resume() {
        // Resume requested from address %08X (reason: %d)
        (void)last_paused_address_; (void)last_break_reason_;

        is_paused_.store(false);
        step_into_requested_.store(false);
        step_over_requested_.store(false);

        // If we are resuming from a breakpoint, set skip_once_ flag
        // This ensures exactly one instruction at the current PC runs freely
        if(last_break_reason_ == BreakReason::Breakpoint) {
            ignore_breakpoint_once_ = last_paused_address_;
            has_ignore_breakpoint_once_ = true;
            skip_was_used_this_instruction_ = false;  // Reset for new skip operation
            // Setting skip_once for address %08X
            (void)ignore_breakpoint_once_;
        }
        else {
            has_ignore_breakpoint_once_ = false;
            skip_was_used_this_instruction_ = false;
        }

        // Set state to exit debug mode like the original debugger
        debugging = false;
        debug_running = false;

#if C_DEBUG
        // Set exitLoop to break out of debug loop (like DOSBox does)
        extern bool exitLoop;
        exitLoop = true;

        // Ensure all breakpoints are active for normal execution
        // NOTE: We do NOT call ActivateBreakpointsExceptAt here anymore
        // The skip_once_ flag in checkBreakpoints() handles that logic
        CBreakpoint::ActivateBreakpoints();

        // Exit debug loop and return to normal execution
        DOSBOX_SetNormalLoop();

        // Update menu items to reflect resumed state
        extern DOSBoxMenu mainMenu;
        mainMenu.get_item("debugger_rundebug").check(false).refresh_item(mainMenu);
        mainMenu.get_item("debugger_runnormal").check(true).refresh_item(mainMenu);
        mainMenu.get_item("debugger_runwatch").check(false).refresh_item(mainMenu);
#endif

        if(resume_callback_) {
            resume_callback_();
        }
        notifyResumed();
    }

    void DosBoxCoreDebugger::stepInto() {
        step_into_requested_.store(true);

        // Ensure we're in debug mode
        debugging = true;
        debug_running = false;

#if C_DEBUG
        // Reactivate breakpoints before stepping (they may have been deactivated)
        CBreakpoint::ActivateBreakpoints();

        // Execute one instruction using the existing debugger
        DEBUG_RunLua(1, false);  // Run 1 instruction without breakpoint handling

        // Force back into debug mode after the step
        pause(BreakReason::Step);
#endif
    }

    void DosBoxCoreDebugger::stepOver() {
        step_over_requested_.store(true);

        // Ensure we're in debug mode
        debugging = true;
        debug_running = false;

#if C_DEBUG
        // Reactivate breakpoints before stepping
        CBreakpoint::ActivateBreakpoints();

        // Check if current instruction is a call
        uint32_t current_address = (SegValue(cs) << 4) + reg_eip;

        if(isCallInstruction(current_address)) {
            // It's a call - add breakpoint after the call instruction
            uint32_t next_address = getNextInstructionAddress(current_address);

            // Convert to segment:offset for DOSBox's breakpoint system
            uint16_t seg = SegValue(cs);
            uint32_t off = (next_address - (seg << 4)) & 0xFFFF;

            // Add temporary breakpoint after the call
            CBreakpoint::AddBreakpoint(seg, off, true);

            // Resume execution - it will hit the breakpoint after the call returns
            resume();
        }
        else {
            // Not a call, just do a regular step into
            stepInto();
        }
#endif
    }

    void DosBoxCoreDebugger::syncWithExistingDebugger() {
        // PERFORMANCE OPTIMIZATION: Early exit if not paused to avoid unnecessary work
        // This function is called on every instruction, so fast-path is critical
        bool should_be_paused = debugging && !debug_running;
        bool currently_paused = is_paused_.load(std::memory_order_relaxed);

        // Fast-path: if state hasn't changed and we're not paused, skip expensive operations
        if(currently_paused == should_be_paused && !should_be_paused) {
            // Just update address and return - skip expensive synchronization
            current_address_ = (SegValue(cs) << 4) + reg_eip;
            follow_address_ = current_address_;
            return;
        }

        // Update our paused state based on the existing debugger
        is_paused_.store(should_be_paused);

        // Always update current address based on actual CPU state
        current_address_ = (SegValue(cs) << 4) + reg_eip;
        follow_address_ = current_address_;

#if C_DEBUG
        // Additional synchronization with the existing DOSBox debugger if available
        // Try to use codeViewData if it's valid and matches current state
        if(SegValue(cs) == codeViewData.useCS &&
            reg_eip >= codeViewData.useEIP &&
            reg_eip <= codeViewData.useEIPlast) {
            // Use the debugger's tracked address for consistency
            current_address_ = (codeViewData.useCS << 4) + codeViewData.useEIP;
            follow_address_ = current_address_;
        }
#endif
    }

    void DosBoxCoreDebugger::processStepCommands() {
        if(step_into_requested_.load()) {
            step_into_requested_.store(false);
            notifyStepComplete((SegValue(cs) << 4) + reg_eip);
            pause(BreakReason::Step);
        }
        else if(step_over_requested_.load()) {
            uint32_t current_address = (SegValue(cs) << 4) + reg_eip;
            if(current_address == step_over_target_.load()) {
                step_over_requested_.store(false);
                notifyStepComplete(current_address);
                pause(BreakReason::Step);
            }
        }
    }

    //=============================================================================
    // Breakpoint Management
    //=============================================================================

    void DosBoxCoreDebugger::addBreakpoint(uint32_t address) {
#if C_DEBUG
        // For DOSBox breakpoints, we need to convert the linear address back to segment:offset
        // Try to find the appropriate segment that contains this linear address

        uint16_t seg = static_cast<uint16_t>(address >> 4); // Default fallback
        uint16_t off = static_cast<uint16_t>(address & 0xFFFF);

        // For now, use simple linear to segment:offset conversion
        // This is the same approach used in symbolic_breakpoints.cpp
        seg = static_cast<uint16_t>(address >> 4);
        off = static_cast<uint16_t>(address & 0xFFFF);

        // Validate offset is within 16-bit range
        if(off > 0xFFF0) {
            // Address might be invalid, but still try with fallback calculation
            seg = static_cast<uint16_t>(address >> 4);
            off = static_cast<uint16_t>(address & 0xFFFF);
        }

        // Adding breakpoint at %08X (seg:off %04X:%04X)
        (void)address; (void)seg; (void)off;

        // Use DOSBox's breakpoint system (false = permanent breakpoint)
        CBreakpoint::AddBreakpoint(seg, off, false);
        // Ensure breakpoints are activated
        CBreakpoint::ActivateBreakpoints();
#endif

        // OPTIMIZATION: Simple breakpoints are handled by CBreakpoint only
        // Skip adding to our hot-path system to avoid double-checking
        bool added = false;
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            for(const auto& bp : breakpoints_) {
                if(bp.address == address) {
                    return; // Already exists
                }
            }

            // Create simple breakpoint for GUI tracking, but exclude from hot path
            Breakpoint new_bp(address);
            new_bp.type = Breakpoint::Simple;
            breakpoints_.push_back(new_bp);
            breakpoint_index_[address] = breakpoints_.size() - 1; // Add to hash map
            // Note: Don't increment active_breakpoint_count_ for simple BPs (not in hot path)
            added = true;

            // No need to rebuild page bitmap or increment version for simple breakpoints
            // since they're excluded from the hot path
        }

        if(added) {
            // Ensure the Lua engine's instruction hooks are active so breakpoints can actually fire
            luaEngine.enableInstructionHooks(true);
            notifyBreakpointListChanged();
        }
    }

    void DosBoxCoreDebugger::addBreakpoint(const Breakpoint& bp) {
        bool added_or_updated = false;

        // OPTIMIZATION: Only conditional breakpoints go into our internal system
        // Simple breakpoints are handled by CBreakpoint only (avoids double-checking)
        if(bp.isConditional()) {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);

            // Check if breakpoint already exists
            size_t existing_index = 0;
            for(auto& existing : breakpoints_) {
                if(existing.address == bp.address) {
                    existing = bp;  // Update existing breakpoint
                    breakpoint_index_[bp.address] = existing_index; // Update hash map
                    added_or_updated = true;
                    break;
                }
                existing_index++;
            }

            if(!added_or_updated) {
                breakpoints_.push_back(bp);
                breakpoint_index_[bp.address] = breakpoints_.size() - 1; // Add to hash map
                active_breakpoint_count_.fetch_add(1, std::memory_order_relaxed);
                added_or_updated = true;
            }

            // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
            if(added_or_updated) {
                rebuildBreakpointPagesLocked();  // OPTIMIZATION: Rebuild page bitmap
                breakpoint_version_.fetch_add(1, std::memory_order_release);
            }
        }

        if(added_or_updated) {
            // Make sure instruction hooks are enabled whenever a breakpoint is present
            luaEngine.enableInstructionHooks(true);
            notifyBreakpointListChanged();
        }
    }

    void DosBoxCoreDebugger::removeBreakpoint(uint32_t address) {
#if C_DEBUG
        // For DOSBox breakpoints, we need to convert the linear address back to segment:offset
        // Use the same logic as addBreakpoint to ensure we're removing the right one

        uint16_t seg = static_cast<uint16_t>(address >> 4); // Default fallback
        uint16_t off = static_cast<uint16_t>(address & 0xFFFF);

        // For now, use simple linear to segment:offset conversion
        // This is the same approach used in symbolic_breakpoints.cpp
        seg = static_cast<uint16_t>(address >> 4);
        off = static_cast<uint16_t>(address & 0xFFFF);

        // Use DOSBox's breakpoint system to remove
        CBreakpoint::DeleteBreakpoint(seg, off);
#endif

        // Also remove from our internal list
        bool removed = false;
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            auto end_it = std::remove_if(breakpoints_.begin(), breakpoints_.end(),
                [address](const Breakpoint& bp) { return bp.address == address; });
            if(end_it != breakpoints_.end()) {
                breakpoints_.erase(end_it, breakpoints_.end());

                // Rebuild hash map index after vector changes
                breakpoint_index_.clear();
                for(size_t i = 0; i < breakpoints_.size(); ++i) {
                    breakpoint_index_[breakpoints_[i].address] = i;
                }

                active_breakpoint_count_.store(breakpoints_.size(), std::memory_order_relaxed);
                removed = true;

                // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
                rebuildBreakpointPagesLocked();  // OPTIMIZATION: Rebuild page bitmap
                breakpoint_version_.fetch_add(1, std::memory_order_release);
            }
        }
        if(removed) {
            notifyBreakpointListChanged();
        }
    }

    void DosBoxCoreDebugger::toggleBreakpoint(uint32_t address) {
        bool removed = false;
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);

            // Check if breakpoint already exists
            for(auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
                if(it->address == address) {
                    // Breakpoint exists - remove it
                    breakpoints_.erase(it);

                    // Rebuild hash map index after vector changes
                    breakpoint_index_.clear();
                    for(size_t i = 0; i < breakpoints_.size(); ++i) {
                        breakpoint_index_[breakpoints_[i].address] = i;
                    }

                    active_breakpoint_count_.store(breakpoints_.size(), std::memory_order_relaxed);
                    removed = true;

                    // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
                    rebuildBreakpointPagesLocked();  // OPTIMIZATION: Rebuild page bitmap
                    breakpoint_version_.fetch_add(1, std::memory_order_release);
                    break;
                }
            }
        }

        if(removed) {
#if C_DEBUG
            // Remove from DOSBox's breakpoint system
            uint16_t seg = SegValue(cs);
            uint32_t cs_base = SegPhys(cs);  // Use SegPhys instead of simple shift
            uint16_t off = static_cast<uint16_t>(address - cs_base);
            CBreakpoint::DeleteBreakpoint(seg, off);
#endif
            notifyBreakpointListChanged();
            return;
        }

        // Breakpoint doesn't exist - add it
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            breakpoints_.emplace_back(address);
            active_breakpoint_count_.fetch_add(1, std::memory_order_relaxed);

            // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
            rebuildBreakpointPagesLocked();  // OPTIMIZATION: Rebuild page bitmap
            breakpoint_version_.fetch_add(1, std::memory_order_release);
        }

#if C_DEBUG
        // Add to DOSBox's breakpoint system
        uint16_t seg = SegValue(cs);
        uint32_t cs_base = SegPhys(cs);  // Use SegPhys instead of simple shift
        uint16_t off = static_cast<uint16_t>(address - cs_base);
        CBreakpoint::AddBreakpoint(seg, off, false);
        // Ensure breakpoints are activated
        CBreakpoint::ActivateBreakpoints();
#endif

        // Keep instruction hooks on so the breakpoint will be observed each instruction
        luaEngine.enableInstructionHooks(true);
        notifyBreakpointListChanged();
    }

    std::vector<Breakpoint> DosBoxCoreDebugger::getBreakpoints() const {
        std::shared_lock<std::shared_mutex> lock(breakpoints_mutex_);
        return breakpoints_;
    }

    bool DosBoxCoreDebugger::hasBreakpoint(uint32_t address) const {
        std::shared_lock<std::shared_mutex> lock(breakpoints_mutex_);

        for(const auto& bp : breakpoints_) {
            if(bp.address == address && bp.enabled) {
                return true;
            }
        }
        return false;
    }

    void DosBoxCoreDebugger::checkBreakpoints(uint32_t address) {
        // FAST PATH: If no breakpoints are active, return immediately without locking or allocation
        if(active_breakpoint_count_.load(std::memory_order_relaxed) == 0) return;

        // Check skip_once_ flag BEFORE checking breakpoints
        // Handle multiple calls per instruction by tracking if we've already used the skip
        if(has_ignore_breakpoint_once_ && address == ignore_breakpoint_once_) {
            if(!skip_was_used_this_instruction_) {
                // Skipping breakpoint at %08X once (resume from bp)
                (void)address;
                skip_was_used_this_instruction_ = true;  // Mark as used, but keep flag active
            }
            // Always return when skip flag is active for this address, even on subsequent calls
            return;
        }

        // ===== OPTIMIZATION 5: PAGE BITMAP FAST FILTER =====
        // Skip page entirely if it has no breakpoints - this eliminates most hash lookups
        uint32_t page = address >> PAGE_SHIFT;
        // Reading breakpoint_pages_ without a lock is safe because:
        // 1. We only read after version check (ensures visibility)
        // 2. The vector only grows, never shrinks below current page count
        // 3. A race that misses a newly set bit is benign (will be caught on next instruction)
        if(page >= breakpoint_pages_.size() || breakpoint_pages_[page] == 0) {
            return;  // This page has no breakpoints - skip expensive hash lookup
        }

        // ===== PHASE 4 LOCK-FREE HOT PATH =====
        // Double-checked locking with thread-local cache:
        // 1. Check version (relaxed atomic, ~zero cost)
        // 2. If stale → lock and refresh cache (RARE)
        // 3. Lookup in thread-local map (NO LOCKS!)

        // Step 1: Check if our thread-local cache is stale
        uint32_t current_version = breakpoint_version_.load(std::memory_order_acquire);

        if(t_local_version_ != current_version) {
            // CACHE MISS: Need to refresh thread-local copy (happens rarely - only when BPs change)
            std::shared_lock<std::shared_mutex> lock(breakpoints_mutex_);

            // OPTIMIZATION: Only copy conditional breakpoints to thread-local cache
            // Simple breakpoints are handled by CBreakpoint system, avoiding double-checking
            t_local_breakpoints_.clear();
            for(const auto& bp : breakpoints_) {
                if(bp.enabled && bp.isConditional()) {
                    t_local_breakpoints_[bp.address] = bp;
                }
            }

            // Update thread-local version to match current
            t_local_version_ = current_version;
        }

        // Step 2: Fast lookup in thread-local cache (NO LOCKS!)
        auto it = t_local_breakpoints_.find(address);
        if(it == t_local_breakpoints_.end()) {
            return;  // No breakpoint at this address
        }

        // Step 3: We found a breakpoint - copy it for evaluation
        Breakpoint hit_bp = it->second;

        // Update hit count (needs to write back to main list under lock)
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            auto index_it = breakpoint_index_.find(address);
            if(index_it != breakpoint_index_.end()) {
                breakpoints_[index_it->second].hit_count++;
            }
        }

        // Evaluate condition if present
        if(!hit_bp.condition.empty() && !evaluateBreakpointCondition(hit_bp.condition, address)) {
            return;
        }

        // Breakpoint hit at %08X (reason: Breakpoint)
        (void)address;

        // Fire breakpoint hit callback
        if(breakpoint_hit_callback_) {
            breakpoint_hit_callback_(address);
        }

        // Pause execution and notify listeners
        notifyBreakpointHit(address, BreakReason::Breakpoint);
        pause(BreakReason::Breakpoint);
    }

    //=============================================================================
    // Disassembly
    //=============================================================================

    DisassemblyLine DosBoxCoreDebugger::disassemble(uint32_t address) {
        DisassemblyLine result;
        result.address = address;

        // Use DOSBox-X's existing disassembly function
        char dasm_buffer[256];
        uint32_t length = DasmI386(dasm_buffer, address, address, cpu.code.big);

        result.full_text = dasm_buffer;
        result.length = length;

        // Extract raw bytes
        std::stringstream bytes_ss;
        for(uint32_t i = 0; i < length; ++i) {
            bytes_ss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(mem_readb(address + i));
            if(i < length - 1) bytes_ss << " ";
        }
        result.bytes = bytes_ss.str();

        // Parse the disassembly to separate mnemonic and operands
        parseDisassemblyText(result.full_text, result.mnemonic, result.operands);

        return result;
    }

    std::vector<DisassemblyLine> DosBoxCoreDebugger::disassembleRange(uint32_t start_address, int count) {
        std::vector<DisassemblyLine> results;
        results.reserve(count);

        uint32_t current_address = start_address;

        for(int i = 0; i < count; ++i) {
            DisassemblyLine line = disassemble(current_address);
            results.push_back(line);
            current_address += line.length;
        }

        return results;
    }

    //=============================================================================
    // Memory Access
    //=============================================================================

    uint8_t DosBoxCoreDebugger::readMemoryByte(uint32_t address) {
        return mem_readb(address);
    }

    uint16_t DosBoxCoreDebugger::readMemoryWord(uint32_t address) {
        return mem_readw(address);
    }

    uint32_t DosBoxCoreDebugger::readMemoryDWord(uint32_t address) {
        return mem_readd(address);
    }

    void DosBoxCoreDebugger::writeMemoryByte(uint32_t address, uint8_t value) {
        mem_writeb(address, value);
    }

    void DosBoxCoreDebugger::writeMemoryWord(uint32_t address, uint16_t value) {
        mem_writew(address, value);
    }

    void DosBoxCoreDebugger::writeMemoryDWord(uint32_t address, uint32_t value) {
        mem_writed(address, value);
    }

    //=============================================================================
    // Event Callbacks
    //=============================================================================

    void DosBoxCoreDebugger::setBreakpointHitCallback(std::function<void(uint32_t)> callback) {
        breakpoint_hit_callback_ = callback;
    }

    void DosBoxCoreDebugger::setPauseCallback(std::function<void()> callback) {
        pause_callback_ = callback;
    }

    void DosBoxCoreDebugger::setResumeCallback(std::function<void()> callback) {
        resume_callback_ = callback;
    }

    void DosBoxCoreDebugger::addListener(CoreDebuggerListener* listener) {
        if(!listener) return;
        std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
        listeners_.insert(listener);
    }

    void DosBoxCoreDebugger::removeListener(CoreDebuggerListener* listener) {
        std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
        auto it = listeners_.find(listener);
        if(it != listeners_.end()) {
            listeners_.erase(it);
        }
    }

    void DosBoxCoreDebugger::notifyPaused(BreakReason reason, uint32_t address) {
        std::vector<CoreDebuggerListener*> listeners_copy;
        {
            std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
            listeners_copy.assign(listeners_.begin(), listeners_.end());
        }
        for(auto* listener : listeners_copy) {
            listener->onPaused(reason, address);
        }
    }

    void DosBoxCoreDebugger::notifyResumed() {
        std::vector<CoreDebuggerListener*> listeners_copy;
        {
            std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
            listeners_copy.assign(listeners_.begin(), listeners_.end());
        }
        for(auto* listener : listeners_copy) {
            listener->onResumed();
        }
    }

    void DosBoxCoreDebugger::notifyStepComplete(uint32_t address) {
        std::vector<CoreDebuggerListener*> listeners_copy;
        {
            std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
            listeners_copy.assign(listeners_.begin(), listeners_.end());
        }
        for(auto* listener : listeners_copy) {
            listener->onStepComplete(address);
        }
    }

    void DosBoxCoreDebugger::notifyBreakpointHit(uint32_t address, BreakReason reason) {
        std::vector<CoreDebuggerListener*> listeners_copy;
        {
            std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
            listeners_copy.assign(listeners_.begin(), listeners_.end());
        }
        for(auto* listener : listeners_copy) {
            listener->onBreakpointHit(address, reason);
        }
    }

    void DosBoxCoreDebugger::notifyBreakpointListChanged() {
        std::vector<CoreDebuggerListener*> listeners_copy;
        {
            std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
            listeners_copy.assign(listeners_.begin(), listeners_.end());
        }
        for(auto* listener : listeners_copy) {
            listener->onBreakpointListChanged();
        }
    }

    //=============================================================================
    // Helper Methods
    //============
    // =================================================================
#undef max
// Performance optimization: Rebuild page bitmap when breakpoints change
    void DosBoxCoreDebugger::rebuildBreakpointPagesLocked() {
        // This method must be called with breakpoints_mutex_ held in write mode
        uint32_t max_addr = 0;
        for(const auto& bp : breakpoints_) {
            if(bp.enabled && bp.isConditional()) {
                max_addr = std::max(max_addr, bp.address);
            }
        }

        if(max_addr == 0) {
            breakpoint_pages_.clear();
            return;
        }

        uint32_t page_count = (max_addr >> PAGE_SHIFT) + 1;
        breakpoint_pages_.assign(page_count, 0);

        for(const auto& bp : breakpoints_) {
            if(!bp.enabled || !bp.isConditional()) continue;
            uint32_t page = bp.address >> PAGE_SHIFT;
            if(page < breakpoint_pages_.size()) {
                breakpoint_pages_[page] = 1;
            }
        }
    }

    bool DosBoxCoreDebugger::isCallInstruction(uint32_t address) {
        uint8_t opcode = mem_readb(address);
        // Basic check for CALL instructions
        return (opcode == 0xE8 || opcode == 0xFF || opcode == 0x9A);
    }

    bool DosBoxCoreDebugger::isRetInstruction(uint32_t address) {
        uint8_t opcode = mem_readb(address);
        // Basic check for RET instructions
        return (opcode == 0xC3 || opcode == 0xC2 || opcode == 0xCB || opcode == 0xCA);
    }

    uint32_t DosBoxCoreDebugger::getNextInstructionAddress(uint32_t address) {
        // Use the disassembler to get instruction length
        char dasm_buffer[256];
        uint32_t length = DasmI386(dasm_buffer, address, address, cpu.code.big);
        return address + length;
    }

    //=============================================================================
    // Missing Method Implementations
    //=============================================================================

    void DosBoxCoreDebugger::stepOut() {
        // Find the return address and run to it
        uint32_t esp = reg_esp;
        uint32_t ss_base = SegValue(ss) << 4;
        uint32_t return_address = mem_readw(ss_base + esp);  // Use 16-bit read for real mode
        runToCursor(return_address);
    }

    void DosBoxCoreDebugger::reset() {
        // Reset CPU state
        On_Software_CPU_Reset();
        clearAllBreakpoints();
        is_paused_.store(false);
        step_into_requested_.store(false);
        step_over_requested_.store(false);
    }

    void DosBoxCoreDebugger::runToCursor(uint32_t address) {
#if C_DEBUG
        // Convert address to segment:offset for DOSBox
        uint16_t seg = SegValue(cs);
        uint32_t cs_base = SegPhys(cs);
        uint16_t off = static_cast<uint16_t>(address - cs_base);

        // Debug output
        DEBUG_ShowMsg("DEBUG: Run to cursor - Target: %08X, Seg:Off: %04X:%04X\n", address, seg, off);

        // Add a temporary breakpoint (true = once only)
        CBreakpoint::AddBreakpoint(seg, off, true);

        // Set exitLoop to break out of debug loop
        extern bool exitLoop;
        exitLoop = true;

        // Activate breakpoints except at current location
        CBreakpoint::ActivateBreakpointsExceptAt(SegPhys(cs) + reg_eip);

        // Set state for resume
        debugging = false;
        debug_running = false;
        is_paused_.store(false);

        // Exit debug loop and return to normal execution
        DOSBOX_SetNormalLoop();

        // Update menu items to reflect resumed state
        extern DOSBoxMenu mainMenu;
        mainMenu.get_item("debugger_rundebug").check(false).refresh_item(mainMenu);
        mainMenu.get_item("debugger_runnormal").check(true).refresh_item(mainMenu);
        mainMenu.get_item("debugger_runwatch").check(false).refresh_item(mainMenu);
#endif
    }

    void DosBoxCoreDebugger::enableBreakpoint(uint32_t address, bool enabled) {
        bool changed = false;
        {
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            for(auto& bp : breakpoints_) {
                if(bp.address == address) {
                    if(bp.enabled != enabled) {
                        bp.enabled = enabled;
                        changed = true;

                        // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
                        rebuildBreakpointPagesLocked();  // OPTIMIZATION: Rebuild page bitmap
                        breakpoint_version_.fetch_add(1, std::memory_order_release);
                    }
                    break;
                }
            }
        }
        if(changed) {
            notifyBreakpointListChanged();
        }
    }

    void DosBoxCoreDebugger::clearAllBreakpoints() {
#if C_DEBUG
        // Clear all breakpoints from DOSBox's system
        CBreakpoint::DeleteAll();
#endif

        {
            // OPTIMIZATION: Use unique lock for writing
            std::unique_lock<std::shared_mutex> lock(breakpoints_mutex_);
            breakpoints_.clear();
            breakpoint_index_.clear();
            active_breakpoint_count_.store(0, std::memory_order_relaxed);

            // PHASE 4 LOCK-FREE HOT PATH: Increment version to invalidate thread-local caches
            breakpoint_pages_.clear();  // OPTIMIZATION: Clear page bitmap
            breakpoint_version_.fetch_add(1, std::memory_order_release);
        }
        notifyBreakpointListChanged();
    }

    std::string DosBoxCoreDebugger::disassembleInstruction(uint32_t address) {
        char dasm_buffer[256];
        DasmI386(dasm_buffer, address, address, cpu.code.big);
        return std::string(dasm_buffer);
    }

    std::string DosBoxCoreDebugger::getInstructionBytes(uint32_t address) {
        std::stringstream ss;
        uint32_t length = getNextInstructionAddress(address) - address;

        for(uint32_t i = 0; i < length && i < 16; ++i) {
            if(i > 0) ss << " ";
            ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(mem_readb(address + i));
        }

        return ss.str();
    }

    std::vector<uint8_t> DosBoxCoreDebugger::getInstructionBytesVector(uint32_t address) {
        std::vector<uint8_t> bytes;
        uint32_t length = getNextInstructionAddress(address) - address;

        const uint32_t max_instruction_length = 16;
        if(length > max_instruction_length) {
            length = max_instruction_length;
        }

        bytes.reserve(length);
        for(uint32_t i = 0; i < length; ++i) {
            bytes.push_back(mem_readb(address + i));
        }

        return bytes;
    }

    uint8_t DosBoxCoreDebugger::readByte(uint32_t address) {
        return mem_readb(address);
    }

    uint16_t DosBoxCoreDebugger::readWord(uint32_t address) {
        return mem_readw(address);
    }

    uint32_t DosBoxCoreDebugger::readDWord(uint32_t address) {
        return mem_readd(address);
    }

    //=============================================================================
    // Bulk Memory Operations
    //=============================================================================

    void DosBoxCoreDebugger::readMemoryBlock(uint32_t address, uint8_t* buffer, size_t size) {
        for(size_t i = 0; i < size; ++i) {
            buffer[i] = mem_readb(address + i);
        }
    }

    void DosBoxCoreDebugger::writeMemoryBlock(uint32_t address, const uint8_t* buffer, size_t size) {
        for(size_t i = 0; i < size; ++i) {
            mem_writeb(address + i, buffer[i]);
        }
    }

    //=============================================================================
    // Text Encoding Support for Japanese and Other Languages
    //=============================================================================

    std::string DosBoxCoreDebugger::convertToUTF8(const uint8_t* data, size_t length, uint16_t codepage) {
        switch(codepage) {
        case 932:  // Shift JIS / CP932
            return sjisToUTF8(data, length);
        case 437:  // CP437 (original DOS codepage)
            return cp437ToUTF8(data, length);
        case 850:  // CP850 (Western European)
            return cp850ToUTF8(data, length);
        case 936:  // GBK (Chinese Simplified)
            // Extended character set - requires iconv for full support
            return std::string(reinterpret_cast<const char*>(data), length);
        case 949:  // Korean
            // Extended character set - requires iconv for full support
            return std::string(reinterpret_cast<const char*>(data), length);
        case 950:  // Big5 (Chinese Traditional)
            // Extended character set - requires iconv for full support
            return std::string(reinterpret_cast<const char*>(data), length);
        default:
            // Default to CP437 for DOS compatibility
            return cp437ToUTF8(data, length);
        }
    }

    std::vector<uint8_t> DosBoxCoreDebugger::convertFromUTF8(const std::string& utf8_text, uint16_t codepage) {
        switch(codepage) {
        case 932:  // Shift JIS / CP932
            return utf8ToSJIS(utf8_text);
        case 437:  // CP437 (original DOS codepage)
            return utf8ToCP437(utf8_text);
        case 850:  // CP850 (Western European)
            return utf8ToCP850(utf8_text);
        case 936:  // GBK (Chinese Simplified)
            // Extended character set - requires iconv for full support
            return std::vector<uint8_t>(utf8_text.begin(), utf8_text.end());
        case 949:  // Korean
            // Extended character set - requires iconv for full support
            return std::vector<uint8_t>(utf8_text.begin(), utf8_text.end());
        case 950:  // Big5 (Chinese Traditional)
            // Extended character set - requires iconv for full support
            return std::vector<uint8_t>(utf8_text.begin(), utf8_text.end());
        default:
            // Default to CP437 for DOS compatibility
            return utf8ToCP437(utf8_text);
        }
    }

    std::string DosBoxCoreDebugger::readTextFromMemory(uint32_t address, size_t length, uint16_t codepage) {
        std::vector<uint8_t> buffer(length);
        readMemoryBlock(address, buffer.data(), length);
        return convertToUTF8(buffer.data(), length, codepage);
    }

    void DosBoxCoreDebugger::writeTextToMemory(uint32_t address, const std::string& text, uint16_t codepage) {
        try {
            // Validate input parameters
            if(text.length() > 0x100000) {  // Max 1MB text write
                return;
            }

            std::vector<uint8_t> encoded = convertFromUTF8(text, codepage);
            if(!encoded.empty()) {
                writeMemoryBlock(address, encoded.data(), encoded.size());
            }
        }
        catch(const std::exception&) {
            // Error during text conversion or writing, fail silently
        }
        catch(...) {
            // Any other exception, fail silently
        }
    }

    uint16_t DosBoxCoreDebugger::getCurrentCodepage() {
        return current_codepage_;
    }

    void DosBoxCoreDebugger::setCurrentCodepage(uint16_t codepage) {
        current_codepage_ = codepage;
        japanese_mode_ = (codepage == 932);
    }

    void DosBoxCoreDebugger::goToAddress(uint32_t address) {
        // Set the follow address for debugger UI navigation
        follow_address_ = address;
        current_address_ = address;

        // Navigate to address %08X
        (void)address;
    }

    //=============================================================================
    // Encoding Helper Methods
    //=============================================================================

    std::string DosBoxCoreDebugger::sjisToUTF8(const uint8_t* sjis_data, size_t length) {
        // Enhanced SJIS to UTF-8 conversion with proper character mapping
        std::string result;

        for(size_t i = 0; i < length; ++i) {
            uint8_t byte1 = sjis_data[i];

            // ASCII characters (0x00-0x7F)
            if(byte1 <= 0x7F) {
                result += static_cast<char>(byte1);
                continue;
            }

            // Half-width katakana (0xA1-0xDF)
            if(byte1 >= 0xA1 && byte1 <= 0xDF) {
                // Convert to UTF-8 half-width katakana (Unicode range FF61-FF9F)
                uint16_t unicode = 0xFF61 + (byte1 - 0xA1);

                // Encode as UTF-8
                result += static_cast<char>(0xEF);
                result += static_cast<char>(0xBD);
                result += static_cast<char>(0xA1 + (byte1 - 0xA1));
                continue;
            }

            // Double-byte characters
            if(i + 1 < length) {
                uint8_t byte2 = sjis_data[i + 1];

                // Check for valid SJIS double-byte sequence
                if(((byte1 >= 0x81 && byte1 <= 0x9F) || (byte1 >= 0xE0 && byte1 <= 0xEF)) &&
                    ((byte2 >= 0x40 && byte2 <= 0x7E) || (byte2 >= 0x80 && byte2 <= 0xFC))) {

                    // Convert SJIS to Unicode
                    uint16_t unicode = sjisToUnicode(byte1, byte2);

                    if(unicode != 0) {
                        // Encode Unicode to UTF-8
                        if(unicode <= 0x7F) {
                            result += static_cast<char>(unicode);
                        }
                        else if(unicode <= 0x7FF) {
                            result += static_cast<char>(0xC0 | (unicode >> 6));
                            result += static_cast<char>(0x80 | (unicode & 0x3F));
                        }
                        else {
                            result += static_cast<char>(0xE0 | (unicode >> 12));
                            result += static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (unicode & 0x3F));
                        }
                    }
                    else {
                        // Unknown character - use replacement character
                        result += "\xEF\xBF\xBD";
                    }

                    i++;  // Skip the second byte
                    continue;
                }
            }

            // Unknown character - use replacement character
            result += "\xEF\xBF\xBD";
        }

        return result;
    }

    std::vector<uint8_t> DosBoxCoreDebugger::utf8ToSJIS(const std::string& utf8_text) {
        // Enhanced UTF-8 to SJIS conversion using proper Unicode decoding
        std::vector<uint8_t> result;

        for(size_t i = 0; i < utf8_text.length(); ++i) {
            uint8_t byte = utf8_text[i];

            // ASCII characters
            if(byte <= 0x7F) {
                result.push_back(byte);
                continue;
            }

            // Multi-byte UTF-8 characters
            uint16_t unicode = 0;

            if((byte & 0xE0) == 0xC0) {
                // 2-byte UTF-8 sequence
                if(i + 1 < utf8_text.length()) {
                    uint8_t byte2 = utf8_text[i + 1];
                    if((byte2 & 0xC0) == 0x80) {
                        unicode = ((byte & 0x1F) << 6) | (byte2 & 0x3F);
                        i++; // Skip the second byte
                    }
                }
            }
            else if((byte & 0xF0) == 0xE0) {
                // 3-byte UTF-8 sequence
                if(i + 2 < utf8_text.length()) {
                    uint8_t byte2 = utf8_text[i + 1];
                    uint8_t byte3 = utf8_text[i + 2];
                    if((byte2 & 0xC0) == 0x80 && (byte3 & 0xC0) == 0x80) {
                        unicode = ((byte & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
                        i += 2; // Skip the second and third bytes
                    }
                }
            }

            // Convert Unicode to SJIS
            if(unicode != 0) {
                auto sjis_bytes = unicodeToSJIS(unicode);
                result.push_back(sjis_bytes.first);
                if(sjis_bytes.second != 0) {
                    result.push_back(sjis_bytes.second);
                }
            }
            else {
                // Unknown character - use replacement
                result.push_back('?');
            }
        }

        return result;
    }

    std::string DosBoxCoreDebugger::cp932ToUTF8(const uint8_t* cp932_data, size_t length) {
        // CP932 is essentially the same as Shift JIS for our purposes
        return sjisToUTF8(cp932_data, length);
    }

    std::vector<uint8_t> DosBoxCoreDebugger::utf8ToCP932(const std::string& utf8_text) {
        // CP932 is essentially the same as Shift JIS for our purposes
        return utf8ToSJIS(utf8_text);
    }

    //=============================================================================
    // Helper Methods for Analysis Functions
    //=============================================================================

    bool DosBoxCoreDebugger::evaluateBreakpointCondition(const std::string& condition, uint32_t address) {
        // Basic condition evaluation - this is a simplified implementation
        // For a full implementation, we would need to integrate with Lua properly

        // Handle simple register comparisons
        if(condition.find("EAX") != std::string::npos) {
            // Extract simple conditions like "EAX == 0x1000"
            if(condition.find("==") != std::string::npos) {
                size_t eq_pos = condition.find("==");
                std::string value_str = condition.substr(eq_pos + 2);

                // Remove whitespace
                value_str.erase(std::remove_if(value_str.begin(), value_str.end(), ::isspace), value_str.end());

                // Parse hex value
                uint32_t expected_value = 0;
                if(value_str.substr(0, 2) == "0x" || value_str.substr(0, 2) == "0X") {
                    expected_value = std::stoul(value_str, nullptr, 16);
                }
                else {
                    expected_value = std::stoul(value_str, nullptr, 10);
                }

                return (reg_eax == expected_value);
            }
        }

        // For more complex conditions, we would need full Lua integration
        // For now, return true to not break existing functionality
        return true;
    }

    void DosBoxCoreDebugger::parseDisassemblyText(const std::string& full_text, std::string& mnemonic, std::string& operands) {
        // Parse DOSBox-X disassembly format
        // Format is typically: "MOV AX,1000" or "JMP 1000:0000"

        size_t space_pos = full_text.find(' ');
        if(space_pos != std::string::npos) {
            mnemonic = full_text.substr(0, space_pos);
            operands = full_text.substr(space_pos + 1);

            // Remove any leading/trailing whitespace
            while(!operands.empty() && std::isspace(operands.front())) {
                operands.erase(operands.begin());
            }
            while(!operands.empty() && std::isspace(operands.back())) {
                operands.pop_back();
            }
        }
        else {
            // No operands - just the mnemonic
            mnemonic = full_text;
            operands = "";
        }

        // Remove any leading/trailing whitespace from mnemonic
        while(!mnemonic.empty() && std::isspace(mnemonic.front())) {
            mnemonic.erase(mnemonic.begin());
        }
        while(!mnemonic.empty() && std::isspace(mnemonic.back())) {
            mnemonic.pop_back();
        }
    }

    //=============================================================================
    // Japanese Text Encoding Helper Methods
    //=============================================================================

    uint16_t DosBoxCoreDebugger::sjisToUnicode(uint8_t byte1, uint8_t byte2) {
        // Enhanced SJIS to Unicode conversion with basic character mapping
        // This provides mapping for common Japanese characters

        if(!isValidSJISSequence(byte1, byte2)) {
            return 0; // Invalid sequence
        }

        // Convert SJIS coordinates to JIS coordinates
        uint8_t jis1, jis2;

        if(byte1 >= 0x81 && byte1 <= 0x9F) {
            jis1 = (byte1 - 0x81) * 2 + 0x21;
        }
        else if(byte1 >= 0xE0 && byte1 <= 0xEF) {
            jis1 = (byte1 - 0xE0 + 0x1F) * 2 + 0x21;
        }
        else {
            return 0;
        }

        if(byte2 >= 0x40 && byte2 <= 0x7E) {
            jis2 = byte2 - 0x40 + 0x21;
        }
        else if(byte2 >= 0x80 && byte2 <= 0x9E) {
            jis2 = byte2 - 0x80 + 0x40;
        }
        else if(byte2 >= 0x9F && byte2 <= 0xFC) {
            jis1++;
            jis2 = byte2 - 0x9F + 0x21;
        }
        else {
            return 0;
        }

        // Basic mapping for common ranges
        // Hiragana (あ-ん)
        if(jis1 == 0x24 && jis2 >= 0x21 && jis2 <= 0x76) {
            return 0x3041 + (jis2 - 0x21);
        }

        // Katakana (ア-ン)  
        if(jis1 == 0x25 && jis2 >= 0x21 && jis2 <= 0x76) {
            return 0x30A1 + (jis2 - 0x21);
        }

        // Basic kanji mapping (simplified)
        if(jis1 >= 0x30 && jis1 <= 0x4F && jis2 >= 0x21 && jis2 <= 0x7E) {
            // Map to CJK unified ideographs area (simplified mapping)
            return 0x4E00 + ((jis1 - 0x30) * 94 + (jis2 - 0x21));
        }

        // Default for unmapped characters
        return 0x25A1; // White square symbol as fallback
    }

    std::pair<uint8_t, uint8_t> DosBoxCoreDebugger::unicodeToSJIS(uint16_t unicode) {
        // Enhanced Unicode to SJIS conversion

        // ASCII characters
        if(unicode <= 0x7F) {
            return { static_cast<uint8_t>(unicode), 0 };
        }

        // Half-width katakana
        if(unicode >= 0xFF61 && unicode <= 0xFF9F) {
            return { static_cast<uint8_t>(unicode - 0xFF61 + 0xA1), 0 };
        }

        // Hiragana
        if(unicode >= 0x3041 && unicode <= 0x3096) {
            uint8_t jis2 = static_cast<uint8_t>(unicode - 0x3041 + 0x21);
            // Convert back to SJIS
            uint8_t byte1 = 0x82; // First byte for hiragana in SJIS
            uint8_t byte2 = jis2 + 0x9F;
            if(byte2 > 0xFC) {
                byte2 = jis2 + 0x40;
            }
            return { byte1, byte2 };
        }

        // Katakana
        if(unicode >= 0x30A1 && unicode <= 0x30F6) {
            uint8_t jis2 = static_cast<uint8_t>(unicode - 0x30A1 + 0x21);
            // Convert back to SJIS
            uint8_t byte1 = 0x83; // First byte for katakana in SJIS
            uint8_t byte2 = jis2 + 0x9F;
            if(byte2 > 0xFC) {
                byte2 = jis2 + 0x40;
            }
            return { byte1, byte2 };
        }

        // CJK ideographs (simplified mapping)
        if(unicode >= 0x4E00 && unicode <= 0x9FFF) {
            // Very basic mapping - real implementation would need full tables
            uint16_t offset = unicode - 0x4E00;
            uint8_t jis1 = static_cast<uint8_t>(0x30 + (offset / 94));
            uint8_t jis2 = static_cast<uint8_t>(0x21 + (offset % 94));

            // Convert JIS to SJIS
            uint8_t byte1, byte2;
            if(jis1 % 2 == 1) {
                byte1 = (jis1 - 0x21) / 2 + 0x81;
                if(byte1 > 0x9F) byte1 += 0x40;
                byte2 = jis2 + 0x40;
                if(byte2 > 0x7E) byte2++;
            }
            else {
                byte1 = (jis1 - 0x22) / 2 + 0x81;
                if(byte1 > 0x9F) byte1 += 0x40;
                byte2 = jis2 + 0x9E;
            }

            return { byte1, byte2 };
        }

        // Unknown character - return replacement
        return { 0x81, 0x48 }; // SJIS replacement character
    }

    bool DosBoxCoreDebugger::isValidSJISSequence(uint8_t byte1, uint8_t byte2) {
        // Check if the byte sequence is a valid SJIS double-byte character
        if((byte1 >= 0x81 && byte1 <= 0x9F) || (byte1 >= 0xE0 && byte1 <= 0xEF)) {
            if((byte2 >= 0x40 && byte2 <= 0x7E) || (byte2 >= 0x80 && byte2 <= 0xFC)) {
                return true;
            }
        }
        return false;
    }

    //=============================================================================
    // CP437 and CP850 DOS Codepage Support
    //=============================================================================

    std::string DosBoxCoreDebugger::cp437ToUTF8(const uint8_t* cp437_data, size_t length) {
        // CP437 to UTF-8 conversion table for extended ASCII (128-255)
        static const uint16_t cp437_to_unicode[128] = {
            0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,  // 128-135
            0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,  // 136-143
            0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,  // 144-151
            0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,  // 152-159
            0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,  // 160-167
            0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,  // 168-175
            0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,  // 176-183
            0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,  // 184-191
            0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,  // 192-199
            0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,  // 200-207
            0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,  // 208-215
            0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,  // 216-223
            0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,  // 224-231
            0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,  // 232-239
            0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,  // 240-247
            0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0   // 248-255
        };

        std::string result;
        result.reserve(length * 2); // Pre-allocate space for efficiency

        for(size_t i = 0; i < length; ++i) {
            uint8_t byte = cp437_data[i];

            // ASCII characters (0-127)
            if(byte <= 0x7F) {
                result += static_cast<char>(byte);
            }
            else {
                // Extended ASCII (128-255) - convert to Unicode then UTF-8
                uint16_t unicode = cp437_to_unicode[byte - 128];

                // Encode Unicode to UTF-8
                if(unicode <= 0x7FF) {
                    result += static_cast<char>(0xC0 | (unicode >> 6));
                    result += static_cast<char>(0x80 | (unicode & 0x3F));
                }
                else {
                    result += static_cast<char>(0xE0 | (unicode >> 12));
                    result += static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (unicode & 0x3F));
                }
            }
        }

        return result;
    }

    std::vector<uint8_t> DosBoxCoreDebugger::utf8ToCP437(const std::string& utf8_text) {
        // Simple UTF-8 to CP437 conversion with basic character mapping
        std::vector<uint8_t> result;
        result.reserve(utf8_text.length()); // Pre-allocate space

        for(size_t i = 0; i < utf8_text.length(); ++i) {
            uint8_t byte = utf8_text[i];

            // ASCII characters (0-127)
            if(byte <= 0x7F) {
                result.push_back(byte);
                continue;
            }

            // Multi-byte UTF-8 - decode to Unicode first
            uint16_t unicode = 0;
            if((byte & 0xE0) == 0xC0 && i + 1 < utf8_text.length()) {
                // 2-byte sequence
                uint8_t byte2 = utf8_text[i + 1];
                if((byte2 & 0xC0) == 0x80) {
                    unicode = ((byte & 0x1F) << 6) | (byte2 & 0x3F);
                    i++;
                }
            }
            else if((byte & 0xF0) == 0xE0 && i + 2 < utf8_text.length()) {
                // 3-byte sequence
                uint8_t byte2 = utf8_text[i + 1];
                uint8_t byte3 = utf8_text[i + 2];
                if((byte2 & 0xC0) == 0x80 && (byte3 & 0xC0) == 0x80) {
                    unicode = ((byte & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
                    i += 2;
                }
            }

            // Convert Unicode back to CP437 (basic mapping)
            if(unicode != 0) {
                // Look for common Unicode characters in CP437 range
                if(unicode >= 0x00C0 && unicode <= 0x00FF) {
                    // Basic Latin-1 supplement - map some common characters
                    switch(unicode) {
                    case 0x00C7: result.push_back(128); break; // Ç
                    case 0x00FC: result.push_back(129); break; // ü
                    case 0x00E9: result.push_back(130); break; // é
                    case 0x00E2: result.push_back(131); break; // â
                    case 0x00E4: result.push_back(132); break; // ä
                    case 0x00E0: result.push_back(133); break; // à
                    case 0x00E5: result.push_back(134); break; // å
                    case 0x00E7: result.push_back(135); break; // ç
                    default: result.push_back('?'); break;
                    }
                }
                else {
                    // Unknown Unicode character
                    result.push_back('?');
                }
            }
            else {
                // Invalid UTF-8 sequence
                result.push_back('?');
            }
        }

        return result;
    }

    std::string DosBoxCoreDebugger::cp850ToUTF8(const uint8_t* cp850_data, size_t length) {
        // CP850 to UTF-8 conversion - similar to CP437 but with different extended chars
        // For now, use CP437 mapping as base (many characters overlap)
        return cp437ToUTF8(cp850_data, length);
    }

    std::vector<uint8_t> DosBoxCoreDebugger::utf8ToCP850(const std::string& utf8_text) {
        // UTF-8 to CP850 conversion - use CP437 as base
        return utf8ToCP437(utf8_text);
    }

    void DosBoxCoreDebugger::enableCallStackTracing(bool enable) {
        std::lock_guard<std::mutex> lock(call_stack_mutex_);
        trace_call_stack_enabled_ = enable;
        if(!enable) {
            traced_call_stack_.clear();
        }
    }

    void DosBoxCoreDebugger::onCallInstruction(uint32_t call_addr, uint32_t target_addr, bool is_far) {
        if(!trace_call_stack_enabled_) return;

        std::lock_guard<std::mutex> lock(call_stack_mutex_);

        TracedCallEntry entry;
        entry.call_site = call_addr;
        entry.target_address = target_addr;
        entry.call_cs = SegValue(cs);
        entry.stack_pointer = reg_esp;
        entry.is_far_call = is_far;
        entry.timestamp = std::chrono::steady_clock::now();

        // Calculate return address (address after the CALL instruction)
        uint32_t call_length = getNextInstructionAddress(call_addr) - call_addr;
        entry.return_address = call_addr + call_length;

        if(is_far) {
            entry.target_cs = static_cast<uint16_t>((target_addr >> 16) & 0xFFFF);
        }
        else {
            entry.target_cs = entry.call_cs;
        }

        traced_call_stack_.push_back(entry);

        // Prevent unbounded growth
        const size_t MAX_TRACED_DEPTH = 1024;
        if(traced_call_stack_.size() > MAX_TRACED_DEPTH) {
            traced_call_stack_.erase(traced_call_stack_.begin());
        }
    }

    void DosBoxCoreDebugger::onReturnInstruction(uint32_t ret_addr) {
        if(!trace_call_stack_enabled_) return;

        std::lock_guard<std::mutex> lock(call_stack_mutex_);

        if(traced_call_stack_.empty()) {
            return;
        }

        TracedCallEntry& top = traced_call_stack_.back();

        uint32_t expected_ret = top.return_address;
        uint32_t actual_linear = SegPhys(cs) + ret_addr;

        if(actual_linear == expected_ret || ret_addr == (expected_ret & 0xFFFF)) {
            // Normal return
            traced_call_stack_.pop_back();
            return;
        }

        // Mismatched return - attempt to unwind to a matching frame
        for(auto it = traced_call_stack_.rbegin(); it != traced_call_stack_.rend(); ++it) {
            if(it->return_address == actual_linear ||
                (it->return_address & 0xFFFF) == ret_addr) {
                traced_call_stack_.erase(it.base(), traced_call_stack_.end());
                return;
            }
        }

        // If nothing matched, drop the top entry
        traced_call_stack_.pop_back();
    }

    std::vector<CallStackFrame> DosBoxCoreDebugger::getTracedCallStack() const {
        std::lock_guard<std::mutex> lock(call_stack_mutex_);

        std::vector<CallStackFrame> result;

        // Add current execution point
        uint32_t cs_base = SegPhys(cs);
        uint32_t current_bp = cpu.stack.big ? reg_ebp : (reg_ebp & 0xFFFF);
        result.emplace_back(cs_base + reg_eip, current_bp, SegValue(cs), reg_eip, false);
        result.back().function_name = resolveSymbolName(cs_base + reg_eip);

        // Add traced entries from newest to oldest
        for(auto it = traced_call_stack_.rbegin(); it != traced_call_stack_.rend(); ++it) {
            uint32_t linear = it->return_address;
            CallStackFrame frame(linear, 0, it->call_cs, it->return_address, it->is_far_call);
            frame.function_name = resolveSymbolName(linear);
            result.push_back(frame);
        }

        return result;
    }

    void DosBoxCoreDebugger::clearTracedCallStack() {
        std::lock_guard<std::mutex> lock(call_stack_mutex_);
        traced_call_stack_.clear();
    }

    void DosBoxCoreDebugger::dumpStackFrameDebugInfo() {
        // Stack Frame Debug Info
        (void)cpu.pmode; (void)reg_flags; (void)cpu.stack.big; (void)cpu.code.big;
        (void)SegValue(cs); (void)reg_eip; (void)SegPhys; (void)SegValue(ss); (void)reg_esp; (void)reg_ebp;

        uint32_t ss_base = SegPhys(ss);
        uint32_t bp = cpu.stack.big ? reg_ebp : (reg_ebp & 0xFFFF);

        for(int i = -2; i <= 8; ++i) {
            uint32_t offset = bp + (i * (cpu.stack.big ? 4 : 2));
            if(cpu.stack.big) {
                uint32_t value = mem_readd(ss_base + offset);
                // [BP%+d] = [%08X] = %08X%s
                (void)i; (void)offset; (void)value;
            }
            else {
                uint16_t value = mem_readw(ss_base + offset);
                // [BP%+d] = [%04X] = %04X%s
                (void)i; (void)offset; (void)value;
            }
        }
    }

    bool DosBoxCoreDebugger::isValidSegmentValue(uint16_t seg, bool is_protected_mode) {
        if(seg == 0 || seg == 0xFFFF) return false;

        if(is_protected_mode) {
            // In protected mode, selector index must be within table limits
            uint16_t index = seg >> 3;
            return index > 0 && index < 8192;
        }

        // Real mode: allow BIOS ranges too
        return seg < 0x10000;
    }

    bool DosBoxCoreDebugger::isLikelyCodeSegment(uint16_t seg, uint32_t ip, bool is_protected_mode) {
        if(!isValidSegmentValue(seg, is_protected_mode)) {
            return false;
        }

        if(is_protected_mode) {
            return true;
        }

        uint32_t linear = (seg << 4) + ip;
        if(linear >= 0x100000) {
            return false;
        }

        uint8_t byte_at_return = mem_readb(linear);

        bool looks_like_code = (
            (byte_at_return >= 0x50 && byte_at_return <= 0x5F) ||  // PUSH/POP
            (byte_at_return >= 0x88 && byte_at_return <= 0x8B) ||  // MOV variants
            byte_at_return == 0x83 ||  // ADD/SUB imm8
            byte_at_return == 0xE8 ||  // CALL
            byte_at_return == 0xE9 ||  // JMP
            byte_at_return == 0x90 ||  // NOP
            byte_at_return == 0xC3 ||  // RET
            byte_at_return == 0xB8 ||  // MOV EAX, imm
            byte_at_return == 0x31 ||  // XOR
            byte_at_return == 0x33     // XOR
        );

        return looks_like_code;
    }

    uint32_t DosBoxCoreDebugger::getSegmentBase(uint16_t selector) {
        if(!cpu.pmode || (reg_flags & FLAG_VM)) {
            return selector << 4;
        }

        bool use_ldt = (selector & 0x04) != 0;
        uint16_t index = selector >> 3;

        if(index == 0) {
            return 0;
        }

        uint32_t table_base = cpu.gdt.GetBase();
        uint16_t table_limit = cpu.gdt.GetLimit();

        (void)use_ldt; // Placeholder until LDT support is wired in

        uint32_t desc_offset = index * 8;
        if(desc_offset + 7 > table_limit) {
            return 0;
        }

        uint32_t desc_addr = table_base + desc_offset;
        uint32_t desc_low = mem_readd(desc_addr);
        uint32_t desc_high = mem_readd(desc_addr + 4);

        uint32_t base = ((desc_low >> 16) & 0xFFFF) |
            ((desc_high & 0xFF) << 16) |
            (desc_high & 0xFF000000);

        return base;
    }

    std::string DosBoxCoreDebugger::resolveSymbolName(uint32_t address) const {
        // Placeholder until integrated with symbol subsystem
        (void)address;
        return "";
    }

    std::vector<CallStackFrame> DosBoxCoreDebugger::getCallStackCombined() {
        if(trace_call_stack_enabled_) {
            auto traced = getTracedCallStack();
            if(traced.size() > 1) {
                return traced;
            }
        }

        std::vector<CallStackFrame> stack;

        bool is_v86_mode = (reg_flags & FLAG_VM) != 0;
        bool is_protected_mode = cpu.pmode && !is_v86_mode;
        bool is_real_mode = !cpu.pmode && !is_v86_mode;
        bool is_32bit_stack = cpu.stack.big;
        bool is_32bit_code = cpu.code.big;

        uint32_t cs_base = SegPhys(cs);
        uint32_t ss_base = SegPhys(ss);

        uint32_t current_eip = reg_eip;
        uint16_t current_cs = SegValue(cs);
        uint32_t current_esp = reg_esp;

        uint32_t current_bp = is_32bit_stack ? reg_ebp : (reg_ebp & 0xFFFF);



        CallStackFrame current_frame(cs_base + current_eip, current_bp, current_cs, current_eip, false);
        current_frame.function_name = resolveSymbolName(cs_base + current_eip);
        stack.push_back(current_frame);

        if(current_bp == 0) {
            return stack;
        }

        uint32_t stack_limit = is_32bit_stack ? 0xFFFFFFFF : 0xFFFF;
        if(current_bp > stack_limit) {

            return stack;
        }

        bool default_to_far_calls = is_real_mode || is_v86_mode;

        std::set<uint32_t> visited_frames;
        const int MAX_STACK_DEPTH = 64;

        uint32_t frame_bp = current_bp;
        uint16_t frame_cs = current_cs;

        for(int depth = 0; depth < MAX_STACK_DEPTH; ++depth) {
            if(visited_frames.count(frame_bp)) {
                break;
            }
            visited_frames.insert(frame_bp);

            if(frame_bp == 0) {
                break;
            }

            uint32_t alignment = is_32bit_stack ? 4 : 2;
            if((frame_bp % alignment) != 0) {
            }

            uint32_t frame_phys = ss_base + frame_bp;

            uint32_t saved_bp = 0;
            uint32_t ret_ip = 0;
            uint16_t ret_cs = frame_cs;
            bool is_far = false;

            if(is_32bit_stack) {
                saved_bp = mem_readd(frame_phys);
                ret_ip = mem_readd(frame_phys + 4);

                if(default_to_far_calls) {
                    uint16_t potential_cs = mem_readw(frame_phys + 8);
                    if(isValidSegmentValue(potential_cs, is_protected_mode)) {
                        ret_cs = potential_cs;
                        is_far = true;
                    }
                }
            }
            else {
                saved_bp = mem_readw(frame_phys);
                ret_ip = mem_readw(frame_phys + 2);

                if(default_to_far_calls) {
                    uint16_t potential_cs = mem_readw(frame_phys + 4);

                    if(isLikelyCodeSegment(potential_cs, ret_ip, is_protected_mode)) {
                        ret_cs = potential_cs;
                        is_far = true;
                        // Far call detected
                    }
                    else {
                        // Near call (potential CS invalid)
                    }
                }
            }

            // Frame debug info: BP=%08X, SavedBP=%08X, RetAddr=%04X:%08X, Far=%d
            (void)depth; (void)frame_bp; (void)saved_bp; (void)ret_cs; (void)ret_ip; (void)is_far;

            if(ret_ip == 0 && ret_cs == 0) {
                // Null return address, stopping
                break;
            }

            if(saved_bp != 0 && saved_bp <= frame_bp) {
                // Frame chain direction invalid: saved_bp=%08X <= frame_bp=%08X
                (void)saved_bp; (void)frame_bp;

                uint32_t ret_base = is_real_mode ? (ret_cs << 4) : getSegmentBase(ret_cs);
                CallStackFrame frame(ret_base + ret_ip, saved_bp, ret_cs, ret_ip, is_far);
                frame.function_name = resolveSymbolName(ret_base + ret_ip);
                stack.push_back(frame);
                break;
            }

            uint32_t ret_linear;
            if(is_real_mode || is_v86_mode) {
                ret_linear = (ret_cs << 4) + ret_ip;
            }
            else {
                ret_linear = getSegmentBase(ret_cs) + ret_ip;
            }

            CallStackFrame frame(ret_linear, saved_bp, ret_cs, ret_ip, is_far);
            frame.function_name = resolveSymbolName(ret_linear);
            stack.push_back(frame);

            frame_bp = saved_bp;
            frame_cs = ret_cs;

            if(!is_32bit_stack) {
                frame_bp &= 0xFFFF;
            }
        }

        // Call stack walk complete, %zu frames found
        (void)stack.size();
        return stack;
    }

    std::vector<CallStackFrame> DosBoxCoreDebugger::getCallStack() {
        return getCallStackCombined();
    }

} // namespace LuaEngineDebug
