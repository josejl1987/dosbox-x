#ifndef CHEAT_WINDOW_H
#define CHEAT_WINDOW_H

#include "cheat_engine.h"
#include <functional>

// Forward declaration
namespace LuaEngineDebugTools { class DebuggerSession; }

namespace LuaEngineCheatEngine {

class CheatWindow {
private:
    CheatEngine* cheat_engine_;
    
    // UI state
    bool show_window_;
    bool show_add_dialog_;
    bool show_edit_dialog_;
    bool show_code_dialog_;
    bool show_pointer_dialog_;
    int selected_cheat_index_;
    
    // Add/Edit dialog state
    char name_input_[64];
    char description_input_[256];
    char address_input_[32];
    char domain_input_[64];
    char value_input_[32];
    char condition_input_[256];
    char delta_input_[32];
    char range_size_input_[32];
    
    // Code dialog state
    char original_code_input_[256];
    char modified_code_input_[256];
    
    // Pointer dialog state
    char pointer_path_input_[256];
    
    // Combo selections
    int size_combo_index_;
    int type_combo_index_;
    int trigger_combo_index_;
    
    // Rendering methods
    void renderMenuBar();
    void renderToolbar();
    void renderCheatTable();
    void renderAddDialog();
    void renderEditDialog();
    void renderCodeDialog();
    void renderPointerDialog();
    
    // Dialog management
    void resetAddDialog();
    void resetEditDialog();
    void resetCodeDialog();
    void resetPointerDialog();
    void openAddDialog();
    void openEditDialog(int cheat_index);
    void openCodeDialog();
    void openPointerDialog();
    
    // Context menu actions
    void onEditCheat(int index);
    void onRemoveCheat(int index);
    void onToggleCheat(int index);
    void onDuplicateCheat(int index);
    void onResetCheat(int index);
    
    // Helper methods
    uint32_t parseAddress(const std::string& input);
    uint64_t parseValue(const std::string& input);
    std::vector<uint8_t> parseHexString(const std::string& input);
    std::vector<uint32_t> parsePointerPath(const std::string& input);
    
    // Utility methods
    std::string formatCheatSummary(const Cheat* cheat);
    void updateCheatFromDialog(Cheat* cheat);
    void fillDialogFromCheat(const Cheat* cheat);
    
public:
    CheatWindow(LuaEngineDebugTools::DebuggerSession* session = nullptr);
    ~CheatWindow();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Main render method
    void render();
    
    // Window control
    void show();
    void hide();
    bool isVisible() const;
    
    // Cheat management
    uint32_t addCheat(const std::string& name, uint32_t address, const std::string& domain, 
                     LuaEngineRamSearch::WatchSize size);
    void removeCheat(uint32_t id);
    void toggleCheat(uint32_t id);
    void clearCheats();
    
    // File operations
    bool saveCheatFile(const std::string& filename);
    bool loadCheatFile(const std::string& filename);
    
    // Batch operations
    void enableAllCheats(bool enabled);
    void resetAllCheats();
    void applyCheats();
    
    // Integration methods
    void freezeAddress(uint32_t address, uint64_t value, const std::string& domain, 
                      LuaEngineRamSearch::WatchSize size);
    void unfreezeAddress(uint32_t address, const std::string& domain);
    
    // Quick creation methods
    uint32_t createStaticCheat(const std::string& name, uint32_t address, const std::string& domain,
                              LuaEngineRamSearch::WatchSize size, uint64_t value);
    uint32_t createPointerCheat(const std::string& name, const std::vector<uint32_t>& pointer_path,
                               const std::string& domain, LuaEngineRamSearch::WatchSize size, uint64_t value);
    uint32_t createCodeCheat(const std::string& name, uint32_t address, const std::string& domain,
                            const std::vector<uint8_t>& original, const std::vector<uint8_t>& modified);
    
    // Accessors
    CheatEngine* getCheatEngine() { return cheat_engine_; }
    const CheatEngine* getCheatEngine() const { return cheat_engine_; }
    
    // Statistics
    size_t getCheatCount() const { return cheat_engine_ ? cheat_engine_->getCheatCount() : 0; }
    size_t getEnabledCheatCount() const { return cheat_engine_ ? cheat_engine_->getEnabledCheatCount() : 0; }
    bool isCheatEngineEnabled() const { return cheat_engine_ && cheat_engine_->isEnabled(); }
    
    // Callbacks for integration
    std::function<void(uint32_t address, uint64_t value, LuaEngineRamSearch::WatchSize size, const std::string& domain)> onFreezeAddressCallback;
    std::function<void(uint32_t address, const std::string& domain)> onUnfreezeAddressCallback;
};

} // namespace LuaEngineCheatEngine

#endif // CHEAT_WINDOW_H