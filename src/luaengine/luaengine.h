#pragma once
#include <config.h>
#include <lua.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <stack>
#include <fstream>
#include <thread>
#include <mutex>
#include <memory>
#include <functional>
#include <map>
#include <unordered_map>
#include <atomic>
#include <queue>
#include <condition_variable>

#define SOL_ALL_SAFETIES_ON  1
#include "../../vs/lua/sol/sol.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>     // Ensure Winsock2 before windows.h in any consumer
#include <ws2tcpip.h>
#endif
// Force standalone Asio (no Boost) for LRDB even if build flags miss it
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE 1
#endif
// LRDB remote Lua debugger (header-only)
#include "lrdb/server.hpp"
#include "event_manager.h"
#include "lua_memory_domains.h"
#include "savestate_manager.h"
// #include "input_recorder.h" // Removed - input recorder system disabled
#include "frame_control.h"
#include "autocomplete.h"
// #include "gui_overlay.h" // Removed - GUI overlay system disabled
#include "gui_windows.h"
#include "symbol_manager.h"
#include "debug_logger.h"

// Forward declarations
class Section;
class CBreakpoint;

// UI State structure for ImGui integration
struct LuaEngineUIState {
    std::atomic<float> fps{0.0f};  // Lock-free FPS tracking
    std::mutex mtx;
    std::vector<std::string> console_messages;
    std::mutex console_mutex;
};

struct LuaEngine
{
    // UI state for ImGui integration
    LuaEngineUIState uiState;
    std::mutex uiStateMutex;

    // Logger using the new debug_logger system
    std::shared_ptr<LuaEngineDebugLogger::DebugLogger> logger;

    // Compatibility wrapper for old logger API
    void log_info(const std::string& message) {
        if (logger) logger->info("LuaEngine", message);
    }
    void log_error(const std::string& message) {
        if (logger) logger->error("LuaEngine", message);
    }
    void log_debug(const std::string& message) {
        if (logger) logger->debug("LuaEngine", message);
    }
    void log_warning(const std::string& message) {
        if (logger) logger->warning("LuaEngine", message);
    }

    struct MapChange {
        uint8_t map;
        uint8_t unk;
        uint8_t x;
        uint8_t y;
    };

    struct LogPoint {
        uint16_t cs;
        uint16_t ip;
        std::string name;
    };
    
    struct LuaBreakpoint {
        uint16_t cs;
        uint16_t ip;
        std::string condition;  // Lua expression to evaluate
        std::string action;     // Lua code to execute when hit
        bool enabled;
        int hit_count;
        int ignore_count;       // Skip this many hits before triggering
        std::string name;
        bool stop_on_hit;      // Whether to pause emulator when hit
    };

    struct CodePos {
        uint16_t cs;
        uint16_t ip;
    };

  
    LuaEngine() {
        // Initialize basic logger wrapper
        logger = std::make_shared<LuaEngineDebugLogger::DebugLogger>();
    };
    
    // Destructor to ensure proper cleanup
    ~LuaEngine() {
        Shutdown();
    }

    sol::state lua;
    // LRDB integration state
    std::unique_ptr<lrdb::server> lrdb_server_;
    bool lrdb_active_ = false;
    int lrdb_port_ = 21110;
    int luaexiterrorcount = 8;

    // Are we running any code right now?
    char* luaScriptName = NULL;

    // Are we running any code right now?
    // THREAD-SAFE: Read from CPU thread, written from main thread
    std::atomic<int> luaRunning{0};

    // True at the frame boundary, false otherwise.
    int frameBoundary = 0;

    std::vector<LogPoint> logpoints;

    // PHASE 2 OPTIMIZATION: O(1) breakpoint lookup using hash map
    // Key is physical address: (cs << 4) + ip
    std::unordered_map<uint32_t, LuaBreakpoint> breakpoints;

    // Helper function to compute breakpoint key from cs:ip
    static inline uint32_t makeBreakpointKey(uint16_t cs, uint16_t ip) {
        return (static_cast<uint32_t>(cs) << 4) + ip;
    }

    // Thread safety for breakpoints and logpoints
    mutable std::mutex breakpoints_mutex_;
    mutable std::mutex logpoints_mutex_;

    // Hot-reload functionality
    std::string autostart_script_path;
    time_t last_script_modified;
    std::chrono::steady_clock::time_point last_hotreload_check;

    // Game-specific hooks and templates
    std::map<std::string, std::string> game_templates;
    std::vector<std::string> active_game_hooks;

    // Frame skipping for performance optimization
    uint64_t frame_counter = 0;
    uint64_t instruction_counter = 0;

    // Hook control flags
    // THREAD-SAFE: Read from CPU thread, written from main thread
    std::atomic<bool> needs_instruction_hooks{false};  // Only true when breakpoints/debugging active
    std::chrono::steady_clock::time_point last_true_frame_time;
    
    // Performance monitoring
    struct PerformanceStats {
        uint64_t total_frame_calls = 0;
        uint64_t total_lua_time_us = 0;
        uint64_t breakpoint_hits = 0;
        uint64_t script_executions = 0;
        uint64_t memory_operations = 0;
        uint64_t max_frame_time_us = 0;
        uint64_t min_frame_time_us = UINT64_MAX;
        std::chrono::steady_clock::time_point start_time;
    } perf_stats;
    std::mutex perf_mutex_;
    
    // Sol2 performance optimization caches
    struct Sol2PerformanceCache {
        // Cached table references for reuse
        sol::table cached_memory_result;
        sol::table cached_cpu_state;
        sol::table cached_debug_info;
        sol::table cached_address_info;         // For debug.get_current_address
        sol::table cached_breakpoint_list;      // For breakpoint.list results
        sol::table cached_breakpoint_info;      // For individual breakpoint data
        sol::table cached_savestate_info;       // For savestate operations
        
        // Cached function references for hot paths
        sol::protected_function cached_frame_callback;
        sol::protected_function cached_memory_callback;
        bool has_frame_callback = false;
        bool has_memory_callback = false;
        
        // Performance mode flags
        bool fast_memory_access = false;
        bool fast_cpu_access = false;
        bool minimal_error_checking = false;
        bool fast_debug_access = false;         // New flag for debug operations
        bool fast_breakpoint_access = false;    // New flag for breakpoint operations
        
        // Pre-allocated buffers for frequent operations
        std::vector<uint8_t> temp_memory_buffer;
        std::string temp_string_buffer;
        std::string temp_hex_buffer;            // For hex dump operations
        
        // Multi-threading support for performance
        std::atomic<bool> enable_threading{false};
        std::atomic<size_t> max_worker_threads{4};
        
        // Background task queue
        std::queue<std::function<void()>> background_tasks;
        std::mutex background_tasks_mutex;
        std::condition_variable background_tasks_cv;
        std::vector<std::thread> worker_threads;
        std::atomic<bool> shutdown_threads{false};
    } sol2_cache;
    
    // Interrupt hooking system for comprehensive debugging
    std::map<uint8_t, sol::function> interrupt_hooks;

    // Reverse-engineering run-until target, checked in LuaInstructionHook
    std::atomic<uint32_t> run_until_target_{0};
    sol::protected_function run_until_callback_;
    std::mutex run_until_mutex_;

    // Debug symbol management for enhanced disassembly
    std::map<uint32_t, std::string> debug_symbols;
    
    // Memory watchpoint system for detecting memory access
    struct MemoryWatchpoint {
        enum Type { read, write, execute, read_WRITE };
        uint32_t address;
        int size;
        Type type;
        bool enabled;
        int hit_count;
        std::string description;
    };
    std::vector<MemoryWatchpoint> memory_watchpoints;

    // =========================================================================
    // MANAGER LIFECYCLE AND OWNERSHIP PATTERN
    // =========================================================================
    //
    // LuaEngine owns all manager instances via std::unique_ptr for proper RAII
    // and automatic cleanup. Each manager's initialization typically sets a
    // corresponding global raw pointer for backward compatibility with code
    // that expects singleton-style access (e.g., g_overlay_manager,
    // g_window_manager, g_frame_controller, etc.).
    //
    // Ownership Model:
    // - LuaEngine owns managers via unique_ptr (exclusive ownership)
    // - Global raw pointers are set to these unique_ptr-managed objects
    // - Global pointers are reset to nullptr during shutdown
    //
    // Initialization Order (in LUAENGINE_Init):
    // 1. Create unique_ptr instances for each manager
    // 2. Call initialize() on each manager (which sets global pointer)
    // 3. Connect managers that depend on each other
    //
    // Shutdown Order (in LUAENGINE_Shutdown):
    // - Managers automatically destroyed when unique_ptrs reset
    // - Each manager's destructor should reset its global pointer to nullptr
    //
    // WARNING: Do not access global pointers after LuaEngine is destroyed,
    // as they will be dangling pointers. Always check for nullptr.
    // =========================================================================

    // Event management system
    std::unique_ptr<LuaEngineEvents::EventManager> event_manager;

    // Input recording system removed - system disabled
    // std::unique_ptr<LuaEngineInput::InputRecorder> input_recorder;
    // std::unique_ptr<LuaEngineInput::PlaybookManager> playbook_manager;

    // Autocomplete engine for Lua console
    std::unique_ptr<LuaAutocomplete::AutocompleteEngine> autocomplete_engine;

    // Frame control system
    std::unique_ptr<LuaEngineFrameControl::FrameController> frame_controller;

    // GUI window management
    std::unique_ptr<LuaEngineGUIWindows::WindowManager> window_manager;

    // Save state management
    std::unique_ptr<SaveStateManager::Manager> save_state_manager;

    // Symbol debugging
    std::unique_ptr<LuaEngineSymbols::SymbolManager> symbol_manager;


    // The execution speed we're running at.
    enum { SPEED_NORMAL, SPEED_NOTHROTTLE, SPEED_TURBO, SPEED_MAXIMUM } speedmode = SPEED_NORMAL;

    // Rerecord count skip mode
    int skipRerecords = 0;

    // Used by the registry to find our functions
    const char* frameAdvanceThread = "emu_frameadvance";
    const char* guiCallbackTable = "FCEU.GUI";

    // True if there's a thread waiting to run after a run of frame-advance.
    int frameAdvanceWaiting = 0;

    // We save our pause status in the case of a natural death.
    // int wasPaused = FALSE;

    // Transparency strength. 255=opaque, 0=so transparent it's invisible
    int transparencyModifier = 255;

    bool enableIdalog = false;

    std::deque<CodePos> position;
    std::stack<CodePos> stack;

    // Our zapper.
    int luazapperx = -1;
    int luazappery = -1;
    int luazapperfire = -1;

    // Our joypads.
    uint8_t luajoypads1[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; //x1
    uint8_t luajoypads2[4] = { 0x00, 0x00, 0x00, 0x00 }; //0x
    /* Crazy logic stuff.
        11 - true		01 - pass-through (default)
        00 - false		10 - invert					*/

    enum { GUI_USED_SINCE_LAST_DISPLAY, GUI_USED_SINCE_LAST_FRAME, GUI_CLEAR } gui_used = GUI_CLEAR;
    uint8_t* gui_data = NULL;
    int gui_saw_current_palette = 0;

    int numTries;
    std::shared_ptr<sol::state_view> sol_state;
    // number of registered memory functions (1 per hooked byte)
    unsigned int numMemHooks;

    char* rawToCString(lua_State* L, int idx = 0);
    const char* toCString(lua_State* L, int idx = 0);
    bool forceChangeMao = false;
    MapChange mc = { 0x41,0x2,0x8,0x18 };

    int exitScheduled = 0;


    int LoadCode(const char* filename, const char* arg);
    int ExecuteCode(const std::string& code);


    enum class X86Registers {
        REGI_AX, REGI_CX, REGI_DX, REGI_BX,
        REGI_SP, REGI_BP, REGI_SI, REGI_DI,
        REGI_AL, REGI_CL, REGI_DL, REGI_BL,
        REGI_AH, REGI_CH, REGI_DH, REGI_BH,
        REGI_EAX, REGI_ECX, REGI_EDX, REGI_EBX,
        REGI_ESP, REGI_EBP, REGI_ESI, REGI_EDI,
        REGI_IP, REGI_EIP, REGI_FLAGS, REGI_CS, REGI_DS, REGI_SS, REGI_ES

    };
    enum AddressLabel {
        BZ_EXEC_LOAD,
        FUN_3000_1396,
        FUN_3000_1401,
        PLAYBGM,
        BZ_LOAD_LIB,
        NORMAL_LOAD_LIB,
        UNPACK_CALL,
        NORMAL_LOAD,
        NORMAL_SAVE,
        LAB_3000_03d5,
        LAB_3000_03f8,
        CHECK_DISK_NUMBER2,
        BZ_LOAD_LIB2,
        LAB_3000_07e7,
        LAB_3000_0ba1,
        LAB_3000_0c3d,
        LAB_3000_0cda,
        FUN_3000_0f8f,
        LAB_3000_0d30,
        LAB_3000_0047
    };


    void debugHook(AddressLabel index);

    void KLBHook();

    void KHDHook();

    void LuaInstructionHook();
    void LuaFrameBoundary();  // True frame boundary hook (60 FPS)
    int emu_frameadvance();

    uint32_t debug_getregistervalue(const X86Registers& reg);
    sol::function loop_coroutine;
    void debug_addlogpoint(uint16_t seg, uint32_t off, const std::string& name);
    void debug_addbreakpoint(uint16_t seg, uint32_t off, bool once);
    void debug_removebreakpoint(uint16_t seg, uint32_t off, bool once);
    void debug_addmembreakpoint(uint16_t seg, uint32_t off);
    void debug_removemembreakpoint(uint16_t seg, uint32_t off);
    void debug_enabledebugger();

    // Initialization function called during DOSBox-X startup
    void LUAENGINE_Init(Section* section = nullptr);
    // Shutdown function to clean up resources
    void Shutdown();
    
    // API registration functions
    void registerMemoryAPI();
    void registerCpuAPI();
    void registerDebugAPI();
    void registerBreakpointAPI();
    void registerReHooksAPI();
    void registerCDLAPI();

    // Reverse-engineering execution control
    void setRunUntilTarget(uint16_t seg, uint32_t off, sol::protected_function callback);
    void clearRunUntilTarget();

    void registerSaveStateAPI();
    void registerHotReloadAPI();
    void registerGameTemplateAPI();
    void registerConsoleAPI();
    void registerPerformanceAPI();
    void registerEventAPI();
    void registerEmuAPI();
    void registerMemoryDomainsAPI();
    // void registerInputRecorderAPI(); // Removed - input recorder system disabled
    // void registerGUIOverlayAPI(); // Removed - GUI overlay system disabled
    void registerFrameControlAPI();
    void registerWindowSystemAPI();
    
    // Sol2 performance optimization methods
    void initializeSol2PerformanceCache();
    void setSol2PerformanceMode(bool fast_memory, bool fast_cpu, bool minimal_errors);
    void cacheFrequentFunctions();
    void optimizeTableReuse();
    void resetSol2Cache();
    
    // Multi-threading performance methods
    void initializeWorkerThreads();
    void shutdownWorkerThreads();
    void enqueueBackgroundTask(std::function<void()> task);
    void workerThreadFunction();
    void setThreadingEnabled(bool enabled);
    void setMaxWorkerThreads(size_t count);
    
    // Hook control methods
    void enableInstructionHooks(bool enable);
    void updateInstructionHookState();
    
    // Breakpoint management
    void addLuaBreakpoint(uint16_t cs, uint16_t ip, const std::string& name,
                         const std::string& condition = "", const std::string& action = "",
                         bool stop_on_hit = true);
    void removeLuaBreakpoint(uint16_t cs, uint16_t ip);
    void enableLuaBreakpoint(uint16_t cs, uint16_t ip, bool enabled);
    bool checkLuaBreakpoint(uint16_t cs, uint16_t ip);
    
    // Hot-reload functionality
    void checkScriptHotReload();
    time_t getFileModificationTime(const std::string& filename);
    
    // Game template functionality
    void initializeGameTemplates();
    void loadGameTemplate(const std::string& game_name);
    std::string generateGameScript(const std::string& template_name, const std::map<std::string, std::string>& params);
    
    // Console command functionality
    void registerConsoleCommands();
    std::string executeLuaConsoleCommand(const std::string& command);
    
    // Performance monitoring
    void initializePerformanceMonitoring();
    void recordFrameTime(uint64_t time_us);
    void recordBreakpointHit();
    void recordScriptExecution();
    void recordMemoryOperation();
    std::string getPerformanceReport();
    
    // DOS interrupt hooks
    void onDOSInterrupt(uint8_t interrupt_num, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx,
                       uint16_t cs, uint16_t ds, uint16_t es, uint16_t ss);
    
    
    // Autocomplete functionality
    void populateAutocompleteAPI();
    
    // Command history persistence
    void saveCommandHistory(const std::vector<std::string>& history);
    std::vector<std::string> loadCommandHistory();
    std::string getHistoryFilePath();
    
    // Breakpoint tracking for enhanced functionality
    std::map<CBreakpoint*, std::string> breakpoint_names_;
    std::map<CBreakpoint*, std::string> breakpoint_conditions_;
    std::map<CBreakpoint*, std::string> breakpoint_actions_;
    std::mutex breakpoint_metadata_mutex_; // Protects the above maps

    // Global instance protection
    mutable std::mutex lua_state_mutex_; // Protects Lua state access across threads
};

// Global instance protection
extern std::mutex g_luaengine_mutex;

/*
 * THREAD SAFETY SYNCHRONIZATION STRATEGY:
 *
 * 1. Global Instance Protection:
 *    - g_luaengine_mutex protects access to the global luaEngine instance
 *    - Use SafeLuaEngineAccess() and SafeLuaEngineAccessVoid() wrappers
 *
 * 2. Breakpoint Protection:
 *    - breakpoints_mutex_ protects the breakpoints vector
 *    - All breakpoint operations (add/remove/check) are synchronized
 *
 * 3. Event Manager Protection:
 *    - callback_mutex protects callback registration/execution
 *    - memory_batch.batch_mutex protects memory event batching
 *    - Proper lock ordering: batch_mutex -> callback_mutex
 *
 * 4. Lua State Protection:
 *    - lua_state_mutex_ protects Sol2 Lua state access
 *    - Critical for script execution and callback invocation
 *
 * 5. Memory Safety:
 *    - Bounds checking in save state operations
 *    - Safe string operations replacing strncpy
 *    - Input validation for buffer operations
 */

// C-style wrapper function for DOSBox section system
void LUA_Init(Section* section);
