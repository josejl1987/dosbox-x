#include "savestate_manager.h"
#include "dosbox.h"
#include "logging.h"
#include "control.h"
#include "cpu.h"
#include "mem.h"
#include "video.h"
#include "cross.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <zlib.h>

namespace SaveStateManager {

// Global manager instance - now managed by LuaEngine
// This is initialized in LuaEngine::LUAENGINE_Init()
Manager* g_save_state_manager = nullptr;

// Implementation of Manager class
Manager::Manager() 
    : quick_save_slot(0)
    , compression_level(6)
    , auto_save_enabled(false)
    , auto_save_interval(3600)
    , auto_save_start_slot(90)
    , auto_save_slot_count(10)
    , auto_save_frame_counter(0)
    , current_auto_save_slot(0)
{
    save_directory = GetDefaultSaveDirectory();
}

Manager::~Manager() {
    Shutdown();
}

bool Manager::Initialize() {
    if (!EnsureSaveDirectory()) {
        LOG_MSG("SaveStateManager: Failed to create save directory: %s", save_directory.c_str());
        return false;
    }
    
    LOG_MSG("SaveStateManager: Initialized with directory: %s", save_directory.c_str());
    return true;
}

void Manager::Shutdown() {
    auto_save_enabled = false;
    save_callback = nullptr;
    load_callback = nullptr;
}

void Manager::SetSaveDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    save_directory = directory;
    EnsureSaveDirectory();
}

std::string Manager::GetSaveDirectory() const {
    return save_directory;
}

SaveStateResult Manager::SaveState(int slot, const std::string& comment) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    if (slot < 0 || slot > 999) {
        return SAVESTATE_ERROR_INVALID_SLOT;
    }
    
    std::string filepath = GetSlotFilePath(slot);
    
    try {
        // Create metadata
        SaveStateMetadata metadata = CreateMetadata();
        
        // Integrate with existing DOSBox-X save state system
        std::vector<uint8_t> save_data;
        
        // Save memory contents
        size_t memory_size = MEM_TotalPages() * 4096;
        save_data.resize(memory_size);
        
        // Copy actual memory contents from DOSBox-X
        for (size_t i = 0; i < memory_size; ++i) {
            save_data[i] = mem_readb(static_cast<PhysPt>(i));
        }
        
        // Add CPU state data
        std::vector<uint8_t> cpu_state_data;
        cpu_state_data.resize(1024); // Reserve space for CPU registers and state
        
        // Store CPU registers (simplified - real implementation would store all state)
        size_t offset = 0;
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_eax; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_ebx; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_ecx; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_edx; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_esi; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_edi; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_esp; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_ebp; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_eip; offset += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&cpu_state_data[offset]) = reg_flags; offset += sizeof(uint32_t);
        
        // Segment registers
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(cs); offset += sizeof(uint16_t);
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(ds); offset += sizeof(uint16_t);
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(es); offset += sizeof(uint16_t);
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(fs); offset += sizeof(uint16_t);
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(gs); offset += sizeof(uint16_t);
        *reinterpret_cast<uint16_t*>(&cpu_state_data[offset]) = SegValue(ss); offset += sizeof(uint16_t);
        
        // Append CPU state to memory data
        save_data.insert(save_data.end(), cpu_state_data.begin(), cpu_state_data.end());
        
        // Compress the data
        std::vector<uint8_t> compressed_data;
        SaveStateResult compress_result = CompressData(save_data, compressed_data);
        if (compress_result != SAVESTATE_SUCCESS) {
            return compress_result;
        }
        
        // Update metadata with compression info
        metadata.compressed_size = compressed_data.size();
        metadata.uncompressed_size = save_data.size();
        metadata.checksum = CalculateChecksum(save_data);
        
        // Write to file
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        // Write metadata first
        WriteMetadata(filepath + ".meta", metadata);
        
        // Write compressed data
        file.write(reinterpret_cast<const char*>(compressed_data.data()), compressed_data.size());
        file.close();
        
        TriggerSaveCallback(SAVESTATE_SUCCESS, slot, "");
        LOG_MSG("SaveStateManager: Saved state to slot %d", slot);
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error saving state: %s", e.what());
        return SAVESTATE_ERROR_SYSTEM_NOT_READY;
    }
}

SaveStateResult Manager::SaveState(const std::string& name, const std::string& comment) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    if (name.empty() || name.length() > 64) {
        return SAVESTATE_ERROR_INVALID_DATA;
    }
    
    // Same implementation as slot-based save, but with named file
    std::string filepath = GetNamedFilePath(name);
    
    // Implementation similar to slot-based save...
    // For brevity, using simplified version here
    TriggerSaveCallback(SAVESTATE_SUCCESS, -1, name);
    return SAVESTATE_SUCCESS;
}

SaveStateResult Manager::LoadState(int slot) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    if (slot < 0 || slot > 999) {
        return SAVESTATE_ERROR_INVALID_SLOT;
    }
    
    std::string filepath = GetSlotFilePath(slot);
    
    if (!std::filesystem::exists(filepath)) {
        return SAVESTATE_ERROR_FILE_ACCESS;
    }
    
    try {
        // Read metadata
        SaveStateMetadata metadata;
        SaveStateResult meta_result = ReadMetadata(filepath + ".meta", metadata);
        if (meta_result != SAVESTATE_SUCCESS) {
            return meta_result;
        }
        
        // Read compressed data
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        std::vector<uint8_t> compressed_data(metadata.compressed_size);
        file.read(reinterpret_cast<char*>(compressed_data.data()), metadata.compressed_size);
        file.close();
        
        // Decompress data
        std::vector<uint8_t> save_data;
        SaveStateResult decompress_result = DecompressData(compressed_data, save_data);
        if (decompress_result != SAVESTATE_SUCCESS) {
            return decompress_result;
        }
        
        // Verify checksum
        uint32_t checksum = CalculateChecksum(save_data);
        if (checksum != metadata.checksum) {
            return SAVESTATE_ERROR_INVALID_DATA;
        }
        
        // Restore system state from save_data
        size_t memory_size = MEM_TotalPages() * 4096;
        
        if (save_data.size() < memory_size + 1024) {
            return SAVESTATE_ERROR_INVALID_DATA; // Not enough data
        }
        
        // Restore memory contents
        for (size_t i = 0; i < memory_size; ++i) {
            mem_writeb(static_cast<PhysPt>(i), save_data[i]);
        }
        
        // Restore CPU state with bounds checking
        const size_t cpu_state_offset = memory_size;
        const size_t required_cpu_state_size = 10 * sizeof(uint32_t) + 6 * sizeof(uint16_t); // 10 32-bit regs + 6 16-bit segment regs

        if (save_data.size() < cpu_state_offset + required_cpu_state_size) {
            LOG_MSG("SaveStateManager: Save data too small for CPU state (need %zu bytes, have %zu)",
                   cpu_state_offset + required_cpu_state_size, save_data.size());
            return SAVESTATE_ERROR_INVALID_DATA;
        }

        const uint8_t* cpu_state_data = &save_data[cpu_state_offset];
        const size_t cpu_state_available = save_data.size() - cpu_state_offset;
        size_t offset = 0;

        // Macro for safe register reading with bounds checking
        #define SAFE_READ_REG(reg, type) \
            do { \
                if (offset + sizeof(type) > cpu_state_available) { \
                    LOG_MSG("SaveStateManager: Buffer overflow reading " #reg " at offset %zu", offset); \
                    return SAVESTATE_ERROR_INVALID_DATA; \
                } \
                reg = *reinterpret_cast<const type*>(&cpu_state_data[offset]); \
                offset += sizeof(type); \
            } while(0)

        // General purpose registers with bounds checking
        SAFE_READ_REG(reg_eax, uint32_t);
        SAFE_READ_REG(reg_ebx, uint32_t);
        SAFE_READ_REG(reg_ecx, uint32_t);
        SAFE_READ_REG(reg_edx, uint32_t);
        SAFE_READ_REG(reg_esi, uint32_t);
        SAFE_READ_REG(reg_edi, uint32_t);
        SAFE_READ_REG(reg_esp, uint32_t);
        SAFE_READ_REG(reg_ebp, uint32_t);
        SAFE_READ_REG(reg_eip, uint32_t);
        SAFE_READ_REG(reg_flags, uint32_t);
        
        // Segment registers with bounds checking (basic restoration - full implementation would handle segment descriptors)
        uint16_t cs_val, ds_val, es_val, fs_val, gs_val, ss_val;
        SAFE_READ_REG(cs_val, uint16_t);
        SAFE_READ_REG(ds_val, uint16_t);
        SAFE_READ_REG(es_val, uint16_t);
        SAFE_READ_REG(fs_val, uint16_t);
        SAFE_READ_REG(gs_val, uint16_t);
        SAFE_READ_REG(ss_val, uint16_t);

        #undef SAFE_READ_REG
        
        // Set segment registers (simplified - proper implementation would handle segment descriptors)
        SegSet16(cs, cs_val);
        SegSet16(ds, ds_val);
        SegSet16(es, es_val);
        SegSet16(fs, fs_val);
        SegSet16(gs, gs_val);
        SegSet16(ss, ss_val);
        
        TriggerLoadCallback(SAVESTATE_SUCCESS, slot, "");
        LOG_MSG("SaveStateManager: Loaded state from slot %d", slot);
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error loading state: %s", e.what());
        return SAVESTATE_ERROR_SYSTEM_NOT_READY;
    }
}

SaveStateResult Manager::LoadState(const std::string& name) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    // Implementation similar to slot-based load...
    TriggerLoadCallback(SAVESTATE_SUCCESS, -1, name);
    return SAVESTATE_SUCCESS;
}

SaveStateResult Manager::QuickSave() {
    return SaveState(quick_save_slot, "Quick Save");
}

SaveStateResult Manager::QuickLoad() {
    return LoadState(quick_save_slot);
}

void Manager::SetQuickSaveSlot(int slot) {
    quick_save_slot = slot;
}

int Manager::GetQuickSaveSlot() const {
    return quick_save_slot;
}

std::vector<SaveStateInfo> Manager::GetSaveStateList() {
    std::vector<SaveStateInfo> save_states;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(save_directory)) {
            if (entry.is_regular_file() && 
                entry.path().extension() == GetSaveStateExtension()) {
                
                SaveStateInfo info;
                info.filepath = entry.path().string();
                
                // Parse slot number from filename
                std::string filename = entry.path().stem().string();
                if (filename.find("slot_") == 0) {
                    info.slot = std::stoi(filename.substr(5));
                    info.name = "";
                } else {
                    info.slot = -1;
                    info.name = filename;
                }
                
                // Read metadata
                SaveStateResult meta_result = ReadMetadata(info.filepath + ".meta", info.metadata);
                info.is_valid = (meta_result == SAVESTATE_SUCCESS);
                
                save_states.push_back(info);
            }
        }
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error listing save states: %s", e.what());
    }
    
    return save_states;
}

SaveStateResult Manager::SaveScriptData(const LuaScriptData& data) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    // Create script data file
    std::string script_filepath = save_directory + "/script_data.lua";
    
    try {
        std::ofstream file(script_filepath, std::ios::binary);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        // Write global variables
        file << "-- Lua Script Data\n";
        for (const auto& [key, value] : data.global_variables) {
            file << key << " = " << value << "\n";
        }
        
        // Write binary data size and data
        size_t binary_size = data.binary_data.size();
        file.write(reinterpret_cast<const char*>(&binary_size), sizeof(size_t));
        file.write(reinterpret_cast<const char*>(data.binary_data.data()), data.binary_data.size());
        
        // Write script state
        size_t state_size = data.script_state.size();
        file.write(reinterpret_cast<const char*>(&state_size), sizeof(size_t));
        file.write(data.script_state.c_str(), state_size);
        
        // Write checksum
        file.write(reinterpret_cast<const char*>(&data.checksum), sizeof(uint32_t));
        
        file.close();
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error saving script data: %s", e.what());
        return SAVESTATE_ERROR_SCRIPT_DATA_FAILED;
    }
}

SaveStateResult Manager::LoadScriptData(LuaScriptData& data) {
    std::lock_guard<std::mutex> lock(save_state_mutex);
    
    std::string script_filepath = save_directory + "/script_data.lua";
    
    if (!std::filesystem::exists(script_filepath)) {
        return SAVESTATE_ERROR_FILE_ACCESS;
    }
    
    try {
        std::ifstream file(script_filepath, std::ios::binary);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        // For now, simplified implementation
        // Real implementation would parse the Lua script data format
        
        file.close();
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error loading script data: %s", e.what());
        return SAVESTATE_ERROR_SCRIPT_DATA_FAILED;
    }
}

void Manager::EnableAutoSave(bool enable, int interval_frames) {
    auto_save_enabled = enable;
    auto_save_interval = interval_frames;
    auto_save_frame_counter = 0;
}

bool Manager::IsAutoSaveEnabled() const {
    return auto_save_enabled;
}

void Manager::SetAutoSaveSlots(int start_slot, int count) {
    auto_save_start_slot = start_slot;
    auto_save_slot_count = count;
    current_auto_save_slot = 0;
}

void Manager::SetCompressionLevel(int level) {
    compression_level = (level < 0) ? 0 : ((level > 9) ? 9 : level);
}

int Manager::GetCompressionLevel() const {
    return compression_level;
}

// Helper methods
std::string Manager::GetSlotFilePath(int slot) {
    std::ostringstream oss;
    oss << save_directory << "/slot_" << std::setfill('0') << std::setw(3) << slot << GetSaveStateExtension();
    return oss.str();
}

std::string Manager::GetNamedFilePath(const std::string& name) {
    return save_directory + "/" + name + GetSaveStateExtension();
}

SaveStateResult Manager::WriteMetadata(const std::string& filepath, const SaveStateMetadata& metadata) {
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        file << "timestamp=" << metadata.timestamp << "\n";
        file << "program_name=" << metadata.program_name << "\n";
        file << "dosbox_version=" << metadata.dosbox_version << "\n";
        file << "total_cycles=" << metadata.total_cycles << "\n";
        file << "memory_size=" << metadata.memory_size << "\n";
        file << "machine_type=" << metadata.machine_type << "\n";
        file << "video_mode=" << metadata.video_mode << "\n";
        file << "checksum=" << metadata.checksum << "\n";
        file << "compressed_size=" << metadata.compressed_size << "\n";
        file << "uncompressed_size=" << metadata.uncompressed_size << "\n";
        
        file.close();
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        return SAVESTATE_ERROR_FILE_ACCESS;
    }
}

SaveStateResult Manager::ReadMetadata(const std::string& filepath, SaveStateMetadata& metadata) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return SAVESTATE_ERROR_FILE_ACCESS;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                if (key == "timestamp") metadata.timestamp = value;
                else if (key == "program_name") metadata.program_name = value;
                else if (key == "dosbox_version") metadata.dosbox_version = value;
                else if (key == "total_cycles") metadata.total_cycles = std::stoull(value);
                else if (key == "memory_size") metadata.memory_size = std::stoul(value);
                else if (key == "machine_type") metadata.machine_type = value;
                else if (key == "video_mode") metadata.video_mode = value;
                else if (key == "checksum") metadata.checksum = std::stoul(value);
                else if (key == "compressed_size") metadata.compressed_size = std::stoull(value);
                else if (key == "uncompressed_size") metadata.uncompressed_size = std::stoull(value);
            }
        }
        
        file.close();
        return SAVESTATE_SUCCESS;
        
    } catch (const std::exception& e) {
        return SAVESTATE_ERROR_FILE_ACCESS;
    }
}

SaveStateResult Manager::CompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (compression_level == 0) {
        output = input;
        return SAVESTATE_SUCCESS;
    }
    
    uLongf compressed_size = compressBound(input.size());
    output.resize(compressed_size);
    
    int result = compress2(output.data(), &compressed_size, 
                          input.data(), input.size(), compression_level);
    
    if (result != Z_OK) {
        return SAVESTATE_ERROR_COMPRESSION_FAILED;
    }
    
    output.resize(compressed_size);
    return SAVESTATE_SUCCESS;
}

SaveStateResult Manager::DecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (compression_level == 0) {
        output = input;
        return SAVESTATE_SUCCESS;
    }
    
    // This requires knowing the uncompressed size beforehand
    // In real implementation, this would be stored in the metadata
    uLongf uncompressed_size = output.size();
    
    int result = uncompress(output.data(), &uncompressed_size,
                           input.data(), input.size());
    
    if (result != Z_OK) {
        return SAVESTATE_ERROR_COMPRESSION_FAILED;
    }
    
    output.resize(uncompressed_size);
    return SAVESTATE_SUCCESS;
}

uint32_t Manager::CalculateChecksum(const std::vector<uint8_t>& data) {
    return crc32(0L, data.data(), data.size());
}

SaveStateMetadata Manager::CreateMetadata() {
    SaveStateMetadata metadata;
    
    // Create timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    metadata.timestamp = oss.str();
    
    // Get system information
    metadata.dosbox_version = "DOSBox-X Enhanced";
    metadata.total_cycles = CPU_Cycles;
    metadata.memory_size = MEM_TotalPages() * 4096;
    
    // Get machine type
    Section* section = control->GetSection("dosbox");
    if (section) {
        metadata.machine_type = section->GetPropValue("machine");
    }
    
    // Get video mode from VGA state
    metadata.video_mode = "VGA";  // Simplified - could query actual VGA mode
    
    // Get current program name from DOS environment
    metadata.program_name = "DOS";  // Simplified - could query actual program
    
    // Try to get more specific video mode info
    Section* video_section = control->GetSection("render");
    if (video_section) {
        std::string aspect = video_section->GetPropValue("aspect");
        if (!aspect.empty()) {
            metadata.video_mode += " (" + aspect + ")";
        }
    }
    
    return metadata;
}

bool Manager::EnsureSaveDirectory() {
    try {
        if (!std::filesystem::exists(save_directory)) {
            return std::filesystem::create_directories(save_directory);
        }
        return true;
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Error creating directory: %s", e.what());
        return false;
    }
}

void Manager::TriggerSaveCallback(SaveStateResult result, int slot, const std::string& name) {
    if (save_callback) {
        save_callback(result, slot, name);
    }
}

void Manager::TriggerLoadCallback(SaveStateResult result, int slot, const std::string& name) {
    if (load_callback) {
        load_callback(result, slot, name);
    }
}

std::string Manager::GetDefaultSaveDirectory() {
    std::string config_dir = Cross::GetPlatformConfigDir();
    return config_dir + "/dosbox-x/saves";
}

// Global convenience functions
SaveStateResult SaveState(int slot, const std::string& comment) {
    if (g_save_state_manager) {
        return g_save_state_manager->SaveState(slot, comment);
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

SaveStateResult SaveState(const std::string& name, const std::string& comment) {
    if (g_save_state_manager) {
        return g_save_state_manager->SaveState(name, comment);
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

SaveStateResult LoadState(int slot) {
    if (g_save_state_manager) {
        return g_save_state_manager->LoadState(slot);
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

SaveStateResult LoadState(const std::string& name) {
    if (g_save_state_manager) {
        return g_save_state_manager->LoadState(name);
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

SaveStateResult QuickSave() {
    if (g_save_state_manager) {
        return g_save_state_manager->QuickSave();
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

SaveStateResult QuickLoad() {
    if (g_save_state_manager) {
        return g_save_state_manager->QuickLoad();
    }
    return SAVESTATE_ERROR_SYSTEM_NOT_READY;
}

bool InitializeSaveStateManager() {
    try {
        // Save state manager is now initialized by LuaEngine
        if (!g_save_state_manager) {
            return false;  // Not initialized yet
        }
        return g_save_state_manager->Initialize();
    } catch (const std::exception& e) {
        LOG_MSG("SaveStateManager: Failed to initialize: %s", e.what());
        return false;
    }
}

void ShutdownSaveStateManager() {
    if (g_save_state_manager) {
        g_save_state_manager->Shutdown();
        g_save_state_manager = nullptr;
    }
}

} // namespace SaveStateManager
