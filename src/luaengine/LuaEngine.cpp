// Standard DosBox-X headers
#include "luaengine.h"
#include "lua_re_hooks.h"
#include "pc98_cdl.h"
#include <list>           // Required for debug.h
#include "debug.h"
#include "regs.h"         // Required for cpu_regs and flag management
#include "cpu.h"          // For SegValue and GetAddress functions/macros
#include "setup.h"        // For Section configuration system
#include "control.h"      // For accessing control->GetSection()
#include "logging.h"      // For DEBUG_ShowMsg
#include <cctype>         // For std::tolower
#include <cstdint>
#include "../debug/debug_inc.h" // For ParseCommand bridge and debugger callbacks
#include "debug_bridge.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>     // Must be included before windows.h for Asio
#include <ws2tcpip.h>
#define NOMINMAX
#include <windows.h>      // For SEH support
#include <excpt.h>
#undef min
#undef max 
#endif

// Forward declarations for DOSBox-X savestate functions
// These are either implemented in src/misc/savestates.cpp or in savestate_shim.cpp
extern "C" {
    void SaveGameState(bool pressed);
    void LoadGameState(bool pressed);
    std::uint64_t GetGameState(void);
    void SetGameState(int value);
}

LuaEngine luaEngine;

// Global instance protection mutex
std::mutex g_luaengine_mutex;

// PHASE 2 OPTIMIZATION: Ultra-fast relaxed atomic for instruction hook fast path
// This is the cheapest possible check (no memory barriers on x86) for the hot path
static std::atomic<bool> g_fast_instruction_hook_active{false};

// External debugger flag from debug/debug.cpp
extern bool skipFirstInstruction;
extern bool inhibit_int_breakpoint;

extern "C" void LuaEngine_LogDebuggerMessage(const char* msg) {
    if (luaEngine.logger && msg) {
        luaEngine.log_info(msg);
    }
}

// Thread-safe global instance access helpers
template<typename Func>
auto SafeLuaEngineAccess(Func&& func) -> decltype(func(luaEngine)) {
    std::lock_guard<std::mutex> lock(g_luaengine_mutex);
    return func(luaEngine);
}

// Thread-safe void function wrapper
template<typename Func>
void SafeLuaEngineAccessVoid(Func&& func) {
    std::lock_guard<std::mutex> lock(g_luaengine_mutex);
    func(luaEngine);
}

// Ensure proper memory access helpers from DosBox core
#include "mem.h"          // mem_writeb_checked
#include "paging.h"       // DosBox memory paging definitions

// For hot-reload functionality
#include <sys/stat.h>     // For file modification time
#include <ctime>          // For time_t

// For symbol file loading
#include <fstream>        // For file I/O
#include <sstream>        // For string stream parsing
#include <regex>          // For pattern matching

// Debug interface
#include "core_debug_interface.h"
#include "lua_symbol_bindings.h"  // Expose 'symbols' table to Lua scripts
#include "symbolic_breakpoints.h"

// For disassembly functionality
#define C_DEBUG 1        // Enable debug functionality for DasmI386
#include "debug.h"        // For DasmI386 function and debug utilities
// debug_inc.h already included at top of file — no include guards, cannot include twice

// For path debugging
#ifdef _WIN32
#include <direct.h>       // For _getcwd
#include <stdlib.h>       // For _fullpath
#define getcwd _getcwd
#else
#include <unistd.h>       // For getcwd
#include <stdlib.h>       // For realpath
#endif

// For performance monitoring
#include <chrono>         // For high-resolution timing
#include <algorithm>      // For min/max
#include <sstream>        // For stringstream in stub functions
#include <iomanip>        // For std::setfill, std::setw, std::setprecision
#include <cmath>          // For std::floor
#include <thread>         // For sleep during debugger sync

// External libraries (conditional compilation for when available)
#define HAVE_IMGUI 1
#define HAVE_SDL   1
#define HAVE_OPENGL 1

#ifdef HAVE_IMGUI
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_opengl3.h>
// Include an OpenGL header for functions like glViewport and glClear.
// GL/gl.h is used here because the Visual Studio tree does not ship glew.
#ifdef HAVE_OPENGL
#include <GL/gl.h>
#endif
#endif

#ifdef HAVE_SDL
// Use standard include path for portability. Ensure your build system can find SDL2.
#include <SDL.h>
#endif

// Include Lua engine subsystems
// #include "gui_overlay.h" // Removed - GUI overlay system disabled
// #include "input_recorder.h" // Removed - input recorder system disabled
#include "gui_windows.h"
#include "trace_logger.h"
#include "lua_memory_domains.h"

// Memory Utility Typedefs
using PhysPt = uint32_t;  // Physical address type (matches DosBox's PhysPt/LinearAdr patterns)

// --- Memory Access Utilities ---
bool WriteBytes(uint16_t seg, uint32_t ofs, const std::vector<uint8_t>& data) {
    // Thread-safe access to luaEngine for performance recording
    SafeLuaEngineAccessVoid([](LuaEngine& engine) {
        engine.recordMemoryOperation();
    });

    // Early exit for empty data
    if(data.empty()) return true;

    // Thread-safe check for fast path settings
    bool use_fast_path = SafeLuaEngineAccess([](LuaEngine& engine) -> bool {
        return !engine.event_manager ||
               (engine.sol2_cache.fast_memory_access &&
                engine.sol2_cache.minimal_error_checking);
    });

    if(use_fast_path && data.size() > 16) {
        // Fast bulk write for large requests
        const size_t CHUNK_SIZE = 256;
        for(size_t chunk_start = 0; chunk_start < data.size(); chunk_start += CHUNK_SIZE) {
            size_t chunk_size = std::min(CHUNK_SIZE, data.size() - chunk_start);

            // Calculate physical address for this chunk
            PhysPt chunk_phys = static_cast<PhysPt>(GetAddress(seg, ofs + chunk_start));

            // Write chunk directly
            for(size_t i = 0; i < chunk_size; ++i) {
                mem_writeb(chunk_phys + i, data[chunk_start + i]);
            }
        }
    }
    else {
        // Standard path with full error checking and events
        for(size_t i = 0; i < data.size(); ++i) {
            PhysPt phys = static_cast<PhysPt>(GetAddress(seg, ofs + i));

            if(!use_fast_path) {
                // Verify we can read the location first
                uint8_t dummy;
                if(mem_readb_checked(phys, &dummy)) {
                    DEBUG_ShowMsg("ERROR: Failed to read byte at %04X:%08X", seg, ofs + i);
                    return false;
                }

                // Fire memory write event before actual write
                if(luaEngine.event_manager) {
                    LuaEngineEvents::MemoryEventData mem_event;
                    mem_event.segment = seg;
                    mem_event.offset = ofs + i;
                    mem_event.physical_addr = phys;
                    mem_event.data = data[i];
                    mem_event.access_size = 1;
                    mem_event.is_write = true;
                    luaEngine.event_manager->fireMemoryEvent(mem_event);
                }
            }

            mem_writeb(phys, data[i]);
        }
    }

    return true;
}

std::vector<uint8_t> GetBytes(uint16_t seg, uint32_t ofs, size_t size) {
    luaEngine.recordMemoryOperation(); // Record performance statistics

    // Early exit for empty requests
    if(size == 0) return std::vector<uint8_t>();

    std::vector<uint8_t> buffer;
    buffer.reserve(size);

    // Check if we can use fast path (no event manager or performance mode enabled)
    bool use_fast_path = !luaEngine.event_manager ||
        (luaEngine.sol2_cache.fast_memory_access &&
            luaEngine.sol2_cache.minimal_error_checking);

    if(use_fast_path && size > 16) {
        // Fast bulk read for large requests
        PhysPt base_phys = static_cast<PhysPt>(GetAddress(seg, ofs));

        // Try to read in chunks to minimize address translation overhead
        const size_t CHUNK_SIZE = 256;
        for(size_t chunk_start = 0; chunk_start < size; chunk_start += CHUNK_SIZE) {
            size_t chunk_size = std::min(CHUNK_SIZE, size - chunk_start);

            // Calculate physical address for this chunk
            PhysPt chunk_phys = static_cast<PhysPt>(GetAddress(seg, ofs + chunk_start));

            // Read chunk directly if memory is contiguous
            for(size_t i = 0; i < chunk_size; ++i) {
                uint8_t value = mem_readb(chunk_phys + i);
                buffer.push_back(value);
            }
        }
    }
    else {
        // Standard path with full error checking and events
        for(size_t i = 0; i < size; ++i) {
            PhysPt phys = static_cast<PhysPt>(GetAddress(seg, ofs + i));
            uint8_t value;

            if(use_fast_path) {
                // Fast read without error checking
                value = mem_readb(phys);
                buffer.push_back(value);
            }
            else {
                // Full error checking
                if(!mem_readb_checked(phys, &value)) {
                    // Fire memory read event after successful read
                    if(luaEngine.event_manager) {
                        LuaEngineEvents::MemoryEventData mem_event;
                        mem_event.segment = seg;
                        mem_event.offset = ofs + i;
                        mem_event.physical_addr = phys;
                        mem_event.data = value;
                        mem_event.access_size = 1;
                        mem_event.is_write = false;
                        luaEngine.event_manager->fireMemoryEvent(mem_event);
                    }
                    buffer.push_back(value);
                }
                else {
                    DEBUG_ShowMsg("ERROR: Failed to read byte at %04X:%08X", seg, ofs + i);
                    break;
                }
            }
        }
    }

    return buffer;
}
// --- ImGui Window Management ---
void RenderLuaConsoleWindow(LuaEngineUIState& state) {
#ifdef HAVE_IMGUI
    ImGui::Begin("Lua Console");

    std::lock_guard<std::mutex> lock(state.console_mutex);

    for(const auto& msg : state.console_messages) {
        ImGui::TextUnformatted(msg.c_str());
    }

    ImGui::SetScrollHereY(1.0f); // Auto-scroll to bottom

    ImGui::End();
#endif
}

struct LuaLogger {
    void info(const char* message) {
        if(luaEngine.logger) {
            luaEngine.log_info(std::string(message));
        }
        else {
            DEBUG_ShowMsg("%s", message);
        }
    }
    void debug(const char* message) {
        if(luaEngine.logger) {
            luaEngine.log_debug(std::string(message));
        }
        else {
            DEBUG_ShowMsg("[DEBUG] %s", message);
        }
    }
    void warn(const char* message) {
        if(luaEngine.logger) {
            luaEngine.log_warning(std::string(message));
        }
        else {
            DEBUG_ShowMsg("[WARN] %s", message);
        }
    }
    void error(const char* message) {
        if(luaEngine.logger) {
            luaEngine.log_error(std::string(message));
        }
        else {
            DEBUG_ShowMsg("[ERROR] %s", message);
        }
    }
};

LuaLogger* getLogger() {
    static LuaLogger instance;
    return &instance;
}

// PHASE 2 OPTIMIZATION: Lazy-loading CPU snapshot
// Only reads registers when actually accessed, avoiding unnecessary work
class FastCpuSnapshot {
private:
    // Lazy-load tracking bitmask
    mutable uint32_t valid_mask = 0;

    // Register cache (mutable to allow updating from const methods)
    mutable uint16_t _ax, _bx, _cx, _dx;
    mutable uint16_t _si, _di, _bp, _sp;
    mutable uint16_t _ip, _cs, _ds, _es, _ss, _fs, _gs;
    mutable uint32_t _flags;
    mutable uint8_t _al, _ah, _bl, _bh, _cl, _ch, _dl, _dh;

    // Helper bitmask constants for which registers are valid
    enum RegBit : uint32_t {
        BIT_AX=1, BIT_BX=2, BIT_CX=4, BIT_DX=8,
        BIT_SI=16, BIT_DI=32, BIT_BP=64, BIT_SP=128,
        BIT_IP=256, BIT_CS=512, BIT_DS=1024, BIT_ES=2048,
        BIT_SS=4096, BIT_FS=8192, BIT_GS=16384, BIT_FLAGS=32768
    };

public:
    // Default constructor explicitly initializes all members to zero
    FastCpuSnapshot() : valid_mask(0), _ax(0), _bx(0), _cx(0), _dx(0),
                        _si(0), _di(0), _bp(0), _sp(0), _ip(0),
                        _cs(0), _ds(0), _es(0), _ss(0), _fs(0), _gs(0),
                        _flags(0), _al(0), _ah(0), _bl(0), _bh(0),
                        _cl(0), _ch(0), _dl(0), _dh(0) {}

    // Lazy accessors - only read from emulator when needed
    inline uint16_t get_ax() const {
        if (!(valid_mask & BIT_AX)) {
            _ax = reg_ax;
            _al = reg_al;
            _ah = reg_ah;
            valid_mask |= BIT_AX;
        }
        return _ax;
    }

    inline uint16_t get_bx() const {
        if (!(valid_mask & BIT_BX)) {
            _bx = reg_bx;
            _bl = reg_bl;
            _bh = reg_bh;
            valid_mask |= BIT_BX;
        }
        return _bx;
    }

    inline uint16_t get_cx() const {
        if (!(valid_mask & BIT_CX)) {
            _cx = reg_cx;
            _cl = reg_cl;
            _ch = reg_ch;
            valid_mask |= BIT_CX;
        }
        return _cx;
    }

    inline uint16_t get_dx() const {
        if (!(valid_mask & BIT_DX)) {
            _dx = reg_dx;
            _dl = reg_dl;
            _dh = reg_dh;
            valid_mask |= BIT_DX;
        }
        return _dx;
    }

    inline uint16_t get_si() const {
        if (!(valid_mask & BIT_SI)) { _si = reg_si; valid_mask |= BIT_SI; }
        return _si;
    }

    inline uint16_t get_di() const {
        if (!(valid_mask & BIT_DI)) { _di = reg_di; valid_mask |= BIT_DI; }
        return _di;
    }

    inline uint16_t get_bp() const {
        if (!(valid_mask & BIT_BP)) { _bp = reg_bp; valid_mask |= BIT_BP; }
        return _bp;
    }

    inline uint16_t get_sp() const {
        if (!(valid_mask & BIT_SP)) { _sp = reg_sp; valid_mask |= BIT_SP; }
        return _sp;
    }

    // CS and IP are almost always needed for breakpoints, but still lazy
    inline uint16_t get_ip() const {
        if (!(valid_mask & BIT_IP)) { _ip = reg_ip; valid_mask |= BIT_IP; }
        return _ip;
    }

    inline uint16_t get_cs() const {
        if (!(valid_mask & BIT_CS)) { _cs = SegValue(::cs); valid_mask |= BIT_CS; }
        return _cs;
    }

    inline uint16_t get_ds() const {
        if (!(valid_mask & BIT_DS)) { _ds = SegValue(::ds); valid_mask |= BIT_DS; }
        return _ds;
    }

    inline uint16_t get_es() const {
        if (!(valid_mask & BIT_ES)) { _es = SegValue(::es); valid_mask |= BIT_ES; }
        return _es;
    }

    inline uint16_t get_ss() const {
        if (!(valid_mask & BIT_SS)) { _ss = SegValue(::ss); valid_mask |= BIT_SS; }
        return _ss;
    }

    inline uint16_t get_fs() const {
        if (!(valid_mask & BIT_FS)) { _fs = SegValue(::fs); valid_mask |= BIT_FS; }
        return _fs;
    }

    inline uint16_t get_gs() const {
        if (!(valid_mask & BIT_GS)) { _gs = SegValue(::gs); valid_mask |= BIT_GS; }
        return _gs;
    }

    inline uint32_t get_flags() const {
        if (!(valid_mask & BIT_FLAGS)) {
            _flags = static_cast<uint32_t>(reg_flags);
            valid_mask |= BIT_FLAGS;
        }
        return _flags;
    }

    // Byte register accessors (depend on word registers)
    inline uint8_t get_al() const { get_ax(); return _al; }
    inline uint8_t get_ah() const { get_ax(); return _ah; }
    inline uint8_t get_bl() const { get_bx(); return _bl; }
    inline uint8_t get_bh() const { get_bx(); return _bh; }
    inline uint8_t get_cl() const { get_cx(); return _cl; }
    inline uint8_t get_ch() const { get_cx(); return _ch; }
    inline uint8_t get_dl() const { get_dx(); return _dl; }
    inline uint8_t get_dh() const { get_dx(); return _dh; }

    // Compatibility accessors for old code (MSVC-specific properties)
#ifdef _MSC_VER
    __declspec(property(get=get_ax)) uint16_t ax;
    __declspec(property(get=get_bx)) uint16_t bx;
    __declspec(property(get=get_cx)) uint16_t cx;
    __declspec(property(get=get_dx)) uint16_t dx;
    __declspec(property(get=get_si)) uint16_t si;
    __declspec(property(get=get_di)) uint16_t di;
    __declspec(property(get=get_sp)) uint16_t sp;
    __declspec(property(get=get_bp)) uint16_t bp;
    __declspec(property(get=get_ip)) uint16_t ip;
    __declspec(property(get=get_cs)) uint16_t cs;
    __declspec(property(get=get_ds)) uint16_t ds;
    __declspec(property(get=get_es)) uint16_t es;
    __declspec(property(get=get_ss)) uint16_t ss;
    __declspec(property(get=get_fs)) uint16_t fs;
    __declspec(property(get=get_gs)) uint16_t gs;
    __declspec(property(get=get_flags)) uint32_t flags;
#else
    // GCC/Clang: Direct public accessors
    inline uint16_t ax() const { return get_ax(); }
    inline uint16_t bx() const { return get_bx(); }
    inline uint16_t cx() const { return get_cx(); }
    inline uint16_t dx() const { return get_dx(); }
    inline uint16_t si() const { return get_si(); }
    inline uint16_t di() const { return get_di(); }
    inline uint16_t sp() const { return get_sp(); }
    inline uint16_t bp() const { return get_bp(); }
    inline uint16_t ip() const { return get_ip(); }
    inline uint16_t cs() const { return get_cs(); }
    inline uint16_t ds() const { return get_ds(); }
    inline uint16_t es() const { return get_es(); }
    inline uint16_t ss() const { return get_ss(); }
    inline uint16_t fs() const { return get_fs(); }
    inline uint16_t gs() const { return get_gs(); }
    inline uint32_t flags() const { return get_flags(); }
#endif
};

// Legacy type alias for backward compatibility
using CpuSnapshot = FastCpuSnapshot;

void LuaEngine::LuaInstructionHook() {
    // If LRDB is active, let it manage execution via its own Lua hooks
    if (lrdb_active_) {
        if (lrdb_server_) {
            lrdb_server_->command_stream().poll();
        }
        return;
    }

    // PHASE 2 OPTIMIZATION: Ultra-fast gate using relaxed atomic (no memory barriers!)
    // This is the absolute cheapest check possible - just a register load on x86
    if (!g_fast_instruction_hook_active.load(std::memory_order_relaxed)) {
        return;
    }

    // Safety check with acquire semantics for proper synchronization
    if (!luaRunning.load(std::memory_order_acquire)) {
        return;
    }

    // If we are manually stepping over a breakpoint (RUN command), ignore this hook.
    // inhibit_int_breakpoint is set true during the single-step in debug.cpp.
    // skipFirstInstruction is checked just in case, though DEBUG_HeavyIsBreakpoint usually clears it first.
    if (inhibit_int_breakpoint || skipFirstInstruction) {
        return;
    }

    instruction_counter++;

    // PHASE 2 OPTIMIZATION: Don't create CPU snapshot yet!
    // Only create it if we actually have work to do (breakpoints or debugger active)

    // Quick check: Do we have any breakpoints, debugger, run-until target, or CDL?
    const bool has_breakpoints = (breakpoints.size() > 0);
    const bool has_debugger = (LuaEngineDebug::g_core_debugger != nullptr);
    const bool has_run_until = (run_until_target_.load(std::memory_order_relaxed) != 0);
    const bool has_cdl = PC98CDL::GetCDL().active();

    if (!has_breakpoints && !has_debugger && !has_run_until && !has_cdl) {
        // Nothing to do - avoid the cost of creating CpuSnapshot entirely!
        // Still give LRDB a chance to process commands when active
        if (lrdb_server_ && (instruction_counter & 0x0F) == 0) {
            lrdb_server_->command_stream().poll();
        }
        return;
    }

    // Now we know we need register values, so create the lazy snapshot
    // It will only read CS/IP initially (for breakpoint checking)
    const FastCpuSnapshot cpu;

    // Integrate with CoreDebugInterface
    if (has_debugger) {
        const uint16_t cs = cpu.get_cs();
        const uint16_t ip = cpu.get_ip();
        const uint32_t physical_address = (uint32_t(cs) << 4) + ip;
        LuaEngineDebug::g_core_debugger->onInstructionExecuted(physical_address);

        // PR4: Forward to trace logger using zero-alloc hot path
        if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
            if (auto* trace_logger = window_manager->getTraceLogger()) {
                if (trace_logger->isEnabled()) {
                    // Read first opcode byte for flow classification — no string disassembly
                    uint8_t opcode_byte = LuaEngineDebug::g_core_debugger->readMemoryByte(physical_address);
                    trace_logger->addRawEntry(physical_address, cs, ip, opcode_byte);
                }
            }
        }
    }

    // Check breakpoints (also accesses cpu.cs and cpu.ip)
    if (has_breakpoints) {
        const uint16_t cs = cpu.get_cs();
        const uint16_t ip = cpu.get_ip();
        // If checkLuaBreakpoint returns true, it means a breakpoint was hit AND it wants to stop
        if (checkLuaBreakpoint(cs, ip)) {
            DEBUG_EnableDebugger();
        }
    }

    // Reverse-engineering run-until target
    if (has_run_until) {
        const uint16_t cs = cpu.get_cs();
        const uint16_t ip = cpu.get_ip();
        const uint32_t here = (static_cast<uint32_t>(cs) << 4u) + ip;
        uint32_t target = run_until_target_.load(std::memory_order_relaxed);
        if (target != 0 && here == target) {
            sol::protected_function cb;
            {
                std::lock_guard<std::mutex> lock(run_until_mutex_);
                if (run_until_target_.load(std::memory_order_relaxed) == target) {
                    run_until_target_ = 0;
                    cb = run_until_callback_;
                    run_until_callback_ = sol::protected_function();
                }
            }
            if (cb.valid()) {
                auto result = cb(cs, ip);
                if (!result.valid()) {
                    sol::error err = result;
                    log_warning(std::string("run_until callback error: ") + err.what());
                }
                // A successful run-until callback keeps the game running so it can
                // be used for non-interactive tracing.
            } else {
                DEBUG_EnableDebugger();
            }
        }
    }

    // CDL instruction fetch capture.
    if (has_cdl) {
        const uint16_t cs = cpu.get_cs();
        const uint16_t ip = cpu.get_ip();
        PC98CDL::GetCDL().recordInsnFetch(cs, static_cast<uint32_t>(ip));
    }

    // Periodically service LRDB so VS Code breakpoints/steps are honored
    if (lrdb_server_ && (instruction_counter & 0x0F) == 0) {
        lrdb_server_->command_stream().poll();
    }

    // Advanced single-step debugging or instruction tracing could go here
    // but only when explicitly enabled
}

void LuaEngine::LuaFrameBoundary() {
    if(!luaRunning.load(std::memory_order_acquire)) return;

    // Service LRDB once per frame to sync breakpoints/commands from the debugger
    if (lrdb_server_) {
        lrdb_server_->command_stream().poll();
    }

    // This is a TRUE frame boundary (~60 FPS) called from VGA refresh
    auto frame_start = std::chrono::steady_clock::now();
    frame_counter++;

    // Fire frame start event using zero-allocation method
    if(event_manager) {
        event_manager->fireFrameEvent(LuaEngineEvents::EventType::FRAME_START);

        // OPTIMIZATION: Flush thread-local batches first (lock-free optimization)
        event_manager->flushThreadLocalBatch();

        // Flush any pending memory events at frame start for consistent timing
        event_manager->flushMemoryEventBatch();

        // PHASE 3 STEP 3: Execute all deferred callbacks in batch
        // This reduces lock contention by batching all Lua calls into one synchronized block
        event_manager->executeDeferredCallbacks();
    }

    // Time-based hot-reload throttling (check every 250ms)
    if(std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - last_hotreload_check).count() >= 250) {
        checkScriptHotReload();
        last_hotreload_check = frame_start;
    }

    // Update FPS (proper frame-based calculation)
#ifdef HAVE_IMGUI
    this->uiState.fps.store(ImGui::GetIO().Framerate, std::memory_order_relaxed);
#else
    this->uiState.fps.store(60.0f, std::memory_order_relaxed);
#endif

    if(luaRunning.load(std::memory_order_acquire) && frameAdvanceWaiting) {
        frameAdvanceWaiting = false;
    }
    frameBoundary = false;

    // Frame-based performance timing (every 4th frame)
    if((frame_counter & 3) == 0) {
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_time = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        recordFrameTime(frame_time);
    }


    // Fire frame end event using zero-allocation method
    if(event_manager) {
        event_manager->fireFrameEvent(LuaEngineEvents::EventType::FRAME_END);
    }

    // PR3-004: dispatch frame boundary to InstrumentationRouter
    // (exact watchpoints, deferred callbacks, step control)
#if C_LUA
    if(g_instrumentation) {
        g_instrumentation->onFrameBoundary(false);
    }
#endif

    LuaReHooks::OnFrame();

    last_true_frame_time = frame_start;
}

void LuaEngine::Shutdown() {
    log_info("Shutting down LuaEngine...");

    // Detach LRDB from the Lua state before tearing anything else down
    if (lrdb_server_) {
        try {
            lrdb_server_->reset();
            log_info("LRDB debugger detached from Lua state");
        }
        catch (const std::exception& e) {
            log_error(std::string("LRDB detach threw exception: ") + e.what());
        }
        catch (...) {
            log_error("LRDB detach threw unknown exception; continuing shutdown");
        }
        lrdb_server_.reset();
        lrdb_active_ = false;
        lrdb_port_ = 21110;
    }


    // 1. Shutdown worker threads for performance optimization
    shutdownWorkerThreads();

    // 2. Window thread management is now handled by DebuggerSession
    // (window thread functionality has been moved to DebuggerSession)
    // }

    // 3. Fire reverse-engineering exit hooks while the Lua state is still valid.
    LuaReHooks::OnExit();

    // 4. Clear all callbacks and breakpoints to release Lua function references
    if(event_manager) {
        event_manager->clearAllCallbacks();
    }
    breakpoints.clear();
    
    // Clear breakpoint metadata maps safely
    {
        std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
        breakpoint_names_.clear();
        breakpoint_conditions_.clear();
        breakpoint_actions_.clear();
    }

    // 3. Reset event manager before closing the Lua state to avoid Lua unrefs on tear-down
    event_manager.reset();

    // 4. Close the Lua state to free all Lua-related memory
    if(lua.lua_state()) {
        lua = sol::state(); // Properly close and reset the Lua state
    }

    // 5. Shutdown memory domains after Lua state is cleared to prevent use-after-free
    // This ensures Lua no longer holds references to domain objects before they're destroyed
    LuaEngineMemoryDomains::ShutdownMemoryDomains();
    
    // 6. Reset smart pointers and other state
    // input_recorder.reset(); // Removed - input recorder system disabled

    // Shutdown window system
    LuaEngineGUIWindows::WindowUtils::shutdownWindowSystem();
    game_templates.clear();
    active_game_hooks.clear();
    luaRunning.store(0, std::memory_order_release);
    frameBoundary = 0;
    frameAdvanceWaiting = 0;
    autostart_script_path.clear();
    last_script_modified = 0;
    perf_stats = {}; // Reset performance stats

    // Clear symbol manager global and destroy instance to avoid dangling pointer
    symbol_manager.reset();
    LuaEngineSymbols::g_symbol_manager = nullptr;

    log_info("LuaEngine shutdown complete.");
}

void LuaEngine::LUAENGINE_Init(Section* section) {
    // ALWAYS perform a clean shutdown first to handle re-initialization.
    Shutdown();
    log_info("Initializing LuaEngine...");

    // Check if section parameter is null
    if(!section) {
        log_info("No [lua] section found in config. LuaEngine remains disabled.");
        return;
    }

    // Cast to Section_prop to get properties
    Section_prop* lua_section = static_cast<Section_prop*>(section);

    // Additional safety check
    if(!lua_section) {
        log_info("ERROR: Failed to cast section to Section_prop! LuaEngine remains disabled.");
        return;
    }

    // Get configuration values from the section properties
    Section_prop* lua_prop_section = static_cast<Section_prop*>(lua_section);
    bool enabled = lua_prop_section->Get_bool("enabled");
    bool hooks = lua_prop_section->Get_bool("hooks");
    bool console = lua_prop_section->Get_bool("console");
    const char* autostart_script = lua_prop_section->Get_string("autostart");
    bool lrdb_enable = lua_prop_section->Get_bool("lrdb_enable");
    int lrdb_port = lua_prop_section->Get_int("lrdb_port");
    if(lrdb_port <= 0) {
        lrdb_port = 21110;
    }
    lrdb_port_ = lrdb_port;
    lrdb_active_ = false;
    char log_buffer[256];
    snprintf(log_buffer, sizeof(log_buffer), "LuaEngine configuration: enabled=%d, hooks=%d, console=%d, autostart='%s', lrdb_enable=%d, lrdb_port=%d",
        enabled, hooks, console, autostart_script ? autostart_script : "", lrdb_enable, lrdb_port_);
    log_info(log_buffer);

    if(!enabled) {
        log_info("LuaEngine disabled in configuration.");
        return;
    }

    // Initialize Lua state and open libraries
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::io, sol::lib::os, sol::lib::package, sol::lib::debug);
    log_info("Lua state initialized");

    // Register raw symbol bindings table (C API) before Sol APIs
    LuaEngineSymbols::RegisterSymbolBindings(lua.lua_state());

    // Load persisted symbolic breakpoints early so they resolve when symbols arrive
    // The singleton function will create the manager if it doesn't exist
    LuaEngineSymbols::GetSymbolicBreakpointManager()->loadFromFile("symbolic_breakpoints.dat");

    // Initialize event system (create first; initialize after WindowManager for TraceLogger)
    // PR1-006 + PR1-008: defer trace_logger retrieval + initialize() until after window_manager
    // is created (defect 8)
    event_manager = std::make_unique<LuaEngineEvents::EventManager>();

    // Enable performance mode by default to minimize overhead
    event_manager->setPerformanceMode(true);
    event_manager->setFrameSkipInterval(2);  // Skip every other frame for frame events

    // Configure memory event batching for optimal performance
    event_manager->setMemoryBatchSize(50);        // Smaller batches for lower latency
    event_manager->setMemoryFlushInterval(8);     // Flush every 8ms for responsive TAS
    event_manager->setMemoryThrottleThreshold(500); // Conservative throttling

    // Pre-allocate vectors for known capacity patterns
    uiState.console_messages.reserve(100);     // Console typically doesn't exceed 100 messages
    breakpoints.reserve(50);           // Most TAS scripts use fewer than 50 breakpoints  
    logpoints.reserve(20);             // Log points are used sparingly
    active_game_hooks.reserve(10);     // Few game hooks are typically active

    // Input recording system initialization removed - system disabled

    // Global pointers for input recorder removed - system disabled

    // Initialize autocomplete engine
    autocomplete_engine = std::make_unique<LuaAutocomplete::AutocompleteEngine>();

    // Initialize frame controller
    frame_controller = std::make_unique<LuaEngineFrameControl::FrameController>();
    LuaEngineFrameControl::g_frame_controller = frame_controller.get();

    // Initialize GUI window manager
    // GUI overlay manager removed - system disabled
    window_manager = std::make_unique<LuaEngineGUIWindows::WindowManager>();
    LuaEngineGUIWindows::g_window_manager = window_manager.get();

    // Now retrieve TraceLogger from the constructed WindowManager and finalize event manager init
    // PR1-006 + PR1-008: moved here so trace_logger_ is non-null when TraceLogger is active
    {
        LuaEngineTraceLogger::TraceLogger* trace_logger = nullptr;
        if (auto* wm = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
            trace_logger = wm->getTraceLogger();
        }
        event_manager->initialize(&lua, trace_logger);
    }

    // Initialize save state manager
    save_state_manager = std::make_unique<SaveStateManager::Manager>();
    SaveStateManager::g_save_state_manager = save_state_manager.get();

    // Initialize symbol manager
    symbol_manager = std::make_unique<LuaEngineSymbols::SymbolManager>();
    LuaEngineSymbols::g_symbol_manager = symbol_manager.get();

    // GUI overlay manager initialization removed - system disabled
    log_info("GUI overlay manager disabled");

    // Initialize memory domains BEFORE registering APIs that depend on them
    if(!LuaEngineMemoryDomains::InitializeMemoryDomains()) {
        log_info("Warning: Memory domains initialization failed");
    }

    registerMemoryAPI();
    registerCpuAPI();
    registerDebugAPI();
    registerBreakpointAPI();
    registerReHooksAPI();
    registerCDLAPI();
    registerReverseEngineeringAPI();
    registerSaveStateAPI();
    registerHotReloadAPI();
    registerEventAPI();
    registerEmuAPI();
    registerMemoryDomainsAPI();  // Now this can access initialized domains
    // registerInputRecorderAPI(); // Removed - input recorder system disabled
    // registerGUIOverlayAPI(); // Removed - GUI overlay system disabled
    registerFrameControlAPI();
    registerWindowSystemAPI();

    // Initialize and register game template system
    initializeGameTemplates();
    registerGameTemplateAPI();

    // Register console command system
    registerConsoleAPI();
    registerConsoleCommands();

    // Initialize performance monitoring
    initializePerformanceMonitoring();
    registerPerformanceAPI();

    // Initialize Sol2 performance optimizations
    initializeSol2PerformanceCache();
    cacheFrequentFunctions();

    // Populate autocomplete engine with registered API functions
    populateAutocompleteAPI();

    log_info("Lua APIs registered");

    // --- LRDB Integration ---
    if(lrdb_enable) {
        try {
            log_info("Initializing LRDB Lua debugger...");

            // Disable event performance mode so LRDB hooks aren't stripped
            if (event_manager) {
                event_manager->setPerformanceMode(false);
            }

            lrdb_server_ = std::make_unique<lrdb::server>(lrdb_port_);
            lrdb_server_->reset(lua.lua_state());
            // Block until a debugger attaches so breakpoints are known before scripts run
            lrdb_server_->command_stream().wait_for_connection();
            // Allow the client to send breakpoints before executing scripts
            log_info("Waiting for debugger initialization to send breakpoints...");
            for (int i = 0; i < 50; ++i) { // up to ~500ms
                lrdb_server_->command_stream().poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            lrdb_active_ = true;
            updateInstructionHookState(); // ensure instruction hook polling is enabled for LRDB

            snprintf(log_buffer, sizeof(log_buffer),
                "LRDB debugger active on port %d (attach VS Code LRDB extension)", lrdb_port_);
            log_info(log_buffer);
        }
        catch (const std::exception& e) {
            log_error(std::string("Failed to initialize LRDB debugger: ") + e.what());
            lrdb_server_.reset();
            lrdb_active_ = false;
        }
    }
    else {
        log_info("LRDB debugger disabled via configuration.");
    }

    luaRunning.store(hooks ? 1 : 0, std::memory_order_release);
    frameBoundary = 0;
    frameAdvanceWaiting = 0;
    {
        std::lock_guard<std::mutex> lock(uiStateMutex);
        uiState.fps.store(0.0f, std::memory_order_relaxed);
    }
    if(autostart_script && strlen(autostart_script) > 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Auto-starting Lua script: %s", autostart_script);
        log_info(log_buffer);
        autostart_script_path = autostart_script;
        last_script_modified = getFileModificationTime(autostart_script_path);
        LoadCode(autostart_script, nullptr);
    }
    if(console) {
        log_info("LuaEngine console enabled");
        // Console window functionality is now integrated with DebuggerSession
        // (createWindow functionality has been moved to DebuggerSession)
    }
    snprintf(log_buffer, sizeof(log_buffer), "LuaEngine initialization complete - hooks: %d, running: %d", hooks, luaRunning.load(std::memory_order_acquire));
    log_info(log_buffer);
}




int LuaEngine::LoadCode(const char* filename, const char* arg) {
    if(!filename || strlen(filename) == 0) {
        log_info("LoadCode: Invalid filename provided");
        return -1;
    }

    char log_buffer[512];
    char absolute_path[1024];
    
    // Get script search directories from configuration
    std::vector<std::string> script_dirs = {"lua-scripts", ".", "scripts"};
    Section* lua_section = control->GetSection("lua");
    if (lua_section) {
        Section_prop* lua_prop_section = static_cast<Section_prop*>(lua_section);
        const char* script_dirs_config = lua_prop_section->Get_string("script_dirs");
        if (script_dirs_config && strlen(script_dirs_config) > 0) {
            script_dirs.clear();
            std::string dirs_str(script_dirs_config);
            size_t pos = 0;
            while ((pos = dirs_str.find(',')) != std::string::npos) {
                std::string dir = dirs_str.substr(0, pos);
                // Trim whitespace
                dir.erase(0, dir.find_first_not_of(" \t"));
                dir.erase(dir.find_last_not_of(" \t") + 1);
                if (!dir.empty()) script_dirs.push_back(dir);
                dirs_str.erase(0, pos + 1);
            }
            // Add the last directory
            dirs_str.erase(0, dirs_str.find_first_not_of(" \t"));
            dirs_str.erase(dirs_str.find_last_not_of(" \t") + 1);
            if (!dirs_str.empty()) script_dirs.push_back(dirs_str);
        }
    }

    // Get current working directory for debugging
    char* cwd = getcwd(nullptr, 0);
    if(cwd) {
        snprintf(log_buffer, sizeof(log_buffer), "Current working directory: %s", cwd);
        log_info(log_buffer);
        free(cwd);
    }

    // Try to get absolute path
#ifdef _WIN32
    if(_fullpath(absolute_path, filename, sizeof(absolute_path))) {
        snprintf(log_buffer, sizeof(log_buffer), "Absolute path: %s", absolute_path);
        log_info(log_buffer);
    }
#else
    if(realpath(filename, absolute_path)) {
        snprintf(log_buffer, sizeof(log_buffer), "Absolute path: %s", absolute_path);
        log_info(log_buffer);
    }
#endif

    // Check if file exists, if not search in configured directories
    struct stat file_stat;
    std::string found_path;
    
    if(stat(filename, &file_stat) == 0) {
        found_path = filename;
        snprintf(log_buffer, sizeof(log_buffer), "File exists: %s (size: %ld bytes)", filename, file_stat.st_size);
        log_info(log_buffer);
    }
    else {
        snprintf(log_buffer, sizeof(log_buffer), "File NOT found at: %s", filename);
        log_info(log_buffer);
        
        // Search in configured script directories
        bool found = false;
        for (const auto& dir : script_dirs) {
            char test_path[512];
            snprintf(test_path, sizeof(test_path), "%s/%s", dir.c_str(), filename);
            if(stat(test_path, &file_stat) == 0) {
                found_path = test_path;
                found = true;
                snprintf(log_buffer, sizeof(log_buffer), "Found script: %s (size: %ld bytes)", test_path, file_stat.st_size);
                log_info(log_buffer);
                break;
            }
        }
        
        if (!found) {
            snprintf(log_buffer, sizeof(log_buffer), "Script not found in any search directory: %s", filename);
            log_error(log_buffer);
            return -1;
        }
    }

    // Prefer absolute path for debugger-friendly source matching
    std::string script_load_path = (absolute_path[0] != '\0') ? std::string(absolute_path) : found_path;

    snprintf(log_buffer, sizeof(log_buffer), "Loading Lua script: %s", script_load_path.c_str());
    log_info(log_buffer);
    try {
        auto result = lua.script_file(script_load_path);
        if(result.get_type() != sol::type::lua_nil) {
            sol::optional<int> return_code = result;
            if(return_code) {
                snprintf(log_buffer, sizeof(log_buffer), "Script %s completed with return code: %d", filename, return_code.value());
                log_info(log_buffer);
                return return_code.value();
            }
            else {
                snprintf(log_buffer, sizeof(log_buffer), "Script %s completed successfully", filename);
                log_info(log_buffer);
                return 0;
            }
        }
        else {
            sol::error err = result;
            snprintf(log_buffer, sizeof(log_buffer), "Script %s failed: %s", filename, err.what());
            log_info(log_buffer);
            return -2;
        }
    }
    catch(const sol::error& e) {
        snprintf(log_buffer, sizeof(log_buffer), "Lua error in %s: %s", filename, e.what());
        log_info(log_buffer);
        return -3;
    }
    catch(const std::exception& e) {
        snprintf(log_buffer, sizeof(log_buffer), "Exception loading %s: %s", filename, e.what());
        log_info(log_buffer);
        return -4;
    }
    return 0;
}

int LuaEngine::ExecuteCode(const std::string& code) {
    if(code.empty()) {
        log_info("ExecuteCode: Empty code provided");
        return -1;
    }
    log_info("Executing Lua code snippet");

    // Record script execution for performance monitoring
    recordScriptExecution();

    char log_buffer[512];
    try {
        auto result = lua.script(code);
        if(result.get_type() != sol::type::lua_nil) {
            sol::optional<int> return_code = result;
            if(return_code) {
                snprintf(log_buffer, sizeof(log_buffer), "Code executed with return code: %d", return_code.value());
                log_info(log_buffer);
                return return_code.value();
            }
            else {
                log_info("Code executed successfully");
                return 0;
            }
        }
        else {
            sol::error err = result;
            snprintf(log_buffer, sizeof(log_buffer), "Code execution failed: %s", err.what());
            log_info(log_buffer);
            return -2;
        }
    }
    catch(const sol::error& e) {
        snprintf(log_buffer, sizeof(log_buffer), "Lua error: %s", e.what());
        log_info(log_buffer);
        return -3;
    }
    catch(const std::exception& e) {
        snprintf(log_buffer, sizeof(log_buffer), "Exception: %s", e.what());
        log_info(log_buffer);
        return -4;
    }
    return 0;
}

// registerBreakpointAPI() is now implemented in lua_api_breakpoint.cpp to avoid duplication

void LuaEngine::addLuaBreakpoint(uint16_t cs, uint16_t ip, const std::string& name,
    const std::string& condition, const std::string& action, bool stop_on_hit) {
    // Thread-safe breakpoint modification
    std::lock_guard<std::mutex> lock(breakpoints_mutex_);

    char log_buffer[256];

    // PHASE 2 OPTIMIZATION: O(1) hash map lookup/insertion instead of O(n) vector iteration
    // Validate input strings to prevent buffer overflow
    size_t name_len = name.length();
    if(name_len > 200) name_len = 200;

    // O(1) hash map lookup
    uint32_t key = makeBreakpointKey(cs, ip);
    auto it = breakpoints.find(key);

    if(it != breakpoints.end()) {
        // Update existing breakpoint
        LuaBreakpoint& bp = it->second;
        bp.name = name;
        bp.condition = condition;
        bp.action = action;
        bp.enabled = true;
        bp.hit_count = 0;
        bp.ignore_count = 0;
        bp.stop_on_hit = stop_on_hit;
        snprintf(log_buffer, sizeof(log_buffer), "Updated existing breakpoint at %04X:%04X", cs, ip);
        log_info(log_buffer);

        // Update instruction hook state since we updated a breakpoint
        updateInstructionHookState();
        return;
    }

    // Create new breakpoint
    LuaBreakpoint new_bp;
    new_bp.cs = cs;
    new_bp.ip = ip;
    new_bp.name = name;
    new_bp.condition = condition;
    new_bp.action = action;
    new_bp.enabled = true;
    new_bp.hit_count = 0;
    new_bp.ignore_count = 0;
    new_bp.stop_on_hit = stop_on_hit;

    // O(1) hash map insertion
    breakpoints[key] = new_bp;
    snprintf(log_buffer, sizeof(log_buffer), "Added new breakpoint '%.*s' at %04X:%04X",
            (int)name_len, name.c_str(), cs, ip);
    log_info(log_buffer);

    // Update instruction hook state since we added a breakpoint
    updateInstructionHookState();
}

void LuaEngine::removeLuaBreakpoint(uint16_t cs, uint16_t ip) {
    // PHASE 2 OPTIMIZATION: O(1) hash map erase instead of O(n) vector removal
    // Thread-safe breakpoint removal
    std::lock_guard<std::mutex> lock(breakpoints_mutex_);

    char log_buffer[256];

    // O(1) hash map erase
    uint32_t key = makeBreakpointKey(cs, ip);
    size_t erased = breakpoints.erase(key);

    if(erased > 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Removed breakpoint at %04X:%04X", cs, ip);
        log_info(log_buffer);

        // Update instruction hook state since we removed a breakpoint
        updateInstructionHookState();
    }
    else {
        snprintf(log_buffer, sizeof(log_buffer), "No breakpoint found at %04X:%04X", cs, ip);
        log_info(log_buffer);
    }
}

void LuaEngine::enableLuaBreakpoint(uint16_t cs, uint16_t ip, bool enabled) {
    // PHASE 2 OPTIMIZATION: O(1) hash map lookup instead of O(n) vector iteration
    // Thread-safe breakpoint enable/disable
    std::lock_guard<std::mutex> lock(breakpoints_mutex_);

    char log_buffer[256];

    // O(1) hash map lookup
    uint32_t key = makeBreakpointKey(cs, ip);
    auto it = breakpoints.find(key);

    if(it != breakpoints.end()) {
        LuaBreakpoint& bp = it->second;
        bp.enabled = enabled;

        // Safe string formatting with length validation
        size_t name_len = bp.name.length();
        if(name_len > 150) name_len = 150;
        snprintf(log_buffer, sizeof(log_buffer), "%s breakpoint '%.*s' at %04X:%04X",
            enabled ? "Enabled" : "Disabled", (int)name_len, bp.name.c_str(), cs, ip);
        log_info(log_buffer);

        // Update instruction hook state since we changed breakpoint enable state
        updateInstructionHookState();
        return;
    }

    snprintf(log_buffer, sizeof(log_buffer), "No breakpoint found at %04X:%04X", cs, ip);
    log_info(log_buffer);
}

bool LuaEngine::checkLuaBreakpoint(uint16_t cs, uint16_t ip) {
    // PHASE 3 STEP 2 OPTIMIZATION: Minimize lock scope to reduce contention
    // Strategy: Separate breakpoint lookup from Lua execution

    // Step 1: Quick lookup and data extraction (hold breakpoints_mutex_ briefly)
    std::string bp_name;
    std::string bp_condition;
    std::string bp_action;
    uint64_t bp_hit_count;
    bool should_execute = false;
    bool should_stop = true; // Default to true for backward compatibility

    {
        std::lock_guard<std::mutex> breakpoint_lock(breakpoints_mutex_);

        // Fast path check - empty map
        if (breakpoints.empty()) {
            return false;
        }

        // O(1) hash map lookup
        uint32_t key = makeBreakpointKey(cs, ip);
        auto it = breakpoints.find(key);

        if (it == breakpoints.end()) {
            // No breakpoint at this address
            return false;
        }

        // Found a breakpoint - now check if it's enabled
        LuaBreakpoint& bp = it->second;
        if (!bp.enabled) {
            return false;
        }

        // Update hit count and check ignore count
        bp.hit_count++;

        if(bp.ignore_count > 0) {
            bp.ignore_count--;
            return false;
        }

        // Copy data we need for Lua execution (to avoid holding lock during script execution)
        bp_name = bp.name;
        bp_condition = bp.condition;
        bp_action = bp.action;
        bp_hit_count = bp.hit_count;
        should_execute = true;
        should_stop = bp.stop_on_hit; // Capture stop preference
    } // Release breakpoints_mutex_ HERE - no longer holding it during Lua execution!

    // Step 2: Execute Lua code (hold lua_state_mutex_ only during execution)
    if (!should_execute) {
        return false;
    }

    std::lock_guard<std::mutex> lua_lock(lua_state_mutex_);
    char log_buffer[512];

    if(!bp_condition.empty()) {
        try {
            // Validate condition string length to prevent buffer overflow
            if(bp_condition.length() > 1000) {
                snprintf(log_buffer, sizeof(log_buffer), "Breakpoint condition too long (>1000 chars), skipping");
                log_info(log_buffer);
                return false;
            }
            auto result = lua.script("return " + bp_condition);
            sol::optional<bool> condition_result = result;
            if(!condition_result || !condition_result.value()) {
                return false;
            }
        }
        catch(const std::exception& e) {
            // Safe string formatting with length validation
            const char* error_msg = e.what();
            size_t error_len = strlen(error_msg);
            if(error_len > 400) error_len = 400; // Truncate if too long
            snprintf(log_buffer, sizeof(log_buffer), "Breakpoint condition exception: %.*s",
                    (int)error_len, error_msg);
            log_info(log_buffer);
            return false;
        }
    }

    if(!bp_action.empty()) {
        try {
            // Validate action string length
            if(bp_action.length() > 1000) {
                snprintf(log_buffer, sizeof(log_buffer), "Breakpoint action too long (>1000 chars), skipping");
                log_info(log_buffer);
                return false;
            }
            auto result = lua.script(bp_action);

            // Check if the action returned a boolean to override stop behavior
            if (result.valid() && result.get_type() == sol::type::boolean) {
                should_stop = result.get<bool>();
            }
            else if(result.get_type() == sol::type::lua_nil) {
                sol::error err = result;
                const char* error_msg = err.what();
                size_t error_len = strlen(error_msg);
                if(error_len > 400) error_len = 400;
                snprintf(log_buffer, sizeof(log_buffer), "Breakpoint action error: %.*s",
                        (int)error_len, error_msg);
                log_info(log_buffer);
            }
        }
        catch(const std::exception& e) {
            const char* error_msg = e.what();
            size_t error_len = strlen(error_msg);
            if(error_len > 400) error_len = 400;
            snprintf(log_buffer, sizeof(log_buffer), "Breakpoint action exception: %.*s",
                    (int)error_len, error_msg);
            log_info(log_buffer);
            // On error, safer to stop so user sees the issue
            should_stop = true;
        }
    }

    // Safe string formatting for breakpoint name
    size_t name_len = bp_name.length();
    if(name_len > 200) name_len = 200; // Truncate if too long
    snprintf(log_buffer, sizeof(log_buffer), "Breakpoint '%.*s' hit at %04X:%04X (hit #%llu)",
        (int)name_len, bp_name.c_str(), cs, ip, (unsigned long long)bp_hit_count);
    log_info(log_buffer);

    // Record breakpoint hit for performance monitoring
    recordBreakpointHit();

    try {
        sol::optional<bool> auto_save = lua["_auto_save_on_breakpoint"];
        if(auto_save && auto_save.value()) {
            log_info("Auto-saving state due to breakpoint hit");
            SaveGameState(true);
        }
    }
    catch(...) {}

    return should_stop; // Return the stop preference instead of always true
}

void LuaEngine::enableInstructionHooks(bool enable) {
    // Only log if the state actually changes to prevent spam
    if (needs_instruction_hooks.load(std::memory_order_acquire) != enable) {
        needs_instruction_hooks.store(enable, std::memory_order_release);

        // PHASE 2 OPTIMIZATION: Sync the ultra-fast relaxed flag for the hot path
        // This allows LuaInstructionHook to check with minimal overhead
        g_fast_instruction_hook_active.store(enable, std::memory_order_relaxed);

        char log_buffer[256];
        snprintf(log_buffer, sizeof(log_buffer), "Instruction hooks %s", enable ? "enabled" : "disabled");
        log_info(log_buffer);
    }
}

void LuaEngine::updateInstructionHookState() {
    // Enable instruction hooks if any debugging features are active
    bool should_enable = !breakpoints.empty() ||
        (event_manager && event_manager->getTotalCallbackCount() > 0) ||
        lrdb_active_;

    if(should_enable != needs_instruction_hooks.load(std::memory_order_acquire)) {
        enableInstructionHooks(should_enable);
    }
}

void LuaEngine::registerSaveStateAPI() {
    auto savestate_table = lua.create_table();
    char log_buffer[256];
    savestate_table["save"] = [this, &log_buffer](sol::optional<int> slot) {
        if(slot) {
            int current_slot = GetGameState();
            SetGameState(slot.value()); SaveGameState(true); SetGameState(current_slot);
            snprintf(log_buffer, sizeof(log_buffer), "Saved state to slot %d", slot.value());
            log_info(log_buffer);
        }
        else {
            SaveGameState(true);
            snprintf(log_buffer, sizeof(log_buffer), "Saved state to current slot %llu",
                static_cast<unsigned long long>(GetGameState()));
            log_info(log_buffer);
        }
        };
    savestate_table["load"] = [this, &log_buffer](sol::optional<int> slot) {
        if(slot) {
            int current_slot = GetGameState();
            SetGameState(slot.value()); LoadGameState(true); SetGameState(current_slot);
            snprintf(log_buffer, sizeof(log_buffer), "Loaded state from slot %d", slot.value());
            log_info(log_buffer);
        }
        else {
            LoadGameState(true);
            snprintf(log_buffer, sizeof(log_buffer), "Loaded state from current slot %llu",
                static_cast<unsigned long long>(GetGameState()));
            log_info(log_buffer);
        }
        };
    savestate_table["get_current_slot"] = []() -> int { return static_cast<int>(GetGameState()); };
    savestate_table["set_current_slot"] = [this, &log_buffer](int slot) {
        SetGameState(slot);
        snprintf(log_buffer, sizeof(log_buffer), "Set current savestate slot to %d", slot);
        log_info(log_buffer);
        };
    savestate_table["quick_save"] = [this, &log_buffer]() {
        SaveGameState(true);
        snprintf(log_buffer, sizeof(log_buffer), "Quick save to slot %llu",
            static_cast<unsigned long long>(GetGameState()));
        log_info(log_buffer);
        };
    savestate_table["quick_load"] = [this, &log_buffer]() {
        LoadGameState(true);
        snprintf(log_buffer, sizeof(log_buffer), "Quick load from slot %llu",
            static_cast<unsigned long long>(GetGameState()));
        log_info(log_buffer);
        };
    savestate_table["auto_save_on_breakpoint"] = [this, &log_buffer](bool enabled) {
        lua["_auto_save_on_breakpoint"] = enabled;
        snprintf(log_buffer, sizeof(log_buffer), "%s auto-save on breakpoint", enabled ? "Enabled" : "Disabled");
        log_info(log_buffer);
        };

    // Rich named savestate support via SaveStateManager
    savestate_table["save_full"] = [this](const std::string& name, sol::optional<std::string> comment) {
        if (!save_state_manager) return false;
        save_state_manager->Initialize();
        SaveStateManager::SaveStateResult result = save_state_manager->SaveState(name, comment.value_or(""));
        if (result == SaveStateManager::SAVESTATE_SUCCESS) {
            log_info("Saved full state to " + name);
            return true;
        }
        log_error("Failed to save full state to " + name);
        return false;
        };

    savestate_table["load_full"] = [this](const std::string& name) {
        if (!save_state_manager) return false;
        save_state_manager->Initialize();
        SaveStateManager::SaveStateResult result = save_state_manager->LoadState(name);
        if (result == SaveStateManager::SAVESTATE_SUCCESS) {
            log_info("Loaded full state from " + name);
            return true;
        }
        log_error("Failed to load full state from " + name);
        return false;
        };

    lua["savestate"] = savestate_table;
}

void LuaEngine::registerHotReloadAPI() {
    auto hotreload_table = lua.create_table();
    char log_buffer[512];
    hotreload_table["enable"] = [this, &log_buffer](const std::string& filename) {
        autostart_script_path = filename;
        last_script_modified = getFileModificationTime(filename);
        snprintf(log_buffer, sizeof(log_buffer), "Enabled hot-reload for script: %s", filename.c_str());
        log_info(log_buffer);
        };
    hotreload_table["disable"] = [this]() {
        autostart_script_path.clear();
        last_script_modified = 0;
        log_info("Disabled hot-reload");
        };
    hotreload_table["check"] = [this]() { checkScriptHotReload(); };
    hotreload_table["get_script"] = [this]() -> std::string { return autostart_script_path; };
    hotreload_table["reload"] = [this, &log_buffer]() {
        if(!autostart_script_path.empty()) {
            snprintf(log_buffer, sizeof(log_buffer), "Manually reloading script: %s", autostart_script_path.c_str());
            log_info(log_buffer);
            LoadCode(autostart_script_path.c_str(), nullptr);
            last_script_modified = getFileModificationTime(autostart_script_path);
        }
        else {
            log_info("No script configured for hot-reload");
        }
        };
    lua["hotreload"] = hotreload_table;
}

time_t LuaEngine::getFileModificationTime(const std::string& filename) {
    struct stat file_stat;
    if(stat(filename.c_str(), &file_stat) == 0) {
        return file_stat.st_mtime;
    }
    return 0;
}

void LuaEngine::checkScriptHotReload() {
    if(autostart_script_path.empty()) return;
    char log_buffer[512];
    time_t current_modified = getFileModificationTime(autostart_script_path);
    if(current_modified == 0) return;
    if(current_modified > last_script_modified) {
        snprintf(log_buffer, sizeof(log_buffer), "Script file modified, hot-reloading: %s", autostart_script_path.c_str());
        log_info(log_buffer);
        if(LoadCode(autostart_script_path.c_str(), nullptr) == 0) {
            last_script_modified = current_modified;
            log_info("Script hot-reloaded successfully");
        }
        else {
            log_info("Script hot-reload failed, keeping old version");
        }
    }
}

// Initialize predefined game templates
void LuaEngine::initializeGameTemplates() {
    log_info("Initializing game templates...");

    // Simplified templates for compilation
    game_templates["generic_dos"] = "debug.log('Generic DOS template loaded')";
    game_templates["platformer"] = "debug.log('Platformer template loaded')";
    game_templates["rpg"] = "debug.log('RPG template loaded')";
    game_templates["action"] = "debug.log('Action template loaded')";
    game_templates["strategy"] = "debug.log('Strategy template loaded')";

    log_info("Game templates initialized");
}

void LuaEngine::registerGameTemplateAPI() {
    auto template_table = lua.create_table();

    // template.list() -> table of available templates
    template_table["list"] = [this]() -> sol::table {
        auto result = lua.create_table();
        int index = 1;

        for(const auto& pair : game_templates) {
            result[index++] = pair.first;
        }

        return result;
        };

    // template.load(name, params) -> string
    template_table["load"] = [this](const std::string& template_name, sol::table params) -> std::string {
        if(game_templates.find(template_name) == game_templates.end()) {
            log_info("Template not found: " + template_name);
            return "";
        }

        return generateGameScript(template_name, {});
        };

    lua["template"] = template_table;
    log_info("Game template API registered");
}

std::string LuaEngine::generateGameScript(const std::string& template_name, const std::map<std::string, std::string>& params) {
    auto it = game_templates.find(template_name);
    if(it == game_templates.end()) {
        log_info("Template not found: " + template_name);
        return "";
    }

    std::string script = it->second;

    // Simple implementation without parameter substitution for now
    return script;
}

void LuaEngine::loadGameTemplate(const std::string& game_name) {
    // Simplified implementation
    log_info("Loading game template: " + game_name);
}

std::string LuaEngine::executeLuaConsoleCommand(const std::string& command) {
    if(command.empty()) {
        return "Empty command";
    }

    log_info("Console executing: " + command);

    // Normalize for simple debugger command detection
    std::string upper_cmd = command;
    std::transform(upper_cmd.begin(), upper_cmd.end(), upper_cmd.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });

    if(upper_cmd == "LRDB") {
        if(!lrdb_active_) {
            return "LRDB debugger is not active";
        }
        return "LRDB active on port " + std::to_string(lrdb_port_);
    }

    // First try legacy debugger commands so they don't trip Lua parser on non-Lua syntax
#if C_DEBUG
    // ponytail: ParseCommand takes char* not const char* — make a mutable copy
    std::string cmd_buf = command;
    if (ParseCommand(&cmd_buf[0])) {
        return "Debugger command executed";
    }
#else
    // In non-debug builds, short-circuit known debugger-only commands to avoid Lua errors
    if (upper_cmd == "HELP" || upper_cmd == "?") {
        return "Debugger commands are not available in this build";
    }
#endif

    try {
        // Try to execute the command and capture any return value
        auto result = lua.script(command);

        if(result.get_type() != sol::type::lua_nil) {
            return "Command executed successfully";
        }
        else {
            return "Command executed";
        }
    }
    catch(const std::exception& e) {
        // If Lua execution failed (likely due to legacy debugger syntax), try the old debugger command parser.
#if C_DEBUG
        if (ParseCommand(&cmd_buf[0])) {
            return "Debugger command executed";
        }
#endif
        return "Error: " + std::string(e.what());
    }
}

void LuaEngine::registerConsoleCommands() {
    // This would integrate with DOSBox-X's command system
    log_info("Console commands registered");
}

// === Performance Monitoring Implementation ===

void LuaEngine::initializePerformanceMonitoring() {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    perf_stats.start_time = std::chrono::steady_clock::now();
    perf_stats.total_frame_calls = 0;
    perf_stats.total_lua_time_us = 0;
    perf_stats.breakpoint_hits = 0;
    perf_stats.script_executions = 0;
    perf_stats.memory_operations = 0;
    perf_stats.max_frame_time_us = 0;
    perf_stats.min_frame_time_us = UINT64_MAX;

    log_info("Performance monitoring initialized");
}

void LuaEngine::recordFrameTime(uint64_t time_us) {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    perf_stats.total_frame_calls++;
    perf_stats.total_lua_time_us += time_us;

    if(time_us > perf_stats.max_frame_time_us) {
        perf_stats.max_frame_time_us = time_us;
    }

    if(time_us < perf_stats.min_frame_time_us) {
        perf_stats.min_frame_time_us = time_us;
    }
}

void LuaEngine::recordBreakpointHit() {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    perf_stats.breakpoint_hits++;
}

void LuaEngine::recordScriptExecution() {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    perf_stats.script_executions++;
}

void LuaEngine::recordMemoryOperation() {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    perf_stats.memory_operations++;
}

std::string LuaEngine::getPerformanceReport() {
    std::lock_guard<std::mutex> perf_lock(perf_mutex_);
    auto current_time = std::chrono::steady_clock::now();
    auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - perf_stats.start_time).count();

    std::string report = "=== DOSBox-X LuaEngine Performance Report ===\\n";
    report += "Total Runtime: " + std::to_string(total_time_ms) + " ms\\n";
    report += "Frame Calls: " + std::to_string(perf_stats.total_frame_calls) + "\\n";

    return report;
}

void LuaEngine::registerPerformanceAPI() {
    auto performance_table = lua.create_table();

    // Get real-time performance statistics
    performance_table["get_stats"] = [this]() -> sol::table {
        auto stats_table = lua.create_table();
        {
            std::lock_guard<std::mutex> perf_lock(perf_mutex_);
            stats_table["frame_calls"] = perf_stats.total_frame_calls;
            stats_table["total_lua_time_us"] = perf_stats.total_lua_time_us;
            stats_table["breakpoint_hits"] = perf_stats.breakpoint_hits;
            stats_table["script_executions"] = perf_stats.script_executions;
            stats_table["memory_operations"] = perf_stats.memory_operations;
        }
        return stats_table;
        };

    // Performance mode controls
    performance_table["enable_fast_memory"] = [this](bool enable) {
        sol2_cache.fast_memory_access = enable;
        log_info(enable ? "Fast memory access enabled" : "Fast memory access disabled");
        };

    performance_table["enable_minimal_error_checking"] = [this](bool enable) {
        sol2_cache.minimal_error_checking = enable;
        log_info(enable ? "Minimal error checking enabled" : "Minimal error checking disabled");
        };

    performance_table["is_fast_memory_enabled"] = [this]() -> bool {
        return sol2_cache.fast_memory_access;
        };

    performance_table["is_minimal_error_checking_enabled"] = [this]() -> bool {
        return sol2_cache.minimal_error_checking;
        };

    // Bulk memory operations for high performance
    performance_table["read_bytes_fast"] = [this](uint32_t address, size_t size) -> std::vector<uint8_t> {
        // Force fast path by temporarily enabling performance mode
        bool old_fast = sol2_cache.fast_memory_access;
        bool old_minimal = sol2_cache.minimal_error_checking;
        sol2_cache.fast_memory_access = true;
        sol2_cache.minimal_error_checking = true;

        auto result = GetBytes(0, address, size);  // Use linear address

        // Restore original settings
        sol2_cache.fast_memory_access = old_fast;
        sol2_cache.minimal_error_checking = old_minimal;

        return result;
        };

    performance_table["write_bytes_fast"] = [this](uint32_t address, const std::vector<uint8_t>& data) -> bool {
        // Force fast path by temporarily enabling performance mode
        bool old_fast = sol2_cache.fast_memory_access;
        bool old_minimal = sol2_cache.minimal_error_checking;
        sol2_cache.fast_memory_access = true;
        sol2_cache.minimal_error_checking = true;

        bool result = WriteBytes(0, address, data);  // Use linear address

        // Restore original settings
        sol2_cache.fast_memory_access = old_fast;
        sol2_cache.minimal_error_checking = old_minimal;

        return result;
        };

    lua["performance"] = performance_table;
    log_info("Performance monitoring and control API registered");
}

void LuaEngine::registerEventAPI() {
    if(!event_manager) {
        log_info("Event manager not initialized, skipping event API registration");
        return;
    }

    auto event_table = lua.create_table();

    // Register frame event callbacks
    event_table["onframestart"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("frame_start_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::FRAME_START, callback, callback_name);
        };

    event_table["onframeend"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("frame_end_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::FRAME_END, callback, callback_name);
        };

    // Register memory event callbacks
    event_table["onmemoryread"] = [this](uint32_t start_addr, uint32_t end_addr, sol::function callback) -> uint32_t {
        return event_manager->registerMemoryCallback(start_addr, end_addr, callback, true, false);
        };

    event_table["onmemorywrite"] = [this](uint32_t start_addr, uint32_t end_addr, sol::function callback) -> uint32_t {
        return event_manager->registerMemoryCallback(start_addr, end_addr, callback, false, true);
        };

    event_table["onmemoryaccess"] = [this](uint32_t start_addr, uint32_t end_addr, sol::function callback) -> uint32_t {
        return event_manager->registerMemoryCallback(start_addr, end_addr, callback, true, true);
        };

    // Register DOS interrupt callbacks
    event_table["oninterrupt"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("interrupt_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::DOS_INTERRUPT, callback, callback_name);
        };

    // Register other system event callbacks
    event_table["onkeyboard"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("keyboard_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::KEYBOARD_INPUT, callback, callback_name);
        };

    event_table["onmouse"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("mouse_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::MOUSE_INPUT, callback, callback_name);
        };

    event_table["onsavestate"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("savestate_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::SAVESTATE_SAVE, callback, callback_name);
        };

    event_table["onloadstate"] = [this](sol::function callback, sol::optional<std::string> name) -> uint32_t {
        std::string callback_name = name.value_or("loadstate_callback");
        return event_manager->registerCallback(LuaEngineEvents::EventType::SAVESTATE_LOAD, callback, callback_name);
        };

    // Generic registration compatible with scripts expecting event.register(name, cb [, start, end])
    event_table["register"] = [this](const std::string& event_name, sol::function callback, sol::variadic_args va) -> uint32_t {
        // Normalize the event name (lowercase, strip spaces/underscores/hyphens)
        std::string norm;
        norm.reserve(event_name.size());
        for (char c : event_name) {
            if (c == ' ' || c == '_' || c == '-') continue;
            norm.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        auto register_simple = [&](LuaEngineEvents::EventType type, const std::string& label) -> uint32_t {
            return event_manager->registerCallback(type, callback, label);
        };

        if (norm == "framestart") return register_simple(LuaEngineEvents::EventType::FRAME_START, "frame_start_callback");
        if (norm == "frameend" || norm == "frame") return register_simple(LuaEngineEvents::EventType::FRAME_END, "frame_end_callback");
        if (norm == "interrupt") return register_simple(LuaEngineEvents::EventType::DOS_INTERRUPT, "interrupt_callback");
        if (norm == "keyboard") return register_simple(LuaEngineEvents::EventType::KEYBOARD_INPUT, "keyboard_callback");
        if (norm == "mouse") return register_simple(LuaEngineEvents::EventType::MOUSE_INPUT, "mouse_callback");
        if (norm == "savestate" || norm == "savestatesave") return register_simple(LuaEngineEvents::EventType::SAVESTATE_SAVE, "savestate_callback");
        if (norm == "loadstate" || norm == "savestateload") return register_simple(LuaEngineEvents::EventType::SAVESTATE_LOAD, "loadstate_callback");
        if (norm == "cpustep") return register_simple(LuaEngineEvents::EventType::CPU_STEP, "cpu_step_callback");
        if (norm == "breakpointhit") return register_simple(LuaEngineEvents::EventType::BREAKPOINT_HIT, "breakpoint_callback");

        // Memory events: allow optional start/end ranges
        auto parse_range = [&](uint32_t& start, uint32_t& end) {
            start = 0;
            end = 0xFFFFF; // default to first 1MB
            if (va.size() >= 2) {
                start = va.get<uint32_t>(0);
                end = va.get<uint32_t>(1);
                if (end < start) std::swap(start, end);
            }
        };

        if (norm == "memoryread" || norm == "memory_read") {
            uint32_t start, end;
            parse_range(start, end);
            return event_manager->registerMemoryCallback(start, end, callback, true, false);
        }

        if (norm == "memorywrite" || norm == "memory_write") {
            uint32_t start, end;
            parse_range(start, end);
            return event_manager->registerMemoryCallback(start, end, callback, false, true);
        }

        if (norm == "memoryaccess" || norm == "memory") {
            uint32_t start, end;
            parse_range(start, end);
            return event_manager->registerMemoryCallback(start, end, callback, true, true);
        }

        // Unknown event
        return 0;
        };

    // Callback management functions
    event_table["unregister"] = [this](uint32_t callback_id) -> bool {
        return event_manager->unregisterCallback(callback_id);
        };

    event_table["enable"] = [this](uint32_t callback_id, bool enabled) -> bool {
        return event_manager->enableCallback(callback_id, enabled);
        };

    event_table["clear"] = [this](sol::optional<std::string> event_type) {
        if(event_type) {
            // Parse event type string and clear specific type
            std::string type_str = event_type.value();
            if(type_str == "frame") {
                event_manager->clearCallbacks(LuaEngineEvents::EventType::FRAME_START);
                event_manager->clearCallbacks(LuaEngineEvents::EventType::FRAME_END);
            }
            else if(type_str == "memory") {
                event_manager->clearCallbacks(LuaEngineEvents::EventType::MEMORY_READ);
                event_manager->clearCallbacks(LuaEngineEvents::EventType::MEMORY_WRITE);
            }
            else if(type_str == "interrupt") {
                event_manager->clearCallbacks(LuaEngineEvents::EventType::DOS_INTERRUPT);
            }
        }
        else {
            event_manager->clearAllCallbacks();
        }
        };

    // Statistics functions
    event_table["count"] = [this]() -> uint32_t {
        return static_cast<uint32_t>(event_manager->getTotalCallbackCount());
        };

    // Performance control functions
    event_table["setperformancemode"] = [this](bool enabled) {
        event_manager->setPerformanceMode(enabled);
        };

    event_table["setframeskip"] = [this](uint32_t interval) {
        event_manager->setFrameSkipInterval(interval);
        };

    event_table["resetcache"] = [this]() {
        event_manager->resetCachedEventTable();
        };

    // Memory performance controls
    event_table["setmemorybatchsize"] = [this](uint32_t max_size) {
        event_manager->setMemoryBatchSize(max_size);
        };

    event_table["setmemoryflushinterval"] = [this](uint32_t ms) {
        event_manager->setMemoryFlushInterval(ms);
        };

    event_table["setmemorythrottle"] = [this](uint32_t threshold) {
        event_manager->setMemoryThrottleThreshold(threshold);
        };

    event_table["flushmemoryevents"] = [this]() {
        event_manager->flushMemoryEventBatch();
        };

    lua["event"] = event_table;
    log_info("Event API registered with advanced callback system");
}

void LuaEngine::onDOSInterrupt(uint8_t interrupt_num, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx,
    uint16_t cs, uint16_t ds, uint16_t es, uint16_t ss) {
    if(!event_manager || !luaRunning.load(std::memory_order_acquire)) return;

    // Only fire for commonly monitored interrupts to avoid spam
    if(interrupt_num == 0x21 || interrupt_num == 0x10 || interrupt_num == 0x13 ||
        interrupt_num == 0x16 || interrupt_num == 0x1A) {

        LuaEngineEvents::InterruptEventData interrupt_data;
        interrupt_data.interrupt_num = interrupt_num;
        interrupt_data.ax = ax;
        interrupt_data.bx = bx;
        interrupt_data.cx = cx;
        interrupt_data.dx = dx;
        interrupt_data.cs = cs;
        interrupt_data.ds = ds;
        interrupt_data.es = es;
        interrupt_data.ss = ss;

        event_manager->fireInterruptEvent(interrupt_data);
    }
}

void LuaEngine::registerEmuAPI() {
    auto emu_table = lua.create_table();

    // Frame advance - core BizHawk functionality
    emu_table["frameadvance"] = [this]() {
        frameAdvanceWaiting = true;
        return lua.script("coroutine.yield()");
        };

    // Alternative name for frameadvance
    emu_table["yield"] = [this]() {
        frameAdvanceWaiting = true;
        return lua.script("coroutine.yield()");
        };

    // Emulation control functions
    emu_table["pause"] = [this]() {
        // This would need integration with DOSBox-X's pause mechanism
        log_info("Emulation pause requested via Lua");
        };

    emu_table["unpause"] = [this]() {
        // This would need integration with DOSBox-X's pause mechanism
        log_info("Emulation unpause requested via Lua");
        };

    // Speed control
    emu_table["speedmode"] = [this](sol::optional<std::string> mode) -> std::string {
        if(mode) {
            std::string mode_str = mode.value();
            if(mode_str == "normal") {
                speedmode = SPEED_NORMAL;
            }
            else if(mode_str == "nothrottle") {
                speedmode = SPEED_NOTHROTTLE;
            }
            else if(mode_str == "turbo") {
                speedmode = SPEED_TURBO;
            }
            else if(mode_str == "maximum") {
                speedmode = SPEED_MAXIMUM;
            }
        }

        // Return current speed mode
        switch(speedmode) {
        case SPEED_NORMAL: return "normal";
        case SPEED_NOTHROTTLE: return "nothrottle";
        case SPEED_TURBO: return "turbo";
        case SPEED_MAXIMUM: return "maximum";
        default: return "unknown";
        }
        };

    // Emulation information
    emu_table["framecount"] = [this]() -> uint64_t {
        return perf_stats.total_frame_calls;
        };

    emu_table["totaltime"] = [this]() -> double {
        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - perf_stats.start_time);
        return duration.count() / 1000.0; // Return seconds as double
        };

    // System information
    emu_table["getsystemid"] = []() -> std::string {
        return "DOS"; // Could be enhanced to detect PC-98, etc.
        };

    emu_table["getdisplaytype"] = []() -> std::string {
        return "VGA"; // Could be enhanced to detect actual display type
        };

    // Register to emulator information
    emu_table["getregister"] = [this](const std::string& register_name) -> sol::object {
        // Delegate to CPU API for register access
        if(register_name == "PC" || register_name == "pc") {
            auto addr_table = lua.create_table();
            addr_table["cs"] = SegValue(::cs);
            addr_table["ip"] = reg_ip;
            return addr_table;
        }
        else if(register_name == "AX" || register_name == "ax") {
            return sol::make_object(lua, reg_ax);
        }
        else if(register_name == "BX" || register_name == "bx") {
            return sol::make_object(lua, reg_bx);
        }
        // Add more registers as needed
        return sol::nil;
        };

    lua["emu"] = emu_table;
    log_info("Emu API registered with frameadvance and control functions");
}

// Helper function to convert Lua objects to strings matching Lua's behavior
std::string luaObjectToString(const sol::object& obj) {
    if (!obj.valid()) {
        return "nil";
    }

    sol::type obj_type = obj.get_type();

    switch (obj_type) {
        case sol::type::nil:
            return "nil";

        case sol::type::boolean:
            return obj.as<bool>() ? "true" : "false";

        case sol::type::number: {
            double num = obj.as<double>();
            if (num == std::floor(num)) {
                return std::to_string(static_cast<long long>(num));
            } else {
                std::ostringstream oss;
                oss << std::setprecision(10) << num;
                std::string result = oss.str();
                result.erase(result.find_last_not_of('0') + 1, std::string::npos);
                return result;
            }
        }

        case sol::type::string:
            return obj.as<std::string>();

        case sol::type::table:
            return "table";

        case sol::type::function:
            return "function";

        default:
            return "userdata";
    }
}

void LuaEngine::registerConsoleAPI() {
    auto console_table = lua.create_table();

    // console.write() - write to console window and debug logger
    console_table["write"] = [this](const std::string& message) {
        std::lock_guard<std::mutex> lock(uiState.console_mutex);
        uiState.console_messages.push_back(message);
        log_info("Console: " + message);
    };

    lua["console"] = console_table;

    // Override global print() to output to console window
    lua.globals()["print"] = [this](sol::variadic_args va) {
        std::string output;

        // Join arguments with tabs (Lua standard behavior)
        for (size_t i = 0; i < va.size(); ++i) {
            if (i > 0) output += "\t";
            output += luaObjectToString(va[i]);
        }

        // Add to console display
        if (!output.empty()) {
            std::lock_guard<std::mutex> lock(uiState.console_mutex);
            uiState.console_messages.push_back(output);
        }

        // Also log for debugging
        if (!output.empty()) {
            log_info("Lua print: " + output);
        }
    };

    log_info("Console API registered with print() override");
}


// (Note: legacy safeDomain* wrappers removed as unused)

void LuaEngine::registerMemoryDomainsAPI() {
    using namespace MemoryDomains;

    auto domain_manager = GetDomainManager();
    if(!domain_manager) {
        log_info("Memory domains not available, skipping domain API registration");
        return;
    }

    // Reuse existing memory table when present instead of replacing it
    sol::table memory_table = lua["memory"];
    if(!memory_table.valid() || memory_table.get_type() != sol::type::table) {
        memory_table = lua.create_table();
    }

    // Enhanced memory operations with domain support
    memory_table["read_domain"] = [this](const std::string& domain_name, uint32_t address, sol::optional<size_t> size) -> sol::object {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return sol::nil;

        DomainType domain_type = StringToDomainType(domain_name);
        auto domain = domain_manager->GetDomain(domain_type);
        if(!domain || !domain->IsAvailable()) return sol::nil;

        size_t read_size = size.value_or(1);

        if(read_size == 1) {
            uint8_t value;
            if(domain->ReadByte(address, value) == AccessResult::SUCCESS) {
                return sol::make_object(lua, value);
            }
        }
        else {
            std::vector<uint8_t> buffer(read_size);
            if(domain->ReadBytes(address, buffer.data(), read_size) == AccessResult::SUCCESS) {
                auto result = lua.create_table();
                for(size_t i = 0; i < buffer.size(); ++i) {
                    result[i + 1] = buffer[i];
                }
                return result;
            }
        }

        return sol::nil;
        };

    memory_table["write_domain"] = [this](const std::string& domain_name, uint32_t address, sol::table data) -> bool {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return false;

        DomainType domain_type = StringToDomainType(domain_name);
        auto domain = domain_manager->GetDomain(domain_type);
        if(!domain || !domain->IsAvailable() || !domain->IsWritable()) return false;

        std::vector<uint8_t> bytes;
        for(size_t i = 1; i <= data.size(); ++i) {
            sol::optional<uint8_t> byte = data[i];
            if(byte) {
                bytes.push_back(byte.value());
            }
        }

        return domain->WriteBytes(address, bytes.data(), bytes.size()) == AccessResult::SUCCESS;
        };

    memory_table["write_domain_byte"] = [this](const std::string& domain_name, uint32_t address, uint8_t value) -> bool {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return false;

        DomainType domain_type = StringToDomainType(domain_name);
        auto domain = domain_manager->GetDomain(domain_type);
        if(!domain || !domain->IsAvailable() || !domain->IsWritable()) return false;

        return domain->WriteByte(address, value) == AccessResult::SUCCESS;
        };

    // Domain information functions
    memory_table["get_domains"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto available_domains = domain_manager->GetAvailableDomains();
        int index = 1;
        
        // Get the adapter manager instead of the main domain manager
        auto* adapter_manager = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager();
        if (!adapter_manager) {
            return result; // Return empty table if no adapter manager
        }
        
        // Convert main domain types to adapter domain types and get adapter domains
        std::vector<LuaEngineMemoryDomains::MemoryDomainType> adapter_types = {
            LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL,
            LuaEngineMemoryDomains::MemoryDomainType::DOS_UMB,
            LuaEngineMemoryDomains::MemoryDomainType::EMS,
            LuaEngineMemoryDomains::MemoryDomainType::XMS,
            LuaEngineMemoryDomains::MemoryDomainType::VIDEO_RAM,
            LuaEngineMemoryDomains::MemoryDomainType::BIOS_ROM
        };
        
        for(auto adapter_type : adapter_types) {
            auto adapter_domain = adapter_manager->GetDomain(adapter_type);
            if(adapter_domain) {
                try {
                    auto domain_info = lua.create_table();
                    
                    // Convert adapter type to string
                    std::string type_name;
                    switch(adapter_type) {
                        case LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL: type_name = "DOS Conventional"; break;
                        case LuaEngineMemoryDomains::MemoryDomainType::DOS_UMB: type_name = "DOS UMB"; break;
                        case LuaEngineMemoryDomains::MemoryDomainType::EMS: type_name = "EMS"; break;
                        case LuaEngineMemoryDomains::MemoryDomainType::XMS: type_name = "XMS"; break;
                        case LuaEngineMemoryDomains::MemoryDomainType::VIDEO_RAM: type_name = "Video RAM"; break;
                        case LuaEngineMemoryDomains::MemoryDomainType::BIOS_ROM: type_name = "BIOS ROM"; break;
                        default: type_name = "Unknown"; break;
                    }
                    
                    domain_info["name"] = type_name;
                    
                    // Use adapter methods directly since we're using the adapter layer
                    domain_info["display_name"] = adapter_domain->getName();
                    domain_info["start_address"] = adapter_domain->getBaseAddress();
                    domain_info["end_address"] = adapter_domain->getBaseAddress() + adapter_domain->getSize() - 1;
                    domain_info["size"] = adapter_domain->getSize();
                    domain_info["writable"] = adapter_domain->isWritable();
                    domain_info["available"] = adapter_domain->isAddressValid(adapter_domain->getBaseAddress());
                    result[index++] = domain_info;
                } catch (const std::exception& e) {
                    // Log the error but continue with other domains
                    if(logger) {
                        log_info("Warning: Failed to register domain info for adapter type " + std::to_string(static_cast<int>(adapter_type)) + ": " + e.what());
                    }
                } catch (...) {
                    // Log the error but continue with other domains
                    if(logger) {
                        log_info("Warning: Unknown error registering domain info for adapter type " + std::to_string(static_cast<int>(adapter_type)));
                    }
                }
            }
        }

        return result;
        };

    memory_table["get_domain_info"] = [this](const std::string& domain_name) -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        DomainType domain_type = StringToDomainType(domain_name);
        auto domain = domain_manager->GetDomain(domain_type);
        if(!domain) return result;

        // Convert main domain type to adapter type and get adapter domain
        LuaEngineMemoryDomains::MemoryDomainType adapter_type;
        switch(domain_type) {
            case DomainType::MAIN_RAM: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL; break;
            case DomainType::DOS_UMB: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::DOS_UMB; break;
            case DomainType::EMS_PAGES: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::EMS; break;
            case DomainType::XMS_EXTENDED: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::XMS; break;
            case DomainType::VIDEO_RAM: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::VIDEO_RAM; break;
            case DomainType::BIOS_ROM: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::BIOS_ROM; break;
            default: adapter_type = LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL; break;
        }
        
        auto* adapter_manager = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager();
        auto* adapter_domain = adapter_manager ? adapter_manager->GetDomain(adapter_type) : nullptr;
        
        result["name"] = DomainTypeToString(domain_type);
        if (adapter_domain) {
            result["display_name"] = adapter_domain->getName();
            result["start_address"] = adapter_domain->getBaseAddress();
            result["end_address"] = adapter_domain->getBaseAddress() + adapter_domain->getSize() - 1;
            result["size"] = adapter_domain->getSize();
            result["writable"] = adapter_domain->isWritable();
            result["available"] = adapter_domain->isAddressValid(adapter_domain->getBaseAddress());
        } else {
            result["display_name"] = "Unavailable";
            result["start_address"] = 0;
            result["end_address"] = 0;
            result["size"] = 0;
            result["writable"] = false;
            result["available"] = false;
        }

        return result;
        };

    // DOS-specific memory functions
    memory_table["get_mcb_chain"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto dos_domain = domain_manager->GetDOSConventional();
        if(!dos_domain) return result;

        auto mcb_list = dos_domain->GetMCBChain();
        int index = 1;
        for(uint32_t mcb_addr : mcb_list) {
            result[index++] = mcb_addr;
        }

        return result;
        };

    memory_table["get_memory_stats"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto stats = domain_manager->GetDomainStatistics();
        for(const auto& [type, stat] : stats) {
            auto stat_entry = lua.create_table();
            stat_entry["total_size"] = stat.total_size;
            stat_entry["available"] = stat.available;
            result[DomainTypeToString(type)] = stat_entry;
        }

        return result;
        };

    // EMS-specific functions
    memory_table["ems_get_total_pages"] = [this]() -> uint16_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto ems_domain = domain_manager->GetEMS();
        // GetTotalPages method not available in interface
        return 0;
        };

    memory_table["ems_get_free_pages"] = [this]() -> uint16_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto ems_domain = domain_manager->GetEMS();
        // GetFreePages method not available in interface
        return 0;
        };

    memory_table["ems_get_active_handles"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto ems_domain = domain_manager->GetEMS();
        if(!ems_domain) return result;

        auto handles = ems_domain->GetActiveHandles();
        int index = 1;
        for(uint16_t handle : handles) {
            auto handle_info = lua.create_table();
            handle_info["handle"] = handle;
            // GetHandleSize method not available in interface

            // GetHandleName method not available in interface

            result[index++] = handle_info;
        }

        return result;
        };

    memory_table["ems_get_page_frame"] = [this]() -> uint32_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto ems_domain = domain_manager->GetEMS();
        return ems_domain ? ems_domain->GetPageFrameAddress() : 0;
        };

    // XMS-specific functions
    memory_table["xms_get_total_memory"] = [this]() -> uint32_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto xms_domain = domain_manager->GetXMS();
        // GetTotalMemory method not available in interface
        return 0;
        };

    memory_table["xms_get_free_memory"] = [this]() -> uint32_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto xms_domain = domain_manager->GetXMS();
        // GetFreeMemory method not available in interface
        return 0;
        };

    memory_table["xms_get_active_handles"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto xms_domain = domain_manager->GetXMS();
        if(!xms_domain) return result;

        auto handles = xms_domain->GetActiveHandles();
        int index = 1;
        for(uint16_t handle : handles) {
            auto handle_info = lua.create_table();
            handle_info["handle"] = handle;
            uint32_t size, address;
            bool allocated;
            if(xms_domain->GetBlockInfo(handle, size, address, allocated)) {
                handle_info["size"] = size;
                handle_info["address"] = address;
                handle_info["allocated"] = allocated;
            }

            result[index++] = handle_info;
        }

        return result;
        };

    // Video RAM specific functions
    memory_table["video_get_mode"] = [this]() -> uint16_t {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return 0;

        auto video_domain = domain_manager->GetVideoRAM();
        return video_domain ? video_domain->GetVideoMode() : 0;
        };

    memory_table["video_get_resolution"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto video_domain = domain_manager->GetVideoRAM();
        if(!video_domain) return result;

        result["width"] = video_domain->GetScreenWidth();
        result["height"] = video_domain->GetScreenHeight();
        result["depth"] = video_domain->GetColorDepth();

        return result;
        };

    memory_table["video_get_buffers"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto video_domain = domain_manager->GetVideoRAM();
        if(!video_domain) return result;

        result["text_address"] = video_domain->GetTextBufferAddress();
        result["graphics_address"] = video_domain->GetGraphicsBufferAddress();
        result["display_start"] = video_domain->GetCurrentDisplayStart();
        result["is_text_mode"] = video_domain->IsTextMode();
        result["is_graphics_mode"] = video_domain->IsGraphicsMode();

        return result;
        };

    memory_table["video_is_pc98"] = [this]() -> bool {
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return false;

        auto video_domain = domain_manager->GetVideoRAM();
        return video_domain ? video_domain->IsPC98Mode() : false;
        };

    // PC-98 specific video functions
    memory_table["pc98_get_video_info"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto video_domain = domain_manager->GetVideoRAM();
        if(!video_domain || !video_domain->IsPC98Mode()) return result;

        result["text_address"] = video_domain->GetPC98TextAddress();
        result["graphics_address"] = video_domain->GetPC98GraphicsAddress();
        result["attribute_address"] = video_domain->GetPC98AttributeAddress();

        // Get active bitplanes
        auto planes = video_domain->GetPC98ActivePlanes();
        auto planes_table = lua.create_table();
        int index = 1;
        for(uint8_t plane : planes) {
            auto plane_info = lua.create_table();
            uint32_t address, size;
            if(video_domain->GetPC98PlaneInfo(plane, address, size)) {
                plane_info["plane"] = plane;
                plane_info["address"] = address;
                plane_info["size"] = size;
                planes_table[index++] = plane_info;
            }
        }
        result["bitplanes"] = planes_table;

        return result;
        };

    // BIOS ROM specific functions
    memory_table["bios_get_info"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto bios_domain = domain_manager->GetBIOSROM();
        if(!bios_domain) return result;

        result["entry_point"] = bios_domain->GetBIOSEntryPoint();
        result["video_rom"] = bios_domain->GetVideoROMAddress();
        result["version"] = bios_domain->GetBIOSVersion();
        result["date"] = bios_domain->GetBIOSDate();

        return result;
        };

    memory_table["bios_check_address"] = [this](uint32_t address) -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto bios_domain = domain_manager->GetBIOSROM();
        if(!bios_domain) return result;

        result["is_system_bios"] = bios_domain->IsSystemBIOS(address);
        result["is_video_bios"] = bios_domain->IsVideoBIOS(address);
        result["is_extension_rom"] = bios_domain->IsExtensionROM(address);
        result["is_pc98_bios"] = bios_domain->IsPC98BIOS(address);

        return result;
        };

    // PC-98 specific BIOS functions
    memory_table["pc98_get_bios_info"] = [this]() -> sol::table {
        auto result = lua.create_table();
        auto domain_manager = GetDomainManager();
        if(!domain_manager) return result;

        auto bios_domain = domain_manager->GetBIOSROM();
        if(!bios_domain) return result;

        result["system_rom"] = bios_domain->GetPC98SystemROMAddress();
        result["chargen_rom"] = bios_domain->GetPC98CharGenROMAddress();
        result["kanji_rom"] = bios_domain->GetPC98KanjiROMAddress();

        return result;
        };

    lua["memory"] = memory_table;
    log_info("Enhanced memory domains API registered with Video RAM and BIOS ROM support");
}

void LuaEngine::registerFrameControlAPI() {
    // Frame controller is already initialized in LUAENGINE_Init()
    // Just initialize it if needed
    if(frame_controller) {
        frame_controller->initialize();
    }

    auto emu_table = lua.create_table();

    // Frame control functions
    emu_table["pause"] = []() {
        LuaEngineFrameControl::BizHawkCompat::pauseEmulation();
        };

    emu_table["unpause"] = []() {
        LuaEngineFrameControl::BizHawkCompat::unpauseEmulation();
        };

    emu_table["ispaused"] = []() -> bool {
        return LuaEngineFrameControl::BizHawkCompat::isEmulationPaused();
        };

    emu_table["frameadvance"] = []() {
        LuaEngineFrameControl::BizHawkCompat::frameAdvance();
        };

    emu_table["seekframe"] = [](uint64_t frame) {
        LuaEngineFrameControl::BizHawkCompat::seekFrame(frame);
        };

    // Speed control functions
    emu_table["setspeed"] = [](int percent) {
        LuaEngineFrameControl::BizHawkCompat::setSpeedPercent(percent);
        };

    emu_table["getspeed"] = []() -> int {
        return LuaEngineFrameControl::BizHawkCompat::getSpeedPercent();
        };

    emu_table["setminspeed"] = [](int percent) {
        LuaEngineFrameControl::BizHawkCompat::setMinimumSpeedPercent(percent);
        };

    emu_table["setmaxspeed"] = [](int percent) {
        LuaEngineFrameControl::BizHawkCompat::setMaximumSpeedPercent(percent);
        };

    emu_table["throttle"] = [](bool enabled) {
        LuaEngineFrameControl::BizHawkCompat::throttleSpeed(enabled);
        };

    // Frame information functions
    emu_table["framecount"] = []() -> uint64_t {
        return LuaEngineFrameControl::BizHawkCompat::getFrameCount();
        };

    emu_table["lagcount"] = []() -> uint64_t {
        return LuaEngineFrameControl::BizHawkCompat::getLagCount();
        };

    emu_table["islagged"] = [](sol::optional<uint64_t> frame) -> bool {
        if(frame) {
            return LuaEngineFrameControl::BizHawkCompat::isLagged(frame.value());
        }
        return LuaEngineFrameControl::BizHawkCompat::isLagged();
        };

    emu_table["getfps"] = []() -> double {
        return LuaEngineFrameControl::BizHawkCompat::getFPS();
        };

    emu_table["gettargetfps"] = []() -> double {
        return LuaEngineFrameControl::BizHawkCompat::getTargetFPS();
        };

    // Frame skipping functions
    emu_table["setframeskip"] = [](int skip) {
        LuaEngineFrameControl::BizHawkCompat::setFrameSkip(skip);
        };

    emu_table["getframeskip"] = []() -> int {
        return LuaEngineFrameControl::BizHawkCompat::getFrameSkip();
        };

    // System information
    emu_table["getsystemid"] = []() -> std::string {
        return LuaEngineFrameControl::BizHawkCompat::getSystemId();
        };

    emu_table["getdisplaytype"] = []() -> std::string {
        return LuaEngineFrameControl::BizHawkCompat::getDisplayType();
        };

    // Advanced timing functions
    auto timing_table = lua.create_table();

    timing_table["setmode"] = [](const std::string& mode) {
        using LuaEngineFrameControl::SpeedMode;
        if(!LuaEngineFrameControl::g_frame_controller) return;

        if(mode == "normal") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::NORMAL);
        }
        else if(mode == "nothrottle") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::NOTHROTTLE);
        }
        else if(mode == "turbo") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::TURBO);
        }
        else if(mode == "maximum") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::MAXIMUM);
        }
        else if(mode == "custom") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::CUSTOM);
        }
        else if(mode == "step") {
            LuaEngineFrameControl::g_frame_controller->setSpeedMode(SpeedMode::FRAME_BY_FRAME);
        }
        };

    timing_table["getmode"] = []() -> std::string {
        if(!LuaEngineFrameControl::g_frame_controller) return "normal";

        using LuaEngineFrameControl::SpeedMode;
        switch(LuaEngineFrameControl::g_frame_controller->getSpeedMode()) {
        case SpeedMode::NORMAL: return "normal";
        case SpeedMode::NOTHROTTLE: return "nothrottle";
        case SpeedMode::TURBO: return "turbo";
        case SpeedMode::MAXIMUM: return "maximum";
        case SpeedMode::CUSTOM: return "custom";
        case SpeedMode::FRAME_BY_FRAME: return "step";
        default: return "unknown";
        }
        };

    timing_table["setmultiplier"] = [](double multiplier) {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->setCustomSpeedMultiplier(multiplier);
        }
        };

    timing_table["getmultiplier"] = []() -> double {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->getCustomSpeedMultiplier() : 1.0;
        };

    timing_table["settargetfps"] = [](double fps) {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->setTargetFPS(fps);
        }
        };

    timing_table["enablestepping"] = [](bool enabled) {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->enableFrameStepping(enabled);
        }
        };

    timing_table["isstepping"] = []() -> bool {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->isFrameStepping() : false;
        };

    timing_table["advanceframes"] = [](uint64_t count) {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->advanceFrames(count);
        }
        };

    // Lag frame detection
    auto lag_table = lua.create_table();

    lag_table["enable"] = [](bool enabled) {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->setLagFrameDetectionEnabled(enabled);
        }
        };

    lag_table["isenabled"] = []() -> bool {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->isLagFrameDetectionEnabled() : false;
        };

    lag_table["getpercentage"] = []() -> double {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->getLagFramePercentage() : 0.0;
        };

    lag_table["markinput"] = []() {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->markInputFrame();
        }
        };

    lag_table["marklag"] = []() {
        if(LuaEngineFrameControl::g_frame_controller) {
            LuaEngineFrameControl::g_frame_controller->markLagFrame();
        }
        };

    // Performance monitoring
    auto perf_table = lua.create_table();

    perf_table["getstats"] = [this]() -> sol::table {
        auto result = lua.create_table();
        if(!LuaEngineFrameControl::g_frame_controller) return result;

        auto stats = LuaEngineFrameControl::g_frame_controller->getFrameStats();
        result["frame_count"] = stats.frame_count;
        result["lag_frame_count"] = stats.lag_frame_count;
        result["input_frame_count"] = stats.input_frame_count;
        result["average_fps"] = stats.average_fps;
        result["target_fps"] = stats.target_fps;
        result["actual_fps"] = stats.actual_fps;
        result["total_time_us"] = stats.total_time_us;
        result["frame_time_us"] = stats.frame_time_us;
        result["render_time_us"] = stats.render_time_us;
        result["cpu_time_us"] = stats.cpu_time_us;
        result["is_lag_frame"] = stats.is_lag_frame;
        result["frame_skip_count"] = stats.frame_skip_count;
        result["speed_multiplier"] = stats.speed_multiplier;

        return result;
        };

    perf_table["getreport"] = []() -> std::string {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->getPerformanceReport() : "Frame controller not initialized";
        };

    perf_table["getframetimehistory"] = [this]() -> sol::table {
        auto result = lua.create_table();
        if(!LuaEngineFrameControl::g_frame_controller) return result;

        auto history = LuaEngineFrameControl::g_frame_controller->getFrameTimeHistory();
        for(size_t i = 0; i < history.size(); i++) {
            result[i + 1] = history[i];
        }
        return result;
        };

    perf_table["getfpshistory"] = [this]() -> sol::table {
        auto result = lua.create_table();
        if(!LuaEngineFrameControl::g_frame_controller) return result;

        auto history = LuaEngineFrameControl::g_frame_controller->getFPSHistory();
        for(size_t i = 0; i < history.size(); i++) {
            result[i + 1] = history[i];
        }
        return result;
        };

    perf_table["getaveragefps"] = [](sol::optional<uint64_t> frames) -> double {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->getAverageFPS(frames.value_or(60)) : 0.0;
        };

    perf_table["getaverageframetime"] = [](sol::optional<uint64_t> frames) -> double {
        return LuaEngineFrameControl::g_frame_controller ?
            LuaEngineFrameControl::g_frame_controller->getAverageFrameTime(frames.value_or(60)) : 0.0;
        };

    // Frame range analysis
    perf_table["analyzerange"] = [this](uint64_t start_frame, uint64_t end_frame) -> sol::table {
        auto result = lua.create_table();
        if(!LuaEngineFrameControl::g_frame_controller) return result;

        auto stats = LuaEngineFrameControl::g_frame_controller->analyzeFrameRange(start_frame, end_frame);
        result["start_frame"] = stats.start_frame;
        result["end_frame"] = stats.end_frame;
        result["total_frames"] = stats.total_frames;
        result["lag_frames"] = stats.lag_frames;
        result["average_fps"] = stats.average_fps;
        result["min_fps"] = stats.min_fps;
        result["max_fps"] = stats.max_fps;
        result["total_time_ms"] = stats.total_time_ms;

        return result;
        };

    // Register sub-tables
    emu_table["timing"] = timing_table;
    emu_table["lag"] = lag_table;
    emu_table["perf"] = perf_table;

    // Register main emu table (extending existing if present)
    sol::optional<sol::table> existing_emu = lua["emu"];
    if(existing_emu) {
        // Merge with existing emu table
        for(auto& [key, value] : emu_table) {
            existing_emu.value()[key] = value;
        }
    }
    else {
        lua["emu"] = emu_table;
    }

    log_info("Frame control API registered with BizHawk-compatible functions");
}

void LuaEngine::registerWindowSystemAPI() {
    // Initialize window system
    if(!LuaEngineGUIWindows::WindowUtils::initializeWindowSystem(&lua)) {
        log_info("Warning: Window system initialization failed");
        return;
    }

    // The window system API is automatically registered by the WindowManager
    // during initialization, so we just need to ensure it's initialized

    log_info("Window system API registered with tool window support");
}

void LuaEngine::registerCDLAPI() {
#ifdef C_LUA_RE_HOOKS
    sol::table cdl = lua.create_named_table("cdl");

    cdl["create"] = [](const std::string& session) {
        PC98CDL::ResetCDL();
        return PC98CDL::GetCDL().start(session);
    };

    cdl["register_module"] = [](sol::table info) -> bool {
        PC98CDL::ModuleInfo mi;
        mi.id = info.get_or<std::string>("id", "");
        mi.base = info.get_or<uint32_t>("base", 0);
        mi.size = info.get_or<uint32_t>("size", 0);
        mi.source_file = info.get_or<std::string>("source_file", "");
        mi.source_offset = info.get_or<uint32_t>("source_offset", 0);
        mi.enable_last_writer = info.get_or<bool>("enable_last_writer", false);
        mi.enable_coverage = info.get_or<bool>("enable_coverage", false);
        if (mi.id.empty() || mi.size == 0) return false;
        return PC98CDL::GetCDL().registerModule(mi) != nullptr;
    };

    cdl["set_active_module"] = [](const std::string& id) {
        return PC98CDL::GetCDL().setActiveModule(id);
    };

    cdl["start"] = [this]() {
        if (PC98CDL::GetCDL().moduleIds().empty()) {
            PC98CDL::GetCDL().registerModule({"conventional", 0, 0xA0000});
        }
        PC98CDL::GetCDL().start("session");
        enableInstructionHooks(true);
        return true;
    };

    cdl["stop"] = [this]() {
        PC98CDL::GetCDL().stop();
        return true;
    };

    cdl["begin_load"] = [](sol::table info) {
        PC98CDL::LoadSpan span;
        span.file = info.get_or<std::string>("file", "");
        span.file_offset = info.get_or<uint32_t>("file_offset", 0);
        span.destination = info.get_or<uint32_t>("destination", 0);
        span.size = info.get_or<uint32_t>("size", 0);
        PC98CDL::GetCDL().beginLoad(span);
        return true;
    };

    cdl["end_load"] = []() {
        PC98CDL::GetCDL().endLoad();
        return true;
    };

    cdl["data_write"] = [](uint32_t linear, uint32_t len) {
        PC98CDL::GetCDL().recordDataWrite(linear, len);
    };

    cdl["data_read"] = [](uint32_t linear, uint32_t len) {
        PC98CDL::GetCDL().recordDataRead(linear, len);
    };

    cdl["save"] = [](const std::string& path) {
        PC98CDL::GetCDL().save(path);
        return true;
    };

    cdl["merge"] = [](const std::string& path) {
        PC98CDL::GetCDL().merge(path);
        return true;
    };

    cdl["stats"] = []() -> std::string {
        return PC98CDL::GetCDL().statsJson();
    };

    // ponytail: PR6 — coverage + last-writer export APIs
    cdl["coverage"] = [](const std::string& module_id) -> std::string {
        PC98CDL::CDL& cdl = PC98CDL::GetCDL();
        PC98CDL::Module* m = cdl.getModule(module_id);
        if (!m) return "{}";
        return m->coverageJson();
    };

    cdl["last_writer"] = [](const std::string& module_id) -> std::string {
        PC98CDL::CDL& cdl = PC98CDL::GetCDL();
        PC98CDL::Module* m = cdl.getModule(module_id);
        if (!m || !m->enable_last_writer) return "{}";
        std::ostringstream out;
        out << "{";
        bool first = true;
        for (uint32_t i = 0; i < m->size; ++i) {
            uint32_t lwpc = m->lastWriterPc(i);
            if (lwpc != 0) {
                if (!first) out << ",";
                first = false;
                out << "\"" << i << "\":" << lwpc;
            }
        }
        out << "}";
        return out.str();
    };

    cdl["export_json"] = [](const std::string& path) -> bool {
        PC98CDL::GetCDL().exportCoverageToFile(path);
        return true;
    };

    cdl["export_jsonl"] = [](const std::string& path) -> bool {
        PC98CDL::GetCDL().exportJsonlToFile(path);
        return true;
    };
#endif
}


// ponytail: PR6 — Reverse-step + annotations + typed-data Lua API
void LuaEngine::registerReverseEngineeringAPI() {
#ifdef C_LUA_RE_HOOKS
    // --- Reverse-step namespace ---
    sol::table re = lua["re"];

    re["reverse_step"] = [this]() -> bool {
        auto* wm = window_manager.get();
        if (!wm) return false;
        auto* tl = wm->getTraceLogger();
        if (!tl || !tl->isReverseStepEnabled()) return false;
        return tl->reverseStep();
    };

    re["set_reverse_step_enabled"] = [this](bool enabled) {
        auto* wm = window_manager.get();
        if (!wm) return;
        auto* tl = wm->getTraceLogger();
        if (tl) tl->setReverseStepEnabled(enabled);
    };

    re["is_reverse_step_available"] = [this]() -> bool {
        auto* wm = window_manager.get();
        if (!wm) return false;
        auto* tl = wm->getTraceLogger();
        return tl && tl->isReverseStepAvailable();
    };

    // --- Annotation namespace ---
    sol::table annotation = lua.create_named_table("annotation");

    annotation["add"] = [this](uint32_t address, const std::string& comment, sol::optional<std::string> module) {
        if (!symbol_manager) return;
        LuaEngineSymbols::Annotation ann;
        ann.address = address;
        ann.comment = comment;
        ann.module = module.value_or("");
        symbol_manager->addAnnotation(ann);
    };

    annotation["get_by_address"] = [this](uint32_t address) -> sol::table {
        if (!symbol_manager) return sol::nil;
        auto annots = symbol_manager->getAnnotationsByAddress(address);
        sol::table result = lua.create_table();
        for (size_t i = 0; i < annots.size(); ++i) {
            sol::table entry = lua.create_table();
            entry["address"] = annots[i].address;
            entry["comment"] = annots[i].comment;
            entry["module"] = annots[i].module;
            result[i + 1] = entry;
        }
        return result;
    };

    annotation["search"] = [this](const std::string& substring) -> sol::table {
        if (!symbol_manager) return sol::nil;
        auto annots = symbol_manager->getAnnotationsByText(substring);
        sol::table result = lua.create_table();
        for (size_t i = 0; i < annots.size(); ++i) {
            sol::table entry = lua.create_table();
            entry["address"] = annots[i].address;
            entry["comment"] = annots[i].comment;
            entry["module"] = annots[i].module;
            result[i + 1] = entry;
        }
        return result;
    };

    annotation["remove"] = [this](uint32_t address, const std::string& comment) -> bool {
        if (!symbol_manager) return false;
        return symbol_manager->removeAnnotation(address, comment);
    };

    // --- Typed data namespace ---
    sol::table typed_data = lua.create_named_table("typed_data");

    typed_data["define"] = [this](uint32_t address, uint32_t length, const std::string& type,
                                    uint32_t element_size, sol::optional<std::string> module) -> bool {
        if (!symbol_manager) return false;
        LuaEngineSymbols::TypedDataRange range;
        range.start_address = address;
        range.length = length;
        range.data_type = type;
        range.element_size = element_size;
        range.module = module.value_or("");
        return symbol_manager->addTypedDataRange(range);
    };

    typed_data["get"] = [this](uint32_t address) -> sol::table {
        if (!symbol_manager) return sol::nil;
        auto ranges = symbol_manager->getTypedDataRangesAt(address);
        sol::table result = lua.create_table();
        for (size_t i = 0; i < ranges.size(); ++i) {
            sol::table entry = lua.create_table();
            entry["start_address"] = ranges[i].start_address;
            entry["length"] = ranges[i].length;
            entry["data_type"] = ranges[i].data_type;
            entry["element_size"] = ranges[i].element_size;
            entry["module"] = ranges[i].module;
            result[i + 1] = entry;
        }
        return result;
    };

    typed_data["list"] = [this](sol::optional<std::string> module) -> sol::table {
        if (!symbol_manager) return sol::nil;
        auto ranges = symbol_manager->getAllTypedDataRanges(module.value_or(""));
        sol::table result = lua.create_table();
        for (size_t i = 0; i < ranges.size(); ++i) {
            sol::table entry = lua.create_table();
            entry["start_address"] = ranges[i].start_address;
            entry["length"] = ranges[i].length;
            entry["data_type"] = ranges[i].data_type;
            entry["element_size"] = ranges[i].element_size;
            entry["module"] = ranges[i].module;
            result[i + 1] = entry;
        }
        return result;
    };

    typed_data["remove"] = [this](uint32_t start_address) -> bool {
        if (!symbol_manager) return false;
        return symbol_manager->removeTypedDataRange(start_address);
    };
#endif
}


// C-style wrapper function for DOSBox section system
void LUA_Init(Section* section) {
    luaEngine.LUAENGINE_Init(section);
}

// ========== Sol2 Performance Optimization Implementation ==========

void LuaEngine::initializeSol2PerformanceCache() {
    // Initialize cached tables for reuse
    sol2_cache.cached_memory_result = lua.create_table();
    sol2_cache.cached_cpu_state = lua.create_table();
    sol2_cache.cached_debug_info = lua.create_table();
    sol2_cache.cached_address_info = lua.create_table();
    sol2_cache.cached_breakpoint_list = lua.create_table();
    sol2_cache.cached_breakpoint_info = lua.create_table();
    sol2_cache.cached_savestate_info = lua.create_table();

    // Pre-allocate buffers
    sol2_cache.temp_memory_buffer.reserve(1024); // Reserve space for common operations
    sol2_cache.temp_string_buffer.reserve(256);
    sol2_cache.temp_hex_buffer.reserve(1024);    // For hex dump operations

    // Initialize performance flags
    sol2_cache.fast_memory_access = false;
    sol2_cache.fast_cpu_access = false;
    sol2_cache.minimal_error_checking = false;
    sol2_cache.fast_debug_access = false;
    sol2_cache.fast_breakpoint_access = false;

    // Initialize function cache flags
    sol2_cache.has_frame_callback = false;
    sol2_cache.has_memory_callback = false;

    DEBUG_ShowMsg("LuaEngine: Sol2 performance cache initialized");
}



void LuaEngine::initializeWorkerThreads() {
    if (!sol2_cache.worker_threads.empty()) return; // Already initialized
    
    size_t thread_count = sol2_cache.max_worker_threads.load();
    sol2_cache.shutdown_threads.store(false);
    
    for (size_t i = 0; i < thread_count; ++i) {
        sol2_cache.worker_threads.emplace_back(&LuaEngine::workerThreadFunction, this);
    }
    
    log_info("Initialized " + std::to_string(thread_count) + " worker threads for background processing");
}

void LuaEngine::shutdownWorkerThreads() {
    sol2_cache.shutdown_threads.store(true);
    sol2_cache.background_tasks_cv.notify_all();
    
    for (auto& thread : sol2_cache.worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    sol2_cache.worker_threads.clear();
    
    // Clear remaining tasks
    std::lock_guard<std::mutex> lock(sol2_cache.background_tasks_mutex);
    while (!sol2_cache.background_tasks.empty()) {
        sol2_cache.background_tasks.pop();
    }
    
    log_info("Worker threads shut down");
}

void LuaEngine::enqueueBackgroundTask(std::function<void()> task) {
    if (!sol2_cache.enable_threading.load()) {
        // Execute immediately if threading is disabled
        task();
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(sol2_cache.background_tasks_mutex);
        sol2_cache.background_tasks.push(std::move(task));
    }
    sol2_cache.background_tasks_cv.notify_one();
}

void LuaEngine::workerThreadFunction() {
    while (!sol2_cache.shutdown_threads.load()) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(sol2_cache.background_tasks_mutex);
            sol2_cache.background_tasks_cv.wait(lock, [this] {
                return !sol2_cache.background_tasks.empty() || sol2_cache.shutdown_threads.load();
            });
            
            if (sol2_cache.shutdown_threads.load()) break;
            
            if (!sol2_cache.background_tasks.empty()) {
                task = std::move(sol2_cache.background_tasks.front());
                sol2_cache.background_tasks.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                log_error("Worker thread task failed: " + std::string(e.what()));
            }
        }
    }
}

void LuaEngine::setThreadingEnabled(bool enabled) {
    sol2_cache.enable_threading.store(enabled);
    log_info("Background threading " + std::string(enabled ? "enabled" : "disabled"));
}

void LuaEngine::setMaxWorkerThreads(size_t count) {
    if (count == 0) count = 1; // Minimum of 1 thread
    if (count > 16) count = 16; // Maximum of 16 threads
    
    sol2_cache.max_worker_threads.store(count);
    
    // Restart threads if already running
    if (!sol2_cache.worker_threads.empty()) {
        shutdownWorkerThreads();
        initializeWorkerThreads();
    }
    
    log_info("Max worker threads set to " + std::to_string(count));
}

void LuaEngine::setSol2PerformanceMode(bool fast_memory, bool fast_cpu, bool minimal_errors) {
    sol2_cache.fast_memory_access = fast_memory;
    sol2_cache.fast_cpu_access = fast_cpu;
    sol2_cache.minimal_error_checking = minimal_errors;
    
    // Enable fast access for debug and breakpoint operations when in fast mode
    sol2_cache.fast_debug_access = fast_memory && fast_cpu;
    sol2_cache.fast_breakpoint_access = fast_memory && fast_cpu;
    
    log_info("Performance mode set: memory=" + std::string(fast_memory ? "fast" : "normal") +
                " cpu=" + std::string(fast_cpu ? "fast" : "normal") +
                " errors=" + std::string(minimal_errors ? "minimal" : "full"));
                
    // Optimize table reuse when in fast mode
    if (fast_memory || fast_cpu) {
        optimizeTableReuse();
    }
}

void LuaEngine::cacheFrequentFunctions() {
    // Cache commonly used callback functions for improved performance
    try {
        // Look for frame callback function
        sol::optional<sol::protected_function> frame_func = lua["onframeadvance"];
        if(frame_func && frame_func.value().get_type() == sol::type::function) {
            sol2_cache.cached_frame_callback = frame_func.value();
            sol2_cache.has_frame_callback = true;
            DEBUG_ShowMsg("LuaEngine: Cached onframeadvance callback function");
        }

        // Look for memory callback function
        sol::optional<sol::protected_function> memory_func = lua["onmemoryaccess"];
        if(memory_func && memory_func.value().get_type() == sol::type::function) {
            sol2_cache.cached_memory_callback = memory_func.value();
            sol2_cache.has_memory_callback = true;
            DEBUG_ShowMsg("LuaEngine: Cached onmemoryaccess callback function");
        }

    // Register performance control API (augment existing table if present)
    sol::table perf_table = lua["performance"];
    if(!perf_table.valid() || perf_table.get_type() != sol::type::table) {
        perf_table = lua.create_table();
    }

        // Fast mode control for all systems
        perf_table["set_fast_mode"] = [this](bool enable) {
            setSol2PerformanceMode(enable, enable, enable);
            };

        // Individual subsystem control
        perf_table["set_memory_fast"] = [this](bool enable) {
            sol2_cache.fast_memory_access = enable;
            DEBUG_ShowMsg("LuaEngine: Memory fast mode %s", enable ? "enabled" : "disabled");
            };

        perf_table["set_cpu_fast"] = [this](bool enable) {
            sol2_cache.fast_cpu_access = enable;
            DEBUG_ShowMsg("LuaEngine: CPU fast mode %s", enable ? "enabled" : "disabled");
            };

        perf_table["set_minimal_errors"] = [this](bool enable) {
            sol2_cache.minimal_error_checking = enable;
            DEBUG_ShowMsg("LuaEngine: Minimal error checking %s", enable ? "enabled" : "disabled");
            };

        // Cache management
        perf_table["clear_cache"] = [this]() {
            resetSol2Cache();
            DEBUG_ShowMsg("LuaEngine: Sol2 cache cleared");
            };
            
        // Threading control
        perf_table["set_threading"] = [this](bool enable) {
            setThreadingEnabled(enable);
            DEBUG_ShowMsg("LuaEngine: Threading %s", enable ? "enabled" : "disabled");
            };
            
        perf_table["set_worker_threads"] = [this](int count) {
            setMaxWorkerThreads(static_cast<size_t>(count));
            DEBUG_ShowMsg("LuaEngine: Worker threads set to %d", count);
            };
            
        perf_table["enqueue_task"] = [this](sol::function task) {
            // Allow Lua scripts to enqueue background tasks while keeping Lua calls serialized
            auto cpp_task = [this, task]() {
                std::lock_guard<std::mutex> lua_lock(lua_state_mutex_);
                try {
                    task();
                } catch (const std::exception& e) {
                    // Error handling - can't easily pass back to Lua from worker thread
                }
            };
            enqueueBackgroundTask(cpp_task);
            };

        // Cache information
        perf_table["get_cache_info"] = [this]() -> sol::table {
            // Use cached debug info table for performance information
            sol2_cache.cached_debug_info.clear();
            sol2_cache.cached_debug_info["fast_memory"] = sol2_cache.fast_memory_access;
            sol2_cache.cached_debug_info["fast_cpu"] = sol2_cache.fast_cpu_access;
            sol2_cache.cached_debug_info["fast_debug"] = sol2_cache.fast_debug_access;
            sol2_cache.cached_debug_info["fast_breakpoint"] = sol2_cache.fast_breakpoint_access;
            sol2_cache.cached_debug_info["minimal_errors"] = sol2_cache.minimal_error_checking;
            sol2_cache.cached_debug_info["has_frame_callback"] = sol2_cache.has_frame_callback;
            sol2_cache.cached_debug_info["has_memory_callback"] = sol2_cache.has_memory_callback;
            sol2_cache.cached_debug_info["buffer_capacity"] = sol2_cache.temp_memory_buffer.capacity();
            sol2_cache.cached_debug_info["threading_enabled"] = sol2_cache.enable_threading.load();
            sol2_cache.cached_debug_info["worker_threads"] = sol2_cache.worker_threads.size();
            sol2_cache.cached_debug_info["max_worker_threads"] = sol2_cache.max_worker_threads.load();
            sol2_cache.cached_debug_info["background_tasks"] = sol2_cache.background_tasks.size();
            return sol2_cache.cached_debug_info;
            };

        // Performance benchmarking function
        perf_table["benchmark"] = [this](const std::string& test_type, int iterations) -> double {
            auto start = std::chrono::high_resolution_clock::now();

            if(test_type == "memory_read") {
                // Benchmark memory read operations
                for(int i = 0; i < iterations; ++i) {
                    uint8_t value;
                    PhysPt phys = static_cast<PhysPt>(GetAddress(0x0040, i % 256));
                    mem_readb_checked(phys, &value);
                }
            }
            else if(test_type == "cpu_state") {
                // Benchmark CPU state access
                for(int i = 0; i < iterations; ++i) {
                    volatile uint16_t ax = reg_ax;
                    volatile uint16_t bx = reg_bx;
                    volatile uint16_t cx = reg_cx;
                    volatile uint16_t dx = reg_dx;
                    (void)ax; (void)bx; (void)cx; (void)dx; // Suppress unused warnings
                }
            }
            else if(test_type == "table_creation") {
                // Benchmark table creation vs reuse
                for(int i = 0; i < iterations; ++i) {
                    if(sol2_cache.fast_memory_access) {
                        sol2_cache.cached_memory_result.clear();
                        sol2_cache.cached_memory_result["test"] = i;
                    }
                    else {
                        auto test_table = lua.create_table();
                        test_table["test"] = i;
                    }
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return static_cast<double>(duration.count()) / iterations; // microseconds per operation
            };

    lua["performance"] = perf_table;

    }
    catch(const std::exception& e) {
        DEBUG_ShowMsg("LuaEngine: Error caching functions: %s", e.what());
    }
}

void LuaEngine::optimizeTableReuse() {
    // Ensure all cached tables are properly sized and cleared
    sol2_cache.cached_memory_result.clear();
    sol2_cache.cached_cpu_state.clear();
    sol2_cache.cached_debug_info.clear();

    // Pre-populate commonly used keys to avoid rehashing
    sol2_cache.cached_memory_result["type"] = sol::nil;
    sol2_cache.cached_memory_result["address"] = sol::nil;
    sol2_cache.cached_memory_result["value"] = sol::nil;
    sol2_cache.cached_memory_result["size"] = sol::nil;

    sol2_cache.cached_cpu_state["ax"] = sol::nil;
    sol2_cache.cached_cpu_state["bx"] = sol::nil;
    sol2_cache.cached_cpu_state["cx"] = sol::nil;
    sol2_cache.cached_cpu_state["dx"] = sol::nil;
    sol2_cache.cached_cpu_state["cs"] = sol::nil;
    sol2_cache.cached_cpu_state["ds"] = sol::nil;
    sol2_cache.cached_cpu_state["es"] = sol::nil;
    sol2_cache.cached_cpu_state["ss"] = sol::nil;
    sol2_cache.cached_cpu_state["ip"] = sol::nil;
    sol2_cache.cached_cpu_state["flags"] = sol::nil;

    DEBUG_ShowMsg("LuaEngine: Table reuse optimization applied");
}

void LuaEngine::resetSol2Cache() {
    // Clear all cached tables
    if(lua.lua_state() != nullptr) {
        sol2_cache.cached_memory_result = lua.create_table();
        sol2_cache.cached_cpu_state = lua.create_table();
        sol2_cache.cached_debug_info = lua.create_table();
        sol2_cache.cached_address_info = lua.create_table();
        sol2_cache.cached_breakpoint_list = lua.create_table();
        sol2_cache.cached_breakpoint_info = lua.create_table();
        sol2_cache.cached_savestate_info = lua.create_table();
    }

    // Clear buffers
    sol2_cache.temp_memory_buffer.clear();
    sol2_cache.temp_string_buffer.clear();
    sol2_cache.temp_hex_buffer.clear();

    // Reset function cache flags
    sol2_cache.has_frame_callback = false;
    sol2_cache.has_memory_callback = false;

    // Recache frequently used functions
    cacheFrequentFunctions();

    DEBUG_ShowMsg("LuaEngine: Sol2 cache reset complete");
}


void LuaEngine::populateAutocompleteAPI() {
    if(!autocomplete_engine) return;

    log_info("Populating autocomplete engine with registered API functions...");

    // Clear any existing registry (in case of re-initialization)
    autocomplete_engine->clearRegistry();

    // Re-register namespaces
    autocomplete_engine->registerNamespace("memory", "Memory access functions for reading/writing emulated memory");
    autocomplete_engine->registerNamespace("cpu", "CPU register and state access functions");
    autocomplete_engine->registerNamespace("debug", "Debugging utilities and breakpoint management");
    autocomplete_engine->registerNamespace("emu", "Emulator control functions (pause, speed, frame advance)");
    autocomplete_engine->registerNamespace("input", "Input recording and playback functions");
    autocomplete_engine->registerNamespace("gui", "GUI overlay and drawing functions");
    autocomplete_engine->registerNamespace("savestate", "Save state management functions");
    autocomplete_engine->registerNamespace("event", "Event system for hooks and callbacks");
    autocomplete_engine->registerNamespace("breakpoint", "Breakpoint management functions");
    autocomplete_engine->registerNamespace("symbols", "Symbol lookup and symbolic breakpoints");
    autocomplete_engine->registerNamespace("domains", "Memory domain access functions");
    autocomplete_engine->registerNamespace("frame", "Frame control and timing functions");
    autocomplete_engine->registerNamespace("window", "Window system management functions");
    autocomplete_engine->registerNamespace("symbols", "Symbol lookup and symbolic breakpoints");

    // Extended Memory API functions
    autocomplete_engine->registerFunction("readbyte", "memory.readbyte", "readbyte(segment, offset)",
        "Read a single byte from memory at segment:offset");
    autocomplete_engine->registerFunction("writebyte", "memory.writebyte", "writebyte(segment, offset, value)",
        "Write a single byte to memory at segment:offset");
    autocomplete_engine->registerFunction("readword", "memory.readword", "readword(segment, offset)",
        "Read a 16-bit word from memory at segment:offset");
    autocomplete_engine->registerFunction("writeword", "memory.writeword", "writeword(segment, offset, value)",
        "Write a 16-bit word to memory at segment:offset");
    autocomplete_engine->registerFunction("readdword", "memory.readdword", "readdword(segment, offset)",
        "Read a 32-bit double word from memory at segment:offset");
    autocomplete_engine->registerFunction("writedword", "memory.writedword", "writedword(segment, offset, value)",
        "Write a 32-bit double word to memory at segment:offset");
    autocomplete_engine->registerFunction("read_u8", "memory.read_u8", "read_u8(address)", "Read unsigned 8-bit value");
    autocomplete_engine->registerFunction("read_s8", "memory.read_s8", "read_s8(address)", "Read signed 8-bit value");
    autocomplete_engine->registerFunction("read_u16_le", "memory.read_u16_le", "read_u16_le(address)", "Read 16-bit little-endian");
    autocomplete_engine->registerFunction("read_u16_be", "memory.read_u16_be", "read_u16_be(address)", "Read 16-bit big-endian");
    autocomplete_engine->registerFunction("write_u8", "memory.write_u8", "write_u8(address, value)", "Write unsigned 8-bit value");
    autocomplete_engine->registerFunction("write_s8", "memory.write_s8", "write_s8(address, value)", "Write signed 8-bit value");
    autocomplete_engine->registerFunction("write_u16_le", "memory.write_u16_le", "write_u16_le(address, value)", "Write 16-bit little-endian");
    autocomplete_engine->registerFunction("write_u16_be", "memory.write_u16_be", "write_u16_be(address, value)", "Write 16-bit big-endian");

    // Extended CPU API functions
    autocomplete_engine->registerFunction("get_ax", "cpu.get_ax", "get_ax()", "Get the value of the AX register");
    autocomplete_engine->registerFunction("get_bx", "cpu.get_bx", "get_bx()", "Get the value of the BX register");
    autocomplete_engine->registerFunction("get_cx", "cpu.get_cx", "get_cx()", "Get the value of the CX register");
    autocomplete_engine->registerFunction("get_dx", "cpu.get_dx", "get_dx()", "Get the value of the DX register");
    autocomplete_engine->registerFunction("get_al", "cpu.get_al", "get_al()", "Get the value of the AL register");
    autocomplete_engine->registerFunction("get_ah", "cpu.get_ah", "get_ah()", "Get the value of the AH register");
    autocomplete_engine->registerFunction("get_bl", "cpu.get_bl", "get_bl()", "Get the value of the BL register");
    autocomplete_engine->registerFunction("get_bh", "cpu.get_bh", "get_bh()", "Get the value of the BH register");
    autocomplete_engine->registerFunction("get_si", "cpu.get_si", "get_si()", "Get the value of the SI register");
    autocomplete_engine->registerFunction("get_di", "cpu.get_di", "get_di()", "Get the value of the DI register");
    autocomplete_engine->registerFunction("get_sp", "cpu.get_sp", "get_sp()", "Get the value of the SP register");
    autocomplete_engine->registerFunction("get_bp", "cpu.get_bp", "get_bp()", "Get the value of the BP register");
    autocomplete_engine->registerFunction("get_ip", "cpu.get_ip", "get_ip()", "Get the value of the IP register");
    autocomplete_engine->registerFunction("get_cs", "cpu.get_cs", "get_cs()", "Get the value of the CS register");
    autocomplete_engine->registerFunction("get_ds", "cpu.get_ds", "get_ds()", "Get the value of the DS register");
    autocomplete_engine->registerFunction("get_es", "cpu.get_es", "get_es()", "Get the value of the ES register");
    autocomplete_engine->registerFunction("get_ss", "cpu.get_ss", "get_ss()", "Get the value of the SS register");
    autocomplete_engine->registerFunction("get_flags", "cpu.get_flags", "get_flags()", "Get the value of the FLAGS register");

    autocomplete_engine->registerFunction("set_ax", "cpu.set_ax", "set_ax(value)", "Set the value of the AX register");
    autocomplete_engine->registerFunction("set_bx", "cpu.set_bx", "set_bx(value)", "Set the value of the BX register");
    autocomplete_engine->registerFunction("set_cx", "cpu.set_cx", "set_cx(value)", "Set the value of the CX register");
    autocomplete_engine->registerFunction("set_dx", "cpu.set_dx", "set_dx(value)", "Set the value of the DX register");
    autocomplete_engine->registerFunction("set_al", "cpu.set_al", "set_al(value)", "Set the value of the AL register");
    autocomplete_engine->registerFunction("set_ah", "cpu.set_ah", "set_ah(value)", "Set the value of the AH register");
    autocomplete_engine->registerFunction("set_si", "cpu.set_si", "set_si(value)", "Set the value of the SI register");
    autocomplete_engine->registerFunction("set_di", "cpu.set_di", "set_di(value)", "Set the value of the DI register");
    autocomplete_engine->registerFunction("set_sp", "cpu.set_sp", "set_sp(value)", "Set the value of the SP register");
    autocomplete_engine->registerFunction("set_bp", "cpu.set_bp", "set_bp(value)", "Set the value of the BP register");

    // Emulator control functions
    autocomplete_engine->registerFunction("pause", "emu.pause", "pause()", "Pause emulation");
    autocomplete_engine->registerFunction("unpause", "emu.unpause", "unpause()", "Resume emulation");
    autocomplete_engine->registerFunction("frameadvance", "emu.frameadvance", "frameadvance()", "Advance one frame while paused");
    autocomplete_engine->registerFunction("framecount", "emu.framecount", "framecount()", "Get current frame number");
    autocomplete_engine->registerFunction("setspeed", "emu.setspeed", "setspeed(multiplier)", "Set emulation speed multiplier");
    autocomplete_engine->registerFunction("getspeed", "emu.getspeed", "getspeed()", "Get current emulation speed multiplier");
    autocomplete_engine->registerFunction("throttle", "emu.throttle", "throttle(enabled)", "Enable/disable frame rate throttling");
    autocomplete_engine->registerFunction("getfps", "emu.getfps", "getfps()", "Get current frames per second");
    autocomplete_engine->registerFunction("lagcount", "emu.lagcount", "lagcount()", "Get lag frame count");

    // Debug functions
    autocomplete_engine->registerFunction("setbreakpoint", "debug.setbreakpoint", "setbreakpoint(address)",
        "Set breakpoint at memory address");
    autocomplete_engine->registerFunction("removebreakpoint", "debug.removebreakpoint", "removebreakpoint(address)",
        "Remove breakpoint at memory address");
    autocomplete_engine->registerFunction("print", "debug.print", "print(message)", "Print debug message to console");
    autocomplete_engine->registerFunction("log", "debug.log", "log(message)", "Log debug message");
    autocomplete_engine->registerFunction("addlogpoint", "debug.addlogpoint", "addlogpoint(segment, offset, name)",
        "Add a log point at specific address");
    autocomplete_engine->registerFunction("removelogpoint", "debug.removelogpoint", "removelogpoint(segment, offset)",
        "Remove log point at specific address");

    // Breakpoint management
    autocomplete_engine->registerFunction("add", "breakpoint.add", "add(address, condition)", "Add conditional breakpoint");
    autocomplete_engine->registerFunction("remove", "breakpoint.remove", "remove(address)", "Remove breakpoint");
    autocomplete_engine->registerFunction("enable", "breakpoint.enable", "enable(address)", "Enable breakpoint");
    autocomplete_engine->registerFunction("disable", "breakpoint.disable", "disable(address)", "Disable breakpoint");
    autocomplete_engine->registerFunction("list", "breakpoint.list", "list()", "List all breakpoints");
    autocomplete_engine->registerFunction("clear", "breakpoint.clear", "clear()", "Clear all breakpoints");
    autocomplete_engine->registerFunction("getSymbolicBreakpoints", "symbols.getSymbolicBreakpoints", "getSymbolicBreakpoints()", "List symbolic breakpoints");
    autocomplete_engine->registerFunction("addSymbolicBreakpoint", "symbols.addSymbolicBreakpoint", "addSymbolicBreakpoint(symbol, offset, desc?, enabled?)", "Add a symbolic breakpoint with optional description/enabled");
    autocomplete_engine->registerFunction("removeSymbolicBreakpoint", "symbols.removeSymbolicBreakpoint", "removeSymbolicBreakpoint(index)", "Remove symbolic breakpoint by index");
    autocomplete_engine->registerFunction("toggleSymbolicBreakpoint", "symbols.toggleSymbolicBreakpoint", "toggleSymbolicBreakpoint(index, enabled)", "Enable/disable a symbolic breakpoint");
    autocomplete_engine->registerFunction("saveSymbolicBreakpoints", "symbols.saveSymbolicBreakpoints", "saveSymbolicBreakpoints(filename)", "Save symbolic breakpoints to file");
    autocomplete_engine->registerFunction("loadSymbolicBreakpoints", "symbols.loadSymbolicBreakpoints", "loadSymbolicBreakpoints(filename)", "Load symbolic breakpoints from file");

    // Save state functions
    autocomplete_engine->registerFunction("save", "savestate.save", "save(slot)", "Save state to slot");
    autocomplete_engine->registerFunction("load", "savestate.load", "load(slot)", "Load state from slot");
    autocomplete_engine->registerFunction("savefile", "savestate.savefile", "savefile(filename)", "Save state to file");
    autocomplete_engine->registerFunction("loadfile", "savestate.loadfile", "loadfile(filename)", "Load state from file");

    // Input recording functions  
    autocomplete_engine->registerFunction("record", "input.record", "record()", "Start input recording");
    autocomplete_engine->registerFunction("playback", "input.playback", "playback()", "Start input playback");
    autocomplete_engine->registerFunction("stop", "input.stop", "stop()", "Stop recording/playback");
    autocomplete_engine->registerFunction("get", "input.get", "get()", "Get current input state");
    autocomplete_engine->registerFunction("set", "input.set", "set(inputdata)", "Set input state");

    // Frame control functions
    autocomplete_engine->registerFunction("setspeed", "frame.setspeed", "setspeed(mode, multiplier)", "Set frame speed");
    autocomplete_engine->registerFunction("getspeed", "frame.getspeed", "getspeed()", "Get current frame speed");
    autocomplete_engine->registerFunction("advance", "frame.advance", "advance(count)", "Advance frame count");
    autocomplete_engine->registerFunction("getcount", "frame.getcount", "getcount()", "Get frame count");

    // GUI overlay functions
    autocomplete_engine->registerFunction("drawtext", "gui.drawtext", "drawtext(x, y, text, color)", "Draw text overlay");
    autocomplete_engine->registerFunction("drawrect", "gui.drawrect", "drawrect(x, y, width, height, color)", "Draw rectangle");
    autocomplete_engine->registerFunction("drawline", "gui.drawline", "drawline(x1, y1, x2, y2, color)", "Draw line");
    autocomplete_engine->registerFunction("clear", "gui.clear", "clear()", "Clear GUI overlay");
    autocomplete_engine->registerFunction("tohex", "gui.tohex", "tohex(value, digits)", "Convert number to hex string");
    autocomplete_engine->registerFunction("todecimal", "gui.todecimal", "todecimal(value)", "Convert to decimal string");
    autocomplete_engine->registerFunction("tofloat", "gui.tofloat", "tofloat(value, precision)", "Convert to float string");

    // Event system functions
    autocomplete_engine->registerFunction("register", "event.register", "register(eventname, callback)", "Register event callback");
    autocomplete_engine->registerFunction("unregister", "event.unregister", "unregister(eventname)", "Unregister event callback");
    autocomplete_engine->registerFunction("fire", "event.fire", "fire(eventname, data)", "Fire custom event");

    // Common Lua built-ins that are frequently used
    autocomplete_engine->registerFunction("print", "print", "print(...)", "Print values to console");
    autocomplete_engine->registerFunction("type", "type", "type(value)", "Get type of value");
    autocomplete_engine->registerFunction("tostring", "tostring", "tostring(value)", "Convert value to string");
    autocomplete_engine->registerFunction("tonumber", "tonumber", "tonumber(value)", "Convert value to number");
    autocomplete_engine->registerFunction("pairs", "pairs", "pairs(table)", "Iterate over table key-value pairs");
    autocomplete_engine->registerFunction("ipairs", "ipairs", "ipairs(table)", "Iterate over array indices");
    autocomplete_engine->registerFunction("next", "next", "next(table, key)", "Get next table element");
    autocomplete_engine->registerFunction("getmetatable", "getmetatable", "getmetatable(object)", "Get object metatable");
    autocomplete_engine->registerFunction("setmetatable", "setmetatable", "setmetatable(table, metatable)", "Set table metatable");

    // String library functions  
    autocomplete_engine->registerFunction("format", "string.format", "string.format(formatstring, ...)", "Format string with printf-style");
    autocomplete_engine->registerFunction("len", "string.len", "string.len(s)", "Get string length");
    autocomplete_engine->registerFunction("sub", "string.sub", "string.sub(s, i, j)", "Get substring");
    autocomplete_engine->registerFunction("find", "string.find", "string.find(s, pattern)", "Find pattern in string");
    autocomplete_engine->registerFunction("match", "string.match", "string.match(s, pattern)", "Match pattern in string");
    autocomplete_engine->registerFunction("gsub", "string.gsub", "string.gsub(s, pattern, repl)", "Global substitute in string");
    autocomplete_engine->registerFunction("upper", "string.upper", "string.upper(s)", "Convert string to uppercase");
    autocomplete_engine->registerFunction("lower", "string.lower", "string.lower(s)", "Convert string to lowercase");

    // Math library functions
    autocomplete_engine->registerFunction("abs", "math.abs", "math.abs(x)", "Absolute value");
    autocomplete_engine->registerFunction("floor", "math.floor", "math.floor(x)", "Floor function");
    autocomplete_engine->registerFunction("ceil", "math.ceil", "math.ceil(x)", "Ceiling function");
    autocomplete_engine->registerFunction("max", "math.max", "math.max(x, ...)", "Maximum value");
    autocomplete_engine->registerFunction("min", "math.min", "math.min(x, ...)", "Minimum value");
    autocomplete_engine->registerFunction("random", "math.random", "math.random(m, n)", "Random number");
    autocomplete_engine->registerFunction("sqrt", "math.sqrt", "math.sqrt(x)", "Square root");
    autocomplete_engine->registerFunction("sin", "math.sin", "math.sin(x)", "Sine function");
    autocomplete_engine->registerFunction("cos", "math.cos", "math.cos(x)", "Cosine function");

    log_info("Autocomplete engine populated with " + std::to_string(autocomplete_engine->getRegistrySize()) + " functions");
}

std::string LuaEngine::getHistoryFilePath() {
    // Get user's home directory for storing persistent data
    std::string home_dir;

#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if(!home) home = getenv("HOMEDRIVE");
    if(home) home_dir = home;
    if(!home_dir.empty() && getenv("HOMEPATH")) {
        home_dir += getenv("HOMEPATH");
    }
#else
    const char* home = getenv("HOME");
    if(home) home_dir = home;
#endif

    if(home_dir.empty()) {
        // Fallback to current directory
        home_dir = ".";
    }

    return home_dir + "/.dosbox-x-lua-history";
}

void LuaEngine::saveCommandHistory(const std::vector<std::string>& history) {
    try {
        std::string history_file = getHistoryFilePath();
        std::ofstream file(history_file);

        if(file.is_open()) {
            for(const auto& command : history) {
                // Escape newlines and save each command on its own line
                std::string escaped_command = command;
                size_t pos = 0;
                while((pos = escaped_command.find('\n', pos)) != std::string::npos) {
                    escaped_command.replace(pos, 1, "\\n");
                    pos += 2;
                }
                file << escaped_command << "\n";
            }
            file.close();
            log_info("Command history saved to: " + history_file);
        }
        else {
            log_warning("Failed to save command history to: " + history_file);
        }
    }
    catch(const std::exception& e) {
        log_error("Error saving command history: " + std::string(e.what()));
    }
}

std::vector<std::string> LuaEngine::loadCommandHistory() {
    std::vector<std::string> history;

    try {
        std::string history_file = getHistoryFilePath();
        std::ifstream file(history_file);

        if(file.is_open()) {
            std::string line;
            while(std::getline(file, line)) {
                if(!line.empty()) {
                // Unescape newlines
                size_t pos = 0;
                while((pos = line.find("\\\\n", pos)) != std::string::npos) {
                    line.replace(pos, 2, "\n");
                    pos += 1;
                }
                history.push_back(line);
            }
        }
            file.close();

            // Limit history size
            if(history.size() > 100) {
                history.erase(history.begin(), history.end() - 100);
            }

            log_info("Loaded " + std::to_string(history.size()) + " commands from history");
        }
    }
    catch(const std::exception& e) {
        log_error("Error loading command history: " + std::string(e.what()));
    }

    return history;
}



// Symbol and MASM .map file support is now handled by symbol_manager.cpp
