#ifndef DEBUG_TOOLS_MANAGER_H
#define DEBUG_TOOLS_MANAGER_H

#include <memory>
#include <string>
#include <atomic>
#include <map>
#include "../luaengine/lua_memory_domains.h"

// Forward declarations
namespace LuaEngineRamSearch { class RamSearchEngine; }
namespace LuaEngineWatchList { class WatchList; }
namespace LuaEngineHexEditor { class HexEditor; }
namespace LuaEngineTraceLogger { class TraceLogger; }
namespace LuaEngineMemoryDomains { class MemoryDomainManagerAdapter; }
namespace LuaEngineDebug { class CoreDebugInterface; }

class DebugToolsManager {
public:
    // Window visibility state (similar to LuaEngine pattern)
    struct WindowVisibility {
        bool show_ram_search = false;
        bool show_watch_list = false;
        bool show_hex_editor = false;
        bool show_trace_logger = false;
        
        // Force show overrides
        bool force_show_ram_search = false;
        bool force_show_watch_list = false;
        bool force_show_hex_editor = false;
        bool force_show_trace_logger = false;
        
        // Global override
        bool show_all_debug_tools = false;
    };

private:
    // Core debug components
    std::unique_ptr<LuaEngineRamSearch::RamSearchEngine> ram_search_;
    std::unique_ptr<LuaEngineWatchList::WatchList> watch_list_;
    std::unique_ptr<LuaEngineHexEditor::HexEditor> hex_editor_;
    std::unique_ptr<LuaEngineTraceLogger::TraceLogger> trace_logger_;
    
    // Dependencies
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    LuaEngineDebug::CoreDebugInterface* debug_interface_;
    
    // State management
    WindowVisibility window_visibility_;
    std::atomic<bool> initialized_;
    
    // Error handling
    std::string last_error_;
    
public:
    DebugToolsManager();
    ~DebugToolsManager();
    
    // Initialization
    bool initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr,
                   LuaEngineDebug::CoreDebugInterface* debug_iface);
    void shutdown();
    
    // Window visibility management (following LuaEngine pattern)
    WindowVisibility& getWindowVisibility() { return window_visibility_; }
    const WindowVisibility& getWindowVisibility() const { return window_visibility_; }
    
    // Show all debug tools (similar to LuaEngine::showAllDebugWindows)
    void showAllDebugTools();
    void hideAllDebugTools();
    void resetWindowOverrides();
    
    // Keyboard shortcut handling
    void toggleRamSearch();
    void toggleWatchList();
    void toggleHexEditor();
    void toggleTraceLogger();
    
    // ImGui render methods (called from RenderImGuiFrame)
    void renderRamSearchWindow();
    void renderWatchListWindow();
    void renderHexEditorWindow();
    void renderTraceLoggerWindow();
    
    // Menu integration helpers
    void renderDebugToolsMenu();
    void renderDebugToolsHelp();
    
    // Tool access (for external integration)
    LuaEngineRamSearch::RamSearchEngine* getRamSearch() const { return ram_search_.get(); }
    LuaEngineWatchList::WatchList* getWatchList() const { return watch_list_.get(); }
    LuaEngineHexEditor::HexEditor* getHexEditor() const { return hex_editor_.get(); }
    LuaEngineTraceLogger::TraceLogger* getTraceLogger() const { return trace_logger_.get(); }
    
    // Status
    bool isInitialized() const { return initialized_.load(); }
    const std::string& getLastError() const { return last_error_; }
    
    // Statistics for performance monitoring
    struct ToolStats {
        size_t ram_search_results = 0;
        size_t watch_list_entries = 0;
        size_t hex_editor_modifications = 0;
        size_t trace_logger_events = 0;
    };
    
    ToolStats getStats() const;
    
private:
    // Internal helpers
    bool shouldShowWindow(bool base_visibility, bool force_show, bool global_override) const;
    void handleError(const std::string& error);
    
    // ImGui window rendering helpers
    void renderWindowControls(const std::string& window_name, bool& visibility, bool& force_show);
    void renderToolStatistics();
    
    // Integration with existing systems
    void setupMemoryDomainIntegration();
    void setupDebugInterfaceIntegration();
};

// Global instance (similar to LuaEngine pattern)
extern DebugToolsManager* g_debug_tools_manager;

// Helper functions for external integration
bool InitializeDebugTools();
void ShutdownDebugTools();
DebugToolsManager* GetDebugToolsManager();

#endif // DEBUG_TOOLS_MANAGER_H
