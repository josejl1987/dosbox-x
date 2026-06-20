#ifndef SAVESTATE_MANAGER_H
#define SAVESTATE_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <functional>
#include <mutex>
#include <chrono>

namespace SaveStateManager {

// Save state result codes
enum SaveStateResult {
    SAVESTATE_SUCCESS = 0,
    SAVESTATE_ERROR_INVALID_SLOT,
    SAVESTATE_ERROR_FILE_ACCESS,
    SAVESTATE_ERROR_INVALID_DATA,
    SAVESTATE_ERROR_COMPRESSION_FAILED,
    SAVESTATE_ERROR_SYSTEM_NOT_READY,
    SAVESTATE_ERROR_SCRIPT_DATA_FAILED
};

// Save state metadata
struct SaveStateMetadata {
    std::string timestamp;
    std::string program_name;
    std::string dosbox_version;
    uint64_t total_cycles;
    uint32_t memory_size;
    std::string machine_type;
    std::string video_mode;
    uint32_t checksum;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
};

// Save state information
struct SaveStateInfo {
    int slot;
    std::string name;
    std::string filepath;
    SaveStateMetadata metadata;
    bool is_valid;
};

// Lua script persistent data
struct LuaScriptData {
    std::map<std::string, std::string> global_variables;
    std::vector<uint8_t> binary_data;
    std::string script_state;
    uint32_t checksum;
};

// Save state manager class
class Manager {
private:
    std::string save_directory;
    int quick_save_slot;
    int compression_level;
    bool auto_save_enabled;
    int auto_save_interval;
    int auto_save_start_slot;
    int auto_save_slot_count;
    int auto_save_frame_counter;
    int current_auto_save_slot;
    
    std::mutex save_state_mutex;
    
    // Callbacks
    std::function<void(SaveStateResult, int, const std::string&)> save_callback;
    std::function<void(SaveStateResult, int, const std::string&)> load_callback;
    
    // Helper methods
    std::string GetSlotFilePath(int slot);
    std::string GetNamedFilePath(const std::string& name);
    SaveStateResult WriteMetadata(const std::string& filepath, const SaveStateMetadata& metadata);
    SaveStateResult ReadMetadata(const std::string& filepath, SaveStateMetadata& metadata);
    SaveStateResult CompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    SaveStateResult DecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    uint32_t CalculateChecksum(const std::vector<uint8_t>& data);
    SaveStateMetadata CreateMetadata();
    bool EnsureSaveDirectory();
    void TriggerSaveCallback(SaveStateResult result, int slot, const std::string& name);
    void TriggerLoadCallback(SaveStateResult result, int slot, const std::string& name);
    std::string GetDefaultSaveDirectory();
    std::string GetSaveStateExtension() { return ".dbxsave"; }
    
public:
    Manager();
    ~Manager();
    
    // Initialization and cleanup
    bool Initialize();
    void Shutdown();
    
    // Directory management
    void SetSaveDirectory(const std::string& directory);
    std::string GetSaveDirectory() const;
    
    // Core save/load functionality
    SaveStateResult SaveState(int slot, const std::string& comment = "");
    SaveStateResult SaveState(const std::string& name, const std::string& comment = "");
    SaveStateResult LoadState(int slot);
    SaveStateResult LoadState(const std::string& name);
    
    // Quick save/load
    SaveStateResult QuickSave();
    SaveStateResult QuickLoad();
    void SetQuickSaveSlot(int slot);
    int GetQuickSaveSlot() const;
    
    // Save state management
    std::vector<SaveStateInfo> GetSaveStateList();
    
    // Lua script data persistence
    SaveStateResult SaveScriptData(const LuaScriptData& data);
    SaveStateResult LoadScriptData(LuaScriptData& data);
    
    // Auto-save functionality
    void EnableAutoSave(bool enable, int interval_frames = 3600);
    bool IsAutoSaveEnabled() const;
    void SetAutoSaveSlots(int start_slot, int count);
    
    // Configuration
    void SetCompressionLevel(int level);
    int GetCompressionLevel() const;
    
    // Callbacks
    void SetSaveCallback(std::function<void(SaveStateResult, int, const std::string&)> callback) {
        save_callback = callback;
    }
    void SetLoadCallback(std::function<void(SaveStateResult, int, const std::string&)> callback) {
        load_callback = callback;
    }
};

// Global manager instance - managed by LuaEngine
// This pointer is set by LuaEngine to point to its member
extern Manager* g_save_state_manager;

// Global convenience functions
SaveStateResult SaveState(int slot, const std::string& comment = "");
SaveStateResult SaveState(const std::string& name, const std::string& comment = "");
SaveStateResult LoadState(int slot);
SaveStateResult LoadState(const std::string& name);
SaveStateResult QuickSave();
SaveStateResult QuickLoad();

// Initialization functions
bool InitializeSaveStateManager();
void ShutdownSaveStateManager();

} // namespace SaveStateManager

#endif // SAVESTATE_MANAGER_H