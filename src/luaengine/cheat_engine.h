#ifndef CHEAT_ENGINE_H
#define CHEAT_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include "lua_memory_domains.h"
#include "ram_search_engine.h"  // For WatchSize

namespace LuaEngineCheatEngine {

// Forward declarations
class CheatEngine;

// Cheat types
enum class CheatType {
    STATIC_VALUE,       // Set to fixed value
    POINTER_VALUE,      // Follow pointer chain
    CODE_MODIFICATION,  // Modify assembly code
    CONDITIONAL,        // Apply only when condition met
    INCREASE_DECREASE,  // Increase/decrease by amount
    FREEZE_RANGE        // Freeze range of addresses
};

// Cheat trigger conditions
enum class CheatTrigger {
    ALWAYS,             // Always active
    ON_LOAD,            // Only when loading save state
    ON_FRAME,           // Every frame
    ON_CONDITION,       // When Lua condition is met
    ON_HOTKEY          // When hotkey is pressed
};

// Individual cheat entry
class Cheat {
private:
    uint32_t id_;
    std::string name_;
    std::string description_;
    std::string domain_;
    uint32_t address_;
    uint64_t value_;
    LuaEngineRamSearch::WatchSize size_;
    CheatType type_;
    CheatTrigger trigger_;
    bool enabled_;
    
    // Advanced features
    std::string condition_;                    // Lua expression for conditional cheats
    std::vector<uint32_t> pointer_path_;      // For pointer cheats
    std::vector<uint8_t> original_code_;      // For code modification cheats
    std::vector<uint8_t> modified_code_;      // For code modification cheats
    int64_t delta_value_;                     // For increase/decrease cheats
    uint32_t range_size_;                     // For range cheats
    
    // State tracking
    uint32_t hit_count_;
    uint64_t last_applied_value_;
    bool dirty_;
    
    // References
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    
public:
    Cheat(uint32_t id, const std::string& name, uint32_t address, const std::string& domain, 
          LuaEngineRamSearch::WatchSize size);
    ~Cheat();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Core functionality
    void apply();
    void reset();
    void toggle();
    
    // Configuration
    void setStaticValue(uint64_t value);
    void setPointerPath(const std::vector<uint32_t>& path);
    void setCodeModification(const std::vector<uint8_t>& original, const std::vector<uint8_t>& modified);
    void setCondition(const std::string& lua_condition);
    void setDeltaValue(int64_t delta);
    void setRangeSize(uint32_t size);
    
    // Properties
    void setName(const std::string& name) { name_ = name; }
    void setDescription(const std::string& description) { description_ = description; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setTrigger(CheatTrigger trigger) { trigger_ = trigger; }
    void setType(CheatType type) { type_ = type; }
    
    // Getters
    uint32_t getId() const { return id_; }
    const std::string& getName() const { return name_; }
    const std::string& getDescription() const { return description_; }
    const std::string& getDomain() const { return domain_; }
    uint32_t getAddress() const { return address_; }
    uint64_t getValue() const { return value_; }
    LuaEngineRamSearch::WatchSize getSize() const { return size_; }
    CheatType getType() const { return type_; }
    CheatTrigger getTrigger() const { return trigger_; }
    bool isEnabled() const { return enabled_; }
    
    uint32_t getHitCount() const { return hit_count_; }
    uint64_t getLastAppliedValue() const { return last_applied_value_; }
    bool isDirty() const { return dirty_; }
    
    // Advanced getters
    const std::string& getCondition() const { return condition_; }
    const std::vector<uint32_t>& getPointerPath() const { return pointer_path_; }
    int64_t getDeltaValue() const { return delta_value_; }
    uint32_t getRangeSize() const { return range_size_; }
    
    // Utility
    std::string getValueString() const;
    std::string getTypeString() const;
    std::string getTriggerString() const;
    std::string getStatusString() const;
    
    // Serialization
    std::string serialize() const;
    bool deserialize(const std::string& data);
    
private:
    // Internal helper methods
    uint64_t readValue(uint32_t address) const;
    void writeValue(uint32_t address, uint64_t value);
    uint32_t resolvePointer() const;
    bool evaluateCondition() const;
    void applyStaticValue();
    void applyPointerValue();
    void applyCodeModification();
    void applyConditional();
    void applyIncreaseDecrease();
    void applyFreezeRange();
};

// Cheat engine manager
class CheatEngine {
private:
    std::vector<std::unique_ptr<Cheat>> cheats_;
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    
    // Configuration
    bool enabled_;
    bool auto_save_;
    std::string cheat_file_;
    int apply_interval_ = 1;
    
    // Statistics
    uint32_t next_cheat_id_;
    uint32_t frame_count_;
    uint32_t cheats_applied_this_frame_;
    
    // File format
    static const int FILE_FORMAT_VERSION = 1;
    
public:
    CheatEngine();
    ~CheatEngine();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Cheat management
    uint32_t addCheat(const std::string& name, uint32_t address, const std::string& domain, 
                     LuaEngineRamSearch::WatchSize size);
    void removeCheat(uint32_t id);
    void removeAllCheats();
    void toggleCheat(uint32_t id);
    void enableCheat(uint32_t id, bool enabled);
    
    // Batch operations
    void enableAllCheats(bool enabled);
    void resetAllCheats();
    
    // Core functionality - called every frame
    void applyCheats();
    void applyCheatsByTrigger(CheatTrigger trigger);
    
    // File operations
    bool saveCheatFile(const std::string& filename);
    bool loadCheatFile(const std::string& filename);
    void setAutoSave(bool auto_save) { auto_save_ = auto_save; }
    
    // Search and access
    Cheat* findCheat(uint32_t id);
    const Cheat* findCheat(uint32_t id) const;
    Cheat* findCheatByName(const std::string& name);
    
    // Accessors
    const std::vector<std::unique_ptr<Cheat>>& getCheats() const { return cheats_; }
    size_t getCheatCount() const { return cheats_.size(); }
    
    Cheat* getCheat(size_t index) { 
        return (index < cheats_.size()) ? cheats_[index].get() : nullptr; 
    }
    
    const Cheat* getCheat(size_t index) const { 
        return (index < cheats_.size()) ? cheats_[index].get() : nullptr; 
    }
    
    // Settings
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    void setApplyInterval(int interval);  // Called by DebugToolsManager
    
    // Statistics
    size_t getEnabledCheatCount() const;
    uint32_t getFrameCount() const { return frame_count_; }
    uint32_t getCheatsAppliedThisFrame() const { return cheats_applied_this_frame_; }
    
    // Integration helpers
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
    
    // Hotkey integration
    void registerHotkey(uint32_t cheat_id, const std::string& hotkey);
    void unregisterHotkey(uint32_t cheat_id);
    void processHotkey(const std::string& hotkey);
    
private:
    // Internal helper methods
    uint32_t generateCheatId();
    void markDirty();
    void autoSaveIfNeeded();
    
    // Hotkey mapping
    std::map<std::string, uint32_t> hotkey_map_;
};

} // namespace LuaEngineCheatEngine

#endif // CHEAT_ENGINE_H