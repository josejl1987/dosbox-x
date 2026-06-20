#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

#include "watch_list.h"
#include <functional>

// Forward declaration
namespace LuaEngineDebugTools { class DebuggerSession; }

namespace LuaEngineWatchList {

class WatchWindow {
private:
    WatchList* watch_list_;
    
    // UI state
    bool show_window_;
    bool show_add_dialog_;
    bool show_edit_dialog_;
    bool show_poke_dialog_;
    int selected_watch_index_;
    
    // Add/Edit dialog state
    char address_input_[32];
    char domain_input_[64];
    char notes_input_[256];
    char poke_value_input_[32];
    int size_combo_index_;
    int display_type_combo_index_;
    
    // Poke dialog state
    uint32_t poke_address_;
    std::string poke_domain_;
    LuaEngineRamSearch::WatchSize poke_size_;
    
    // Rendering methods
    void renderMenuBar();
    void renderWatchTable();
    void renderAddDialog();
    void renderEditDialog();
    void renderPokeDialog();
    
    // Helper methods
    void resetAddDialog();
    void resetEditDialog();
    void resetPokeDialog();
    uint32_t parseAddress(const std::string& input);
    uint64_t parseValue(const std::string& input);
    void openAddDialog();
    void openEditDialog(int watch_index);
    void openPokeDialog(uint32_t address, const std::string& domain, LuaEngineRamSearch::WatchSize size);
    
    // Context menu actions
    void onEditWatch(int index);
    void onRemoveWatch(int index);
    void onPokeWatch(int index);
    void onFreezeWatch(int index);
    void onUnfreezeWatch(int index);
    void onDuplicateWatch(int index);
    
public:
    WatchWindow(LuaEngineDebugTools::DebuggerSession* session = nullptr);
    ~WatchWindow();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Main render method
    void render();
    
    // Window control
    void show();
    void hide();
    bool isVisible() const;
    
    // Watch management
    void addWatch(uint32_t address, const std::string& domain, LuaEngineRamSearch::WatchSize size);
    void addWatch(const LuaEngineRamSearch::SearchResult& result, const std::string& domain);
    void removeWatch(uint32_t address, const std::string& domain);
    void clearWatches();
    
    // File operations
    bool saveWatchList(const std::string& filename);
    bool loadWatchList(const std::string& filename);
    
    // Batch operations
    void updateAllWatches();
    void applyAllFrozenValues();
    void unfreezeAllWatches();
    
    // Accessors
    WatchList* getWatchList() { return watch_list_; }
    const WatchList* getWatchList() const { return watch_list_; }
    
    // Statistics
    size_t getWatchCount() const { return watch_list_ ? watch_list_->getWatchCount() : 0; }
    size_t getFrozenCount() const { return watch_list_ ? watch_list_->getFrozenCount() : 0; }
    size_t getChangedCount() const { return watch_list_ ? watch_list_->getChangedCount() : 0; }
    
    // Callbacks for integration
    std::function<void(uint32_t address)> onEditAddressCallback;
    std::function<void(uint32_t address, uint64_t value, LuaEngineRamSearch::WatchSize size, const std::string& domain)> onPokeValueCallback;
    std::function<void(uint32_t address, uint64_t value, LuaEngineRamSearch::WatchSize size, const std::string& domain)> onFreezeValueCallback;
    std::function<void(uint32_t address, const std::string& domain)> onUnfreezeValueCallback;
};

} // namespace LuaEngineWatchList

#endif // WATCH_WINDOW_H