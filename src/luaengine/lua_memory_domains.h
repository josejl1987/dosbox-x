#ifndef LUA_MEMORY_DOMAINS_H
#define LUA_MEMORY_DOMAINS_H

// Use main memory domain system directly instead of wrapper
#include <memory>
#include <string>
#include <vector>
#include "../../include/memory_domains.h"

// LuaEngine-specific namespace for compatibility
namespace LuaEngineMemoryDomains {
    class MemoryDomainManagerAdapter;  // forward declaration for alias

    // Type aliases for compatibility with existing code
    using MemoryDomain = ::MemoryDomains::MemoryDomain;
    using MemoryDomainManager = MemoryDomainManagerAdapter;  // Use our adapter
    using DomainType = ::MemoryDomains::DomainType;
    using AccessResult = ::MemoryDomains::AccessResult;
    using AccessFlags = ::MemoryDomains::AccessFlags;

    // Enum aliases for compatibility - map old names to new system
    enum class MemoryDomainType : int {
        DOS_CONVENTIONAL = static_cast<int>(::MemoryDomains::DomainType::DOS_CONVENTIONAL),
        DOS_UMB = static_cast<int>(::MemoryDomains::DomainType::DOS_UMB),
        EMS = static_cast<int>(::MemoryDomains::DomainType::EMS_PAGES),  // Map EMS to EMS_PAGES
        XMS = static_cast<int>(::MemoryDomains::DomainType::XMS_EXTENDED),  // Map XMS to XMS_EXTENDED
        VIDEO_RAM = static_cast<int>(::MemoryDomains::DomainType::VIDEO_RAM),
        BIOS_ROM = static_cast<int>(::MemoryDomains::DomainType::BIOS_ROM),
        CUSTOM = static_cast<int>(::MemoryDomains::DomainType::UNKNOWN)
    };

    // Compatibility wrapper class that adds missing methods to the main system
    class MemoryDomainManagerAdapter {
    private:
        ::MemoryDomains::DomainManager* manager_;

    public:
        MemoryDomainManagerAdapter(::MemoryDomains::DomainManager* mgr = ::MemoryDomains::GetDomainManager()) : manager_(mgr) {}

        // Original methods
        ::MemoryDomains::MemoryDomain* GetDomain(::MemoryDomains::DomainType type) {
            return manager_ ? manager_->GetDomain(type) : nullptr;
        }

        ::MemoryDomains::MemoryDomain* GetDomain(MemoryDomainType type) {
            return GetDomain(static_cast<::MemoryDomains::DomainType>(type));
        }

        std::vector<::MemoryDomains::DomainType> GetAvailableDomains() const {
            return manager_ ? manager_->GetAvailableDomains() : std::vector<::MemoryDomains::DomainType>();
        }

        // Compatibility methods that were in the old adapter
        std::vector<std::string> getDomainNames() const;
        ::MemoryDomains::MemoryDomain* getDomain(const std::string& name);
        bool initializeDomains();

        // Direct memory operations on specific domains
        bool readByte(uint32_t address, uint8_t& value);
        bool writeByte(uint32_t address, uint8_t value);
        bool readWord(uint32_t address, uint16_t& value);
        bool writeWord(uint32_t address, uint16_t value);
        bool readDWord(uint32_t address, uint32_t& value);
        bool writeDWord(uint32_t address, uint32_t value);
        bool writeByte(const std::string& domain_name, uint32_t address, uint8_t value);
        bool writeWord(const std::string& domain_name, uint32_t address, uint16_t value);
        bool writeDWord(const std::string& domain_name, uint32_t address, uint32_t value);

        // Domain-scoped helpers matching legacy interface
        uint8_t readByte(const std::string& domain_name, uint32_t address);
        uint16_t readWord(const std::string& domain_name, uint32_t address);
        uint32_t readDWord(const std::string& domain_name, uint32_t address);
        uint8_t readByteAt(uint32_t address) { uint8_t v = 0; readByte(address, v); return v; }
        bool isValidAddress(const std::string& domain_name, uint32_t address);
        std::vector<uint32_t> searchBytes(const std::string& domain_name, const std::vector<uint8_t>& pattern);

        // Domain type conversion
        ::MemoryDomains::DomainType getDomainTypeFromName(const std::string& name);
    };

    // Initialize and shutdown functions
    bool InitializeMemoryDomains();
    void ShutdownMemoryDomains();
    MemoryDomainManagerAdapter* GetGlobalMemoryDomainManager();
}

#endif // LUA_MEMORY_DOMAINS_H
