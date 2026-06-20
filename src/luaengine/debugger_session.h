#ifndef DEBUGGER_SESSION_H
#define DEBUGGER_SESSION_H

#include "core_debug_interface.h"
#include "lua_memory_domains.h"
#include "ram_search_engine.h"
#include "watch_list.h"
#include "trace_logger.h"
#include "debug_logger.h"
#include "debug_config.h"
#include "cheat_engine.h"
#include <memory>
#include <string>

namespace LuaEngineDebugTools {

/**
 * Single owner of all debugger backend services.
 *
 * Replaces DebugToolsManager as the one place that creates and owns
 * every backend instance. Windows receive raw pointers via constructor
 * injection; no window embeds its own backend copy.
 */
class DebuggerSession {
public:
    DebuggerSession();
    ~DebuggerSession();

    // Lifecycle
    bool initialize();
    void shutdown();
    void update();

    // Accessors — return nullptr before initialize() or after shutdown()
    LuaEngineDebug::DosBoxCoreDebugger* debugger() const;
    LuaEngineMemoryDomains::MemoryDomainManager* memory() const;
    LuaEngineRamSearch::RamSearchEngine* ramSearch() const;
    LuaEngineWatchList::WatchList* watches() const;
    LuaEngineTraceLogger::TraceLogger* tracer() const;
    LuaEngineCheatEngine::CheatEngine* cheats() const;
    LuaEngineDebugConfig::DebugConfigManager* config() const;
    LuaEngineDebugLogger::DebugLogger* logger() const;

    bool isInitialized() const { return initialized_; }

private:
    std::unique_ptr<LuaEngineDebug::DosBoxCoreDebugger> debug_interface_;
    std::unique_ptr<LuaEngineMemoryDomains::MemoryDomainManager> memory_manager_;
    std::unique_ptr<LuaEngineRamSearch::RamSearchEngine> ram_search_engine_;
    std::unique_ptr<LuaEngineWatchList::WatchList> watch_list_;
    std::unique_ptr<LuaEngineTraceLogger::TraceLogger> trace_logger_;
    std::unique_ptr<LuaEngineCheatEngine::CheatEngine> cheat_engine_;
    std::unique_ptr<LuaEngineDebugConfig::DebugConfigManager> config_manager_;
    std::unique_ptr<LuaEngineDebugLogger::DebugLogger> debug_logger_;
    bool initialized_{false};
};

} // namespace LuaEngineDebugTools

// Global lifecycle — replaces DebugToolsManager globals
extern std::unique_ptr<LuaEngineDebugTools::DebuggerSession> g_debugger_session;

bool InitializeDebugSession();
void ShutdownDebuggerSession();
LuaEngineDebugTools::DebuggerSession* GetDebuggerSession();

#endif // DEBUGGER_SESSION_H
