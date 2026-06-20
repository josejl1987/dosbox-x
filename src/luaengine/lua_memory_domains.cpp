#include "lua_memory_domains.h"
#include "../../include/dosbox.h"
#include "../../include/logging.h"
#include "../../include/memory_domains.h"
#include <algorithm>

namespace LuaEngineMemoryDomains {

// Global accessor function that creates an adapter wrapper
MemoryDomainManager* GetGlobalMemoryDomainManager() {
    static std::unique_ptr<MemoryDomainManagerAdapter> adapter =
        std::make_unique<MemoryDomainManagerAdapter>(::MemoryDomains::GetDomainManager());
    return adapter.get();
}

// Implementation of MemoryDomainManagerAdapter methods
std::vector<std::string> MemoryDomainManagerAdapter::getDomainNames() const {
    std::vector<std::string> names;
    if (!manager_) return names;

    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        switch (type) {
            case ::MemoryDomains::DomainType::DOS_CONVENTIONAL:
                names.push_back("DOS Conventional");
                break;
            case ::MemoryDomains::DomainType::DOS_UMB:
                names.push_back("DOS UMB");
                break;
            case ::MemoryDomains::DomainType::EMS_PAGES:
                names.push_back("EMS");
                break;
            case ::MemoryDomains::DomainType::XMS_EXTENDED:
                names.push_back("XMS");
                break;
            case ::MemoryDomains::DomainType::VIDEO_RAM:
                names.push_back("Video RAM");
                break;
            case ::MemoryDomains::DomainType::BIOS_ROM:
                names.push_back("BIOS ROM");
                break;
            default:
                names.push_back("Unknown");
                break;
        }
    }
    return names;
}

::MemoryDomains::MemoryDomain* MemoryDomainManagerAdapter::getDomain(const std::string& name) {
    if (!manager_) return nullptr;

    if (name == "DOS Conventional") {
        return GetDomain(::MemoryDomains::DomainType::DOS_CONVENTIONAL);
    } else if (name == "DOS UMB") {
        return GetDomain(::MemoryDomains::DomainType::DOS_UMB);
    } else if (name == "EMS") {
        return GetDomain(::MemoryDomains::DomainType::EMS_PAGES);
    } else if (name == "XMS") {
        return GetDomain(::MemoryDomains::DomainType::XMS_EXTENDED);
    } else if (name == "Video RAM") {
        return GetDomain(::MemoryDomains::DomainType::VIDEO_RAM);
    } else if (name == "BIOS ROM") {
        return GetDomain(::MemoryDomains::DomainType::BIOS_ROM);
    }
    return nullptr;
}

bool MemoryDomainManagerAdapter::initializeDomains() {
    // The main system handles initialization automatically
    return manager_ != nullptr;
}

bool MemoryDomainManagerAdapter::readByte(uint32_t address, uint8_t& value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->ReadByte(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

bool MemoryDomainManagerAdapter::writeByte(uint32_t address, uint8_t value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->WriteByte(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

bool MemoryDomainManagerAdapter::readWord(uint32_t address, uint16_t& value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->ReadWord(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

bool MemoryDomainManagerAdapter::writeWord(uint32_t address, uint16_t value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->WriteWord(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

bool MemoryDomainManagerAdapter::readDWord(uint32_t address, uint32_t& value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->ReadDword(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

bool MemoryDomainManagerAdapter::writeDWord(uint32_t address, uint32_t value) {
    // Find which domain this address belongs to
    auto domains = GetAvailableDomains();
    for (auto type : domains) {
        auto* domain = GetDomain(type);
        if (domain && domain->IsAddressValid(address)) {
            auto result = domain->WriteDword(address, value);
            return result == ::MemoryDomains::AccessResult::SUCCESS;
        }
    }
    return false;
}

::MemoryDomains::DomainType MemoryDomainManagerAdapter::getDomainTypeFromName(const std::string& name) {
    if (name == "DOS Conventional") {
        return ::MemoryDomains::DomainType::DOS_CONVENTIONAL;
    } else if (name == "DOS UMB") {
        return ::MemoryDomains::DomainType::DOS_UMB;
    } else if (name == "EMS") {
        return ::MemoryDomains::DomainType::EMS_PAGES;
    } else if (name == "XMS") {
        return ::MemoryDomains::DomainType::XMS_EXTENDED;
    } else if (name == "Video RAM") {
        return ::MemoryDomains::DomainType::VIDEO_RAM;
    } else if (name == "BIOS ROM") {
        return ::MemoryDomains::DomainType::BIOS_ROM;
    }
    return ::MemoryDomains::DomainType::UNKNOWN;
}

uint8_t MemoryDomainManagerAdapter::readByte(const std::string& domain_name, uint32_t address) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->readByte(address) : 0;
}

uint16_t MemoryDomainManagerAdapter::readWord(const std::string& domain_name, uint32_t address) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->readWord(address) : 0;
}

uint32_t MemoryDomainManagerAdapter::readDWord(const std::string& domain_name, uint32_t address) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->readDWord(address) : 0;
}

bool MemoryDomainManagerAdapter::writeByte(const std::string& domain_name, uint32_t address, uint8_t value) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->writeByte(address, value) : false;
}

bool MemoryDomainManagerAdapter::writeWord(const std::string& domain_name, uint32_t address, uint16_t value) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->writeWord(address, value) : false;
}

bool MemoryDomainManagerAdapter::writeDWord(const std::string& domain_name, uint32_t address, uint32_t value) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->writeDWord(address, value) : false;
}

bool MemoryDomainManagerAdapter::isValidAddress(const std::string& domain_name, uint32_t address) {
    auto* domain = getDomain(domain_name);
    return domain ? domain->isAddressValid(address) : false;
}

std::vector<uint32_t> MemoryDomainManagerAdapter::searchBytes(const std::string& domain_name, const std::vector<uint8_t>& pattern) {
    std::vector<uint32_t> results;
    if (pattern.empty()) return results;
    auto* domain = getDomain(domain_name);
    if (!domain) return results;

    auto base = domain->getBaseAddress();
    auto size = domain->getSize();
    auto buffer = domain->readBlock(base, size);
    if (buffer.size() < pattern.size()) return results;

    auto it = std::search(buffer.begin(), buffer.end(), pattern.begin(), pattern.end());
    while (it != buffer.end()) {
        uint32_t offset = static_cast<uint32_t>(std::distance(buffer.begin(), it));
        results.push_back(base + offset);
        it = std::search(it + 1, buffer.end(), pattern.begin(), pattern.end());
    }
    return results;
}

// Legacy function name for compatibility
MemoryDomainManager* getGlobalMemoryDomainManager() {
    return GetGlobalMemoryDomainManager();
}

// Initialize memory domains
bool InitializeMemoryDomains() {
    try {
        LOG(LOG_MISC,LOG_DEBUG)("Initializing LuaEngine memory domains using main system");

        // Initialize the main memory domains system
        if (!::MemoryDomains::InitializeMemoryDomains()) {
            LOG(LOG_MISC,LOG_WARN)("Failed to initialize main memory domains system");
            return false;
        }

        // Get the global manager to ensure it's available
        auto* manager = GetGlobalMemoryDomainManager();
        if (!manager) {
            LOG(LOG_MISC,LOG_WARN)("Failed to get global memory domain manager");
            return false;
        }

        LOG(LOG_MISC,LOG_DEBUG)("LuaEngine memory domains initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG(LOG_MISC,LOG_ERROR)("Exception initializing memory domains: %s", e.what());
        return false;
    } catch (...) {
        LOG(LOG_MISC,LOG_ERROR)("Unknown exception initializing memory domains");
        return false;
    }
}

// Shutdown memory domains (now handled by main system)
void ShutdownMemoryDomains() {
    // The main system handles shutdown, so we just log
    LOG(LOG_MISC,LOG_DEBUG)("LuaEngine memory domains shutdown requested");
}

// Cleanup function (legacy name)
void shutdownGlobalMemoryDomainManager() {
    ShutdownMemoryDomains();
}

} // namespace LuaEngineMemoryDomains
