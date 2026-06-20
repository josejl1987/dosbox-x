#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>

// Forward declarations
class Section;

namespace MemoryDomains {

enum class DomainType {
    MAIN_RAM,           // Physical RAM (MemBase access)
    DOS_CONVENTIONAL,   // 0x00000-0x9FFFF DOS conventional memory
    DOS_UMB,           // Upper Memory Blocks (0xA0000-0xFFFFF)
    EMS_PAGES,         // EMS expanded memory pages
    XMS_EXTENDED,      // XMS extended memory (above 1MB)
    VIDEO_RAM,         // Video framebuffers
    BIOS_ROM,          // ROM areas (BIOS, option ROMs)
    IO_PORTS,          // I/O port space (for completeness)
    SAVE_STATE,        // Save state memory (for state management)
    UNKNOWN            // Unknown/invalid domain
};

// Memory access result codes
enum class AccessResult {
    SUCCESS,
    ADDRESS_OUT_OF_RANGE,
    READ_ONLY_VIOLATION,
    UNMAPPED_MEMORY,
    ACCESS_DENIED,
    DOMAIN_UNAVAILABLE
};

// Memory domain access flags
enum AccessFlags {
    ACCESS_READ     = 0x01,
    ACCESS_WRITE    = 0x02,
    ACCESS_EXECUTE  = 0x04,
    ACCESS_ALL      = ACCESS_READ | ACCESS_WRITE | ACCESS_EXECUTE
};

// Base memory domain interface
class MemoryDomain {
public:
    virtual ~MemoryDomain() = default;
    
    // Core access methods
    virtual AccessResult ReadByte(uint32_t address, uint8_t& value) = 0;
    virtual AccessResult WriteByte(uint32_t address, uint8_t value) = 0;
    virtual AccessResult ReadWord(uint32_t address, uint16_t& value);
    virtual AccessResult WriteWord(uint32_t address, uint16_t value);
    virtual AccessResult ReadDword(uint32_t address, uint32_t& value);
    virtual AccessResult WriteDword(uint32_t address, uint32_t value);
    
    // Bulk operations
    virtual AccessResult ReadBytes(uint32_t address, uint8_t* buffer, size_t count);
    virtual AccessResult WriteBytes(uint32_t address, const uint8_t* buffer, size_t count);
    
    // Domain information
    virtual uint32_t GetSize() const = 0;
    virtual uint32_t GetStartAddress() const = 0;
    virtual uint32_t GetEndAddress() const = 0;
    virtual const char* GetName() const = 0;
    virtual DomainType GetType() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool IsWritable() const = 0;
    virtual uint32_t GetAccessFlags() const = 0;
    
    // Utility methods
    virtual bool IsAddressValid(uint32_t address) const;
    virtual std::vector<uint32_t> Search(const uint8_t* pattern, size_t pattern_size, 
                                       uint32_t start_addr = 0, uint32_t end_addr = 0xFFFFFFFF);
    virtual std::string GetDisplayName() const;

    // ---------------------------------------------------------------------
    // Compatibility helpers for the legacy Lua engine codebase
    // These thin wrappers preserve the old lower-case naming and convenience
    // signatures that return values directly instead of AccessResult enums.
    // ---------------------------------------------------------------------
    uint32_t getSize() const { return GetSize(); }
    uint32_t getBaseAddress() const { return GetStartAddress(); }
    uint32_t getEndAddress() const { return GetEndAddress(); }
    std::string getName() const { return std::string(GetName()); }
    DomainType getDomainType() const { return GetType(); }
    bool isWritable() const { return IsWritable(); }
    bool isAddressValid(uint32_t address) const { return IsAddressValid(address); }
    uint32_t getAccessFlags() const { return GetAccessFlags(); }

    uint8_t readByte(uint32_t address) const {
        auto* self = const_cast<MemoryDomain*>(this);
        uint8_t value = 0;
        return self->ReadByte(address, value) == AccessResult::SUCCESS ? value : 0;
    }

    uint16_t readWord(uint32_t address) const {
        auto* self = const_cast<MemoryDomain*>(this);
        uint16_t value = 0;
        return self->ReadWord(address, value) == AccessResult::SUCCESS ? value : 0;
    }

    uint32_t readDWord(uint32_t address) const {
        auto* self = const_cast<MemoryDomain*>(this);
        uint32_t value = 0;
        return self->ReadDword(address, value) == AccessResult::SUCCESS ? value : 0;
    }

    bool writeByte(uint32_t address, uint8_t value) {
        return WriteByte(address, value) == AccessResult::SUCCESS;
    }

    bool writeWord(uint32_t address, uint16_t value) {
        return WriteWord(address, value) == AccessResult::SUCCESS;
    }

    bool writeDWord(uint32_t address, uint32_t value) {
        return WriteDword(address, value) == AccessResult::SUCCESS;
    }

    std::vector<uint8_t> readBlock(uint32_t address, size_t count) const {
        std::vector<uint8_t> buffer(count);
        auto* self = const_cast<MemoryDomain*>(this);
        if (count == 0) return {};
        if (self->ReadBytes(address, buffer.data(), count) != AccessResult::SUCCESS) {
            return {};
        }
        return buffer;
    }

    std::string readText(uint32_t address, size_t length, uint16_t /*codepage*/ = 932) const {
        std::string out;
        out.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            uint8_t b = readByte(address + static_cast<uint32_t>(i));
            if (b == 0) break;
            out.push_back(static_cast<char>(b));
        }
        return out;
    }
};

// Domain-specific interfaces
class DOSMemoryDomain : public MemoryDomain {
public:
    // DOS-specific methods
    virtual bool GetMCBInfo(uint32_t address, uint16_t& segment, uint16_t& size, 
                           uint8_t& owner, bool& allocated) = 0;
    virtual std::vector<uint32_t> GetMCBChain() = 0;
    virtual uint32_t GetLargestFreeBlock() = 0;
    virtual uint32_t GetTotalFreeMemory() = 0;
};

class EMSMemoryDomain : public MemoryDomain {
public:
    // EMS-specific methods
    virtual bool GetPageInfo(uint16_t handle, uint16_t logical_page, 
                           uint16_t& physical_page, bool& mapped) = 0;
    virtual std::vector<uint16_t> GetActiveHandles() = 0;
    virtual uint32_t GetPageFrameAddress() = 0;
    virtual uint16_t GetPageSize() = 0;
    virtual uint16_t GetTotalPages() const = 0;
    virtual uint16_t GetFreePages() const = 0;
    virtual uint16_t GetHandleSize(uint16_t handle) const = 0;
    virtual bool GetHandleName(uint16_t handle, std::string& name) const = 0;
};

class XMSMemoryDomain : public MemoryDomain {
public:
    // XMS-specific methods
    virtual bool GetBlockInfo(uint16_t handle, uint32_t& size, uint32_t& address, 
                            bool& allocated) = 0;
    virtual std::vector<uint16_t> GetActiveHandles() = 0;
    virtual uint32_t GetLargestFreeBlock() = 0;
    virtual uint32_t GetTotalFreeMemory() = 0;
    virtual bool IsHMAAvailable() = 0;
    virtual uint32_t GetHMAAddress() = 0;
    virtual uint32_t GetTotalMemory() const = 0;
    virtual uint32_t GetFreeMemory() const = 0;
    virtual uint32_t GetHandleSize(uint16_t handle) const = 0;
    virtual uint32_t GetHandleAddress(uint16_t handle) const = 0;
    virtual uint16_t GetHandleLocks(uint16_t handle) const = 0;
    virtual bool IsHandleLocked(uint16_t handle) const = 0;
};

class VideoRAMDomain : public MemoryDomain {
public:
    // Video RAM specific methods
    virtual bool IsTextMode() = 0;
    virtual bool IsGraphicsMode() = 0;
    virtual bool IsPC98Mode() = 0;
    virtual uint32_t GetTextBufferAddress() = 0;
    virtual uint32_t GetGraphicsBufferAddress() = 0;
    virtual uint32_t GetCurrentDisplayStart() = 0;
    virtual uint16_t GetVideoMode() = 0;
    virtual uint16_t GetScreenWidth() = 0;
    virtual uint16_t GetScreenHeight() = 0;
    virtual uint8_t GetColorDepth() = 0;
    
    // PC-98 specific methods
    virtual uint32_t GetPC98TextAddress() = 0;
    virtual uint32_t GetPC98GraphicsAddress() = 0;
    virtual uint32_t GetPC98AttributeAddress() = 0;
    virtual bool GetPC98PlaneInfo(uint8_t plane, uint32_t& address, uint32_t& size) = 0;
    virtual std::vector<uint8_t> GetPC98ActivePlanes() = 0;
};

class BIOSROMDomain : public MemoryDomain {
public:
    // BIOS ROM specific methods
    virtual bool IsSystemBIOS(uint32_t address) = 0;
    virtual bool IsVideoBIOS(uint32_t address) = 0;
    virtual bool IsExtensionROM(uint32_t address) = 0;
    virtual bool IsPC98BIOS(uint32_t address) = 0;
    virtual uint32_t GetBIOSEntryPoint() = 0;
    virtual uint32_t GetVideoROMAddress() = 0;
    virtual std::string GetBIOSVersion() = 0;
    virtual std::string GetBIOSDate() = 0;
    
    // PC-98 specific BIOS methods
    virtual uint32_t GetPC98SystemROMAddress() = 0;
    virtual uint32_t GetPC98CharGenROMAddress() = 0;
    virtual uint32_t GetPC98KanjiROMAddress() = 0;
};

// Memory domain factory and manager
class DomainManager {
private:
    std::map<DomainType, std::unique_ptr<MemoryDomain>> domains;
    bool initialized;
    
public:
    DomainManager();
    ~DomainManager();
    
    // Initialization and management
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized; }
    
    // Domain access
    MemoryDomain* GetDomain(DomainType type);
    const MemoryDomain* GetDomain(DomainType type) const;
    std::vector<DomainType> GetAvailableDomains() const;
    
    // Convenience methods
    DOSMemoryDomain* GetDOSConventional();
    DOSMemoryDomain* GetDOSUMB();
    EMSMemoryDomain* GetEMS();
    XMSMemoryDomain* GetXMS();
    VideoRAMDomain* GetVideoRAM();
    BIOSROMDomain* GetBIOSROM();
    
    // Global search across domains
    struct SearchResult {
        DomainType domain;
        uint32_t address;
        std::vector<uint8_t> context; // Surrounding bytes for context
    };
    
    std::vector<SearchResult> SearchAllDomains(const uint8_t* pattern, size_t pattern_size);
    
    // Statistics
    struct DomainStats {
        uint32_t total_size;
        uint32_t used_size;
        uint32_t free_size;
        uint32_t read_count;
        uint32_t write_count;
        bool available;
    };
    
    std::map<DomainType, DomainStats> GetDomainStatistics() const;
    
private:
    void RegisterDomain(DomainType type, std::unique_ptr<MemoryDomain> domain);
    void CreateStandardDomains();
};

// Utility functions
const char* DomainTypeToString(DomainType type);
DomainType StringToDomainType(const std::string& str);
const char* AccessResultToString(AccessResult result);

// Global domain manager instance
extern std::unique_ptr<DomainManager> g_domain_manager;

// Helper functions for initialization
bool InitializeMemoryDomains();
void ShutdownMemoryDomains();
DomainManager* GetDomainManager();

} // namespace MemoryDomains
