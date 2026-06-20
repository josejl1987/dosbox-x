#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <cstdint>

// Forward declarations
class Section;

namespace SaveStateManager {

// Save state result codes
enum SaveStateResult {
    SAVESTATE_SUCCESS = 0,
    SAVESTATE_ERROR_FILE_ACCESS,
    SAVESTATE_ERROR_INVALID_SLOT,
    SAVESTATE_ERROR_INVALID_DATA,
    SAVESTATE_ERROR_COMPRESSION_FAILED,
    SAVESTATE_ERROR_SYSTEM_NOT_READY,
    SAVESTATE_ERROR_SCRIPT_DATA_FAILED,
    SAVESTATE_ERROR_MEMORY_ALLOCATION
};

// Save state metadata
struct SaveStateMetadata {
    std::string timestamp;      // When the save state was created
    std::string program_name;   // Running program/game name
    std::string dosbox_version; // DOSBox-X version
    uint64_t total_cycles;      // CPU cycles when saved
    uint32_t memory_size;       // Total system memory
    std::string machine_type;   // Machine type (pc98, tandy, etc.)
    std::string video_mode;     // Current video mode
    uint32_t checksum;          // Data integrity checksum
    size_t compressed_size;     // Compressed save state size
    size_t uncompressed_size;   // Uncompressed save state size
};

// Save state information for listing/management
struct SaveStateInfo {
    int slot;                   // Slot number (-1 for named saves)
    std::string name;           // Save state name (for named saves)
    std::string filepath;       // Full path to save state file
    SaveStateMetadata metadata; // Save state metadata
    bool is_valid;              // Whether save state is valid/loadable
    std::string error_message;  // Error message if invalid
};

// Lua script data interface
struct LuaScriptData {
    std::map<std::string, std::string> global_variables;  // Lua global variables
    std::vector<uint8_t> binary_data;                    // Binary script data
    std::string script_state;                            // Serialized script state
    uint32_t checksum;                                   // Data integrity checksum
};

// Save state manager class
class Manager {
public:
    Manager();
    ~Manager();
    
    // Initialization and configuration
    bool Initialize();
    void Shutdown();
    void SetSaveDirectory(const std::string& directory);
    std::string GetSaveDirectory() const;
    
    // Core save/load operations
    SaveStateResult SaveState(int slot, const std::string& comment = "");
    SaveStateResult SaveState(const std::string& name, const std::string& comment = "");
    SaveStateResult LoadState(int slot);
    SaveStateResult LoadState(const std::string& name);
    
    // Quick save/load operations
    SaveStateResult QuickSave();
    SaveStateResult QuickLoad();
    void SetQuickSaveSlot(int slot);
    int GetQuickSaveSlot() const;
    
    // Save state management
    std::vector<SaveStateInfo> GetSaveStateList();
    SaveStateInfo GetSaveStateInfo(int slot);
    SaveStateInfo GetSaveStateInfo(const std::string& name);
    bool DeleteSaveState(int slot);
    bool DeleteSaveState(const std::string& name);
    bool RenameSaveState(int slot, const std::string& new_name);
    bool CopySaveState(int from_slot, int to_slot);
    bool CopySaveState(const std::string& from_name, const std::string& to_name);
    
    // Lua script data persistence
    SaveStateResult SaveScriptData(const LuaScriptData& data);
    SaveStateResult LoadScriptData(LuaScriptData& data);
    bool HasScriptData(int slot);
    bool HasScriptData(const std::string& name);
    
    // Auto-save functionality
    void EnableAutoSave(bool enable, int interval_frames = 3600); // Default: auto-save every minute at 60fps
    bool IsAutoSaveEnabled() const;
    void SetAutoSaveSlots(int start_slot, int count); // Use a range of slots for rotation
    
    // Event callbacks for save state operations
    using SaveStateCallback = std::function<void(SaveStateResult result, int slot, const std::string& name)>;
    void SetSaveCallback(SaveStateCallback callback);
    void SetLoadCallback(SaveStateCallback callback);
    
    // Validation and repair
    bool ValidateSaveState(int slot, std::string& error_message);
    bool ValidateSaveState(const std::string& name, std::string& error_message);
    SaveStateResult RepairSaveState(int slot);
    SaveStateResult RepairSaveState(const std::string& name);
    
    // Utility functions
    std::vector<int> GetUsedSlots();
    std::vector<std::string> GetNamedSaves();
    int FindFreeSlot();
    bool IsSlotUsed(int slot);
    bool IsNameUsed(const std::string& name);
    size_t GetTotalSaveStatesSize();
    void CleanupOldSaveStates(int max_count);
    
    // Compression settings
    void SetCompressionLevel(int level); // 0-9, 0=no compression, 9=max compression
    int GetCompressionLevel() const;
    
    // Static utility functions
    static std::string GetSaveStateExtension() { return ".dxs"; } // DOSBox-X Save
    static std::string GetDefaultSaveDirectory();
    static bool IsSaveStateFile(const std::string& filepath);
    static SaveStateResult ConvertOldSaveState(const std::string& old_path, const std::string& new_path);

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
    
    SaveStateCallback save_callback;
    SaveStateCallback load_callback;
    
    std::mutex save_state_mutex;
    
    // Internal helper methods
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
    
    // Auto-save management
    void UpdateAutoSave();
    int GetNextAutoSaveSlot();
};

// Global save state manager instance
extern std::unique_ptr<Manager> g_save_state_manager;

// Convenience functions
SaveStateResult SaveState(int slot, const std::string& comment = "");
SaveStateResult SaveState(const std::string& name, const std::string& comment = "");
SaveStateResult LoadState(int slot);
SaveStateResult LoadState(const std::string& name);
SaveStateResult QuickSave();
SaveStateResult QuickLoad();

// Initialization function (called from DOSBox-X startup)
bool InitializeSaveStateManager();
void ShutdownSaveStateManager();

// Integration with existing DOSBox-X save state system
void RegisterWithDOSBoxSaveSystem();

} // namespace SaveStateManager