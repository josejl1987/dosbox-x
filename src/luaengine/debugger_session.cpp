#include "debugger_session.h"
#include "gui_windows.h"
#include "debug_config.h"
#include "core_debug_interface.h"
#include <iostream>

namespace LuaEngineDebugTools {

DebuggerSession::DebuggerSession() = default;

DebuggerSession::~DebuggerSession() {
    shutdown();
}

bool DebuggerSession::initialize() {
    if (initialized_) {
        return true;
    }

    try {
        // Core debug interface
        debug_interface_ = std::make_unique<LuaEngineDebug::DosBoxCoreDebugger>();
        debug_interface_->initialize(nullptr);
        LuaEngineDebug::g_core_debugger = debug_interface_.get();

        // Memory domain manager
        memory_manager_ = std::make_unique<LuaEngineMemoryDomains::MemoryDomainManager>();
        memory_manager_->initializeDomains();

        // RAM search
        ram_search_engine_ = std::make_unique<LuaEngineRamSearch::RamSearchEngine>();
        ram_search_engine_->initialize(memory_manager_.get());

        // Watch list
        watch_list_ = std::make_unique<LuaEngineWatchList::WatchList>();
        watch_list_->initialize(memory_manager_.get());

        // Trace logger — previously borrowed from WindowManager; now owned here
        trace_logger_ = std::make_unique<LuaEngineTraceLogger::TraceLogger>();
        trace_logger_->initialize(debug_interface_.get());

        // Cheat engine
        cheat_engine_ = std::make_unique<LuaEngineCheatEngine::CheatEngine>();
        cheat_engine_->initialize(memory_manager_.get());

        // Config manager with hotkey wiring
        config_manager_ = std::make_unique<LuaEngineDebugConfig::DebugConfigManager>();
        config_manager_->initialize();
        config_manager_->setHotkeyCallback("run_pause", [this]() {
            if (!debug_interface_) return;
            if (debug_interface_->isPaused()) {
                debug_interface_->resume();
            } else {
                debug_interface_->pause();
            }
        });
        config_manager_->setHotkeyCallback("step_into", [this]() {
            if (debug_interface_) debug_interface_->stepInto();
        });
        config_manager_->setHotkeyCallback("step_over", [this]() {
            if (debug_interface_) debug_interface_->stepOver();
        });
        config_manager_->setHotkeyCallback("step_out", [this]() {
            if (debug_interface_) debug_interface_->stepOut();
        });

        // Register Ctrl+Shift window toggle shortcuts as hotkeys
        config_manager_->addHotkey("toggle_memory_search", "Toggle RAM Search", 'S', true, false, true);
        config_manager_->addHotkey("toggle_watch_list", "Toggle Watch List", 'W', true, false, true);
        config_manager_->addHotkey("toggle_hex_editor", "Toggle Hex Editor", 'H', true, false, true);
        config_manager_->addHotkey("toggle_trace_logger", "Toggle Trace Logger", 'T', true, false, true);
        config_manager_->addHotkey("toggle_disassembly", "Toggle Disassembly", 'D', true, false, true);

        // Debug logger
        debug_logger_ = std::make_unique<LuaEngineDebugLogger::DebugLogger>();

        initialized_ = true;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize DebuggerSession: " << e.what() << std::endl;
        return false;
    }
}

void DebuggerSession::shutdown() {
    if (!initialized_) {
        return;
    }

    // Destroy in reverse dependency order; windows are already destroyed by WindowManager
    debug_logger_.reset();
    config_manager_.reset();
    cheat_engine_.reset();
    trace_logger_.reset();
    watch_list_.reset();
    ram_search_engine_.reset();
    memory_manager_.reset();

    // Clear global before resetting the unique_ptr
    LuaEngineDebug::g_core_debugger = nullptr;
    debug_interface_.reset();

    initialized_ = false;
}

void DebuggerSession::update() {
    if (!initialized_) return;

    if (watch_list_) {
        watch_list_->updateAllValues();
        watch_list_->applyAllFrozenValues();
    }

    if (config_manager_) {
        config_manager_->processHotkeys();
    }
}

// Accessors

LuaEngineDebug::DosBoxCoreDebugger* DebuggerSession::debugger() const {
    return debug_interface_.get();
}

LuaEngineMemoryDomains::MemoryDomainManager* DebuggerSession::memory() const {
    return memory_manager_.get();
}

LuaEngineRamSearch::RamSearchEngine* DebuggerSession::ramSearch() const {
    return ram_search_engine_.get();
}

LuaEngineWatchList::WatchList* DebuggerSession::watches() const {
    return watch_list_.get();
}

LuaEngineTraceLogger::TraceLogger* DebuggerSession::tracer() const {
    return trace_logger_.get();
}

LuaEngineCheatEngine::CheatEngine* DebuggerSession::cheats() const {
    return cheat_engine_.get();
}

LuaEngineDebugConfig::DebugConfigManager* DebuggerSession::config() const {
    return config_manager_.get();
}

LuaEngineDebugLogger::DebugLogger* DebuggerSession::logger() const {
    return debug_logger_.get();
}

} // namespace LuaEngineDebugTools

// Global lifecycle functions

std::unique_ptr<LuaEngineDebugTools::DebuggerSession> g_debugger_session;

bool InitializeDebugSession() {
    if (g_debugger_session) {
        return true; // Already initialized
    }

    g_debugger_session = std::make_unique<LuaEngineDebugTools::DebuggerSession>();
    if (!g_debugger_session->initialize()) {
        g_debugger_session.reset();
        return false;
    }
    return true;
}

void ShutdownDebuggerSession() {
    if (g_debugger_session) {
        g_debugger_session->shutdown();
        g_debugger_session.reset();
    }
}

LuaEngineDebugTools::DebuggerSession* GetDebuggerSession() {
    return g_debugger_session.get();
}
