#ifndef RAM_SEARCH_WINDOW_H
#define RAM_SEARCH_WINDOW_H

#include "ram_search_engine.h"
#include <string>
#include <functional>

// Forward declaration
namespace LuaEngineDebugTools { class DebuggerSession; }

namespace LuaEngineRamSearch {

class RamSearchWindow {
private:
    // Core engine — pointer to session-owned instance
    RamSearchEngine* engine_;
    
    // UI state
    SearchOperator selected_operator_;
    CompareType selected_compare_type_;
    uint64_t search_value_;
    WatchSize selected_size_;
    WatchDisplayType selected_display_type_;
    
    // Window state
    bool show_window_;
    size_t results_page_;
    size_t results_per_page_ = 1000;
    static const size_t RESULTS_PER_PAGE = 1000;
    
    // Input buffers
    char search_input_text_[64];
    
    // Rendering methods
    void renderMenuBar();
    void renderControls();
    void renderOperatorSelection();
    void renderResults();
    
    // Event handlers
    void onAddToWatchList(uint32_t address);
    void onPokeValue(uint32_t address);
    void onFreezeValue(uint32_t address, uint64_t value);
    
    // Helper methods
    uint64_t parseSearchValue(const std::string& input);
    
public:
    RamSearchWindow(LuaEngineDebugTools::DebuggerSession* session = nullptr);
    ~RamSearchWindow();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Main render method
    void render();
    
    // Window control
    void show();
    void hide();
    bool isVisible() const;
    
    // Accessors
    RamSearchEngine* getEngine() { return engine_; }
    const RamSearchEngine* getEngine() const { return engine_; }
    
    // Configuration methods called by DebugToolsManager
    void setResultsPerPage(size_t results_per_page);
    
    // Callbacks for integration with other systems
    std::function<void(uint32_t address, WatchSize size, const std::string& domain)> onAddToWatchListCallback;
    std::function<void(uint32_t address, uint64_t value, WatchSize size, const std::string& domain)> onPokeValueCallback;
    std::function<void(uint32_t address, uint64_t value, WatchSize size, const std::string& domain)> onFreezeValueCallback;
};

} // namespace LuaEngineRamSearch

#endif // RAM_SEARCH_WINDOW_H