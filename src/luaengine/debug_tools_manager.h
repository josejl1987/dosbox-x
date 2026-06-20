#ifndef DEBUG_TOOLS_MANAGER_H
#define DEBUG_TOOLS_MANAGER_H

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
 * Unified manager for all debugging tools in DOSBox-X
 * 
 * This class provides a centralized interface for managing all debugging components:
 * - Memory domain management
 * - RAM search functionality
 * - Watch list management
 * - Hex editor
 * - Cheat engine
 * - Trace logger
 * - Disassembly debugger
 */
class DebugToolsManager {
private:
    // Core components
    std::unique_ptr<LuaEngineDebug::DosBoxCoreDebugger> debug_interface_;
    std::unique_ptr<LuaEngineMemoryDomains::MemoryDomainManager> memory_manager_;
    
    // Search and watch components (backends only)
    std::unique_ptr<LuaEngineRamSearch::RamSearchEngine> ram_search_engine_;
    std::unique_ptr<LuaEngineWatchList::WatchList> watch_list_;
    
    // Trace components (backend only)
    LuaEngineTraceLogger::TraceLogger* trace_logger_;
    
    // Additional debug tools (backend only)
    std::unique_ptr<LuaEngineDebugConfig::DebugConfigManager> config_manager_;
    std::unique_ptr<LuaEngineDebugLogger::DebugLogger> debug_logger_;
    std::unique_ptr<LuaEngineCheatEngine::CheatEngine> cheat_engine_;
    
    // State
    bool initialized_;
    bool tools_visible_;
    
    // Integration methods
    void setupIntegration();
    void connectComponents();
    
    // UI rendering
    void renderDebugMenu();
    
public:
    DebugToolsManager();
    ~DebugToolsManager();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Main interface
    void render();
    void update();
    
    // Window management
    void showAllTools();
    void hideAllTools();
    void toggleToolsVisibility();
    
    // GUI integration functions (for compatibility with imgui_window.cpp)
    void toggleRamSearch();
    void toggleWatchList();
    void toggleHexEditor();
    void toggleTraceLogger();
    void toggleDisassemblyWindow();
    void renderRamSearchWindow();
    void renderWatchListWindow();
    void renderHexEditorWindow();
    void renderTraceLoggerWindow();
    void renderCheatEngineWindow(); // Deprecated: no-op under WindowManager ownership
    void renderDebugToolsMenu();
    void renderDebugToolsHelp();
    
    // Individual tool access
    void showRamSearch();
    void showWatchList();
    void showHexEditor();
    void showTraceLogger();
    void showLogViewer();
    void showConfiguration();
    void showCheatEngine();
    void showDisassemblyWindow();
    
    // Integration callbacks
    void onInstructionExecuted(uint32_t address);
    void onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value);
    void onBreakpointHit(uint32_t address);
    
    // Accessors
    LuaEngineDebug::DosBoxCoreDebugger* getDebugInterface() const { return debug_interface_.get(); }
    LuaEngineMemoryDomains::MemoryDomainManager* getMemoryManager() const { return memory_manager_.get(); }
    LuaEngineRamSearch::RamSearchEngine* getRamSearchEngine() const { return ram_search_engine_.get(); }
    LuaEngineWatchList::WatchList* getWatchList() const { return watch_list_.get(); }
    LuaEngineTraceLogger::TraceLogger* getTraceLogger() const { return trace_logger_; }
    LuaEngineDebugConfig::DebugConfigManager* getConfigManager() const { return config_manager_.get(); }
    LuaEngineDebugLogger::DebugLogger* getDebugLogger() const { return debug_logger_.get(); }
    
    // State
    bool isInitialized() const { return initialized_; }
    bool areToolsVisible() const { return tools_visible_; }
    
    // Utility methods
    void freezeAddress(uint32_t address, uint64_t value, const std::string& domain = "DOS Conventional");
    void addWatch(uint32_t address, const std::string& domain = "DOS Conventional");
    void searchMemory(const std::string& value, LuaEngineRamSearch::SearchOperator op = LuaEngineRamSearch::SearchOperator::EQUAL);
    void goToAddress(uint32_t address);
    void toggleBreakpoint(uint32_t address);
    void startTracing();
    void stopTracing();
    
    // File operations
    bool saveSession(const std::string& filename);
    bool loadSession(const std::string& filename);
    
    // Export functionality
    void exportWatchList(const std::string& filename);
    void exportCheats(const std::string& filename);
    void exportTraceLog(const std::string& filename);
};

} // namespace LuaEngineDebugTools

// Global functions for GUI integration
bool InitializeDebugTools();
void ShutdownDebugTools();
LuaEngineDebugTools::DebugToolsManager* GetDebugToolsManager();

#endif // DEBUG_TOOLS_MANAGER_H
