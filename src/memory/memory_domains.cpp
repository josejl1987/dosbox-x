// Memory domains implementation - implements the interface from include/memory_domains.h
#include "../../include/memory_domains.h"
#include "../../include/dosbox.h"
#include "../../include/logging.h"
#include "../../include/mem.h"
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

namespace MemoryDomains {

// Global instance
std::unique_ptr<DomainManager> g_domain_manager;

// Default helper implementations for the MemoryDomain interface
AccessResult MemoryDomain::ReadWord(uint32_t address, uint16_t& value) {
    uint8_t lo = 0, hi = 0;
    auto res_lo = ReadByte(address, lo);
    if (res_lo != AccessResult::SUCCESS) return res_lo;
    auto res_hi = ReadByte(address + 1, hi);
    if (res_hi != AccessResult::SUCCESS) return res_hi;
    value = static_cast<uint16_t>(lo | (hi << 8));
    return AccessResult::SUCCESS;
}

AccessResult MemoryDomain::WriteWord(uint32_t address, uint16_t value) {
    auto res_lo = WriteByte(address, static_cast<uint8_t>(value & 0xFF));
    if (res_lo != AccessResult::SUCCESS) return res_lo;
    auto res_hi = WriteByte(address + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
    if (res_hi != AccessResult::SUCCESS) return res_hi;
    return AccessResult::SUCCESS;
}

AccessResult MemoryDomain::ReadDword(uint32_t address, uint32_t& value) {
    uint16_t lo = 0, hi = 0;
    auto res_lo = ReadWord(address, lo);
    if (res_lo != AccessResult::SUCCESS) return res_lo;
    auto res_hi = ReadWord(address + 2, hi);
    if (res_hi != AccessResult::SUCCESS) return res_hi;
    value = static_cast<uint32_t>(lo | (static_cast<uint32_t>(hi) << 16));
    return AccessResult::SUCCESS;
}

AccessResult MemoryDomain::WriteDword(uint32_t address, uint32_t value) {
    auto res_lo = WriteWord(address, static_cast<uint16_t>(value & 0xFFFF));
    if (res_lo != AccessResult::SUCCESS) return res_lo;
    auto res_hi = WriteWord(address + 2, static_cast<uint16_t>((value >> 16) & 0xFFFF));
    if (res_hi != AccessResult::SUCCESS) return res_hi;
    return AccessResult::SUCCESS;
}

AccessResult MemoryDomain::ReadBytes(uint32_t address, uint8_t* buffer, size_t count) {
    if (!buffer || count == 0) return AccessResult::ADDRESS_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        auto res = ReadByte(address + static_cast<uint32_t>(i), buffer[i]);
        if (res != AccessResult::SUCCESS) return res;
    }
    return AccessResult::SUCCESS;
}

AccessResult MemoryDomain::WriteBytes(uint32_t address, const uint8_t* buffer, size_t count) {
    if (!buffer || count == 0) return AccessResult::ADDRESS_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        auto res = WriteByte(address + static_cast<uint32_t>(i), buffer[i]);
        if (res != AccessResult::SUCCESS) return res;
    }
    return AccessResult::SUCCESS;
}

bool MemoryDomain::IsAddressValid(uint32_t address) const {
    return address >= GetStartAddress() && address <= GetEndAddress();
}

std::vector<uint32_t> MemoryDomain::Search(const uint8_t* pattern, size_t pattern_size,
                                           uint32_t start_addr, uint32_t end_addr) {
    std::vector<uint32_t> hits;
    if (!pattern || pattern_size == 0) return hits;

    uint32_t domain_start = GetStartAddress();
    uint32_t domain_end = GetEndAddress();
    uint32_t search_start = std::max(domain_start, start_addr);
    uint32_t search_end = end_addr ? std::min(domain_end, end_addr) : domain_end;

    if (search_start > search_end || (search_end - search_start + 1) < pattern_size)
        return hits;

    for (uint32_t addr = search_start; addr <= search_end - pattern_size + 1; ++addr) {
        bool match = true;
        for (size_t i = 0; i < pattern_size; ++i) {
            uint8_t byte = 0;
            if (ReadByte(addr + static_cast<uint32_t>(i), byte) != AccessResult::SUCCESS ||
                byte != pattern[i]) {
                match = false;
                break;
            }
        }
        if (match) hits.push_back(addr);
    }
    return hits;
}

std::string MemoryDomain::GetDisplayName() const {
    return std::string(GetName());
}

// Minimal DOS memory domain implementations backed by DOSBox memory helpers.
class BasicDOSDomain : public DOSMemoryDomain {
private:
    uint32_t start_;
    uint32_t size_;
    DomainType type_;
    std::string name_;
    bool writable_;

public:
    BasicDOSDomain(uint32_t start, uint32_t size, DomainType type,
                   const std::string& name, bool writable)
        : start_(start), size_(size), type_(type), name_(name), writable_(writable) {}

    AccessResult ReadByte(uint32_t address, uint8_t& value) override {
        if (address < start_ || address >= start_ + size_)
            return AccessResult::ADDRESS_OUT_OF_RANGE;
        value = mem_readb(static_cast<PhysPt>(address));
        return AccessResult::SUCCESS;
    }

    AccessResult WriteByte(uint32_t address, uint8_t value) override {
        if (address < start_ || address >= start_ + size_)
            return AccessResult::ADDRESS_OUT_OF_RANGE;
        if (!writable_)
            return AccessResult::READ_ONLY_VIOLATION;
        mem_writeb(static_cast<PhysPt>(address), value);
        return AccessResult::SUCCESS;
    }

    uint32_t GetSize() const override { return size_; }
    uint32_t GetStartAddress() const override { return start_; }
    uint32_t GetEndAddress() const override { return start_ + size_ - 1; }
    const char* GetName() const override { return name_.c_str(); }
    DomainType GetType() const override { return type_; }
    bool IsAvailable() const override { return true; }
    bool IsWritable() const override { return writable_; }
    uint32_t GetAccessFlags() const override { return writable_ ? ACCESS_ALL : ACCESS_READ; }

    // DOS-specific helpers not implemented for now
    bool GetMCBInfo(uint32_t, uint16_t&, uint16_t&, uint8_t&, bool&) override { return false; }
    std::vector<uint32_t> GetMCBChain() override { return {}; }
    uint32_t GetLargestFreeBlock() override { return 0; }
    uint32_t GetTotalFreeMemory() override { return 0; }
};

// DomainManager method implementations
DomainManager::DomainManager() : initialized(false) {
}

DomainManager::~DomainManager() {
    Shutdown();
}

bool DomainManager::Initialize() {
    LOG(LOG_MISC,LOG_DEBUG)("Initializing memory domain manager");
    initialized = true;
    CreateStandardDomains();
    return true;
}

void DomainManager::Shutdown() {
    domains.clear();
    initialized = false;
}

void DomainManager::CreateStandardDomains() {
    LOG(LOG_MISC,LOG_DEBUG)("Creating standard memory domains");

    // Conventional memory: 0x00000 - 0x9FFFF (640KB)
    RegisterDomain(DomainType::DOS_CONVENTIONAL,
        std::make_unique<BasicDOSDomain>(0x00000, 0xA0000,
                                         DomainType::DOS_CONVENTIONAL,
                                         "DOS Conventional", true));

    // Upper Memory Blocks: 0xA0000 - 0xFFFFF (384KB)
    RegisterDomain(DomainType::DOS_UMB,
        std::make_unique<BasicDOSDomain>(0xA0000, 0x60000,
                                         DomainType::DOS_UMB,
                                         "DOS UMB", true));
}

void DomainManager::RegisterDomain(DomainType type, std::unique_ptr<MemoryDomain> domain) {
    domains[type] = std::move(domain);
}

MemoryDomain* DomainManager::GetDomain(DomainType type) {
    auto it = domains.find(type);
    return (it != domains.end()) ? it->second.get() : nullptr;
}

const MemoryDomain* DomainManager::GetDomain(DomainType type) const {
    auto it = domains.find(type);
    return (it != domains.end()) ? it->second.get() : nullptr;
}

std::vector<DomainType> DomainManager::GetAvailableDomains() const {
    std::vector<DomainType> result;
    for (const auto& pair : domains) {
        if (pair.second) {
            result.push_back(pair.first);
        }
    }
    return result;
}

// Domain-specific getters
DOSMemoryDomain* DomainManager::GetDOSConventional() {
    return dynamic_cast<DOSMemoryDomain*>(GetDomain(DomainType::DOS_CONVENTIONAL));
}

DOSMemoryDomain* DomainManager::GetDOSUMB() {
    return dynamic_cast<DOSMemoryDomain*>(GetDomain(DomainType::DOS_UMB));
}

EMSMemoryDomain* DomainManager::GetEMS() {
    return dynamic_cast<EMSMemoryDomain*>(GetDomain(DomainType::EMS_PAGES));
}

XMSMemoryDomain* DomainManager::GetXMS() {
    return dynamic_cast<XMSMemoryDomain*>(GetDomain(DomainType::XMS_EXTENDED));
}

VideoRAMDomain* DomainManager::GetVideoRAM() {
    return dynamic_cast<VideoRAMDomain*>(GetDomain(DomainType::VIDEO_RAM));
}

BIOSROMDomain* DomainManager::GetBIOSROM() {
    return dynamic_cast<BIOSROMDomain*>(GetDomain(DomainType::BIOS_ROM));
}

std::vector<DomainManager::SearchResult> DomainManager::SearchAllDomains(const uint8_t* pattern, size_t pattern_size) {
    std::vector<DomainManager::SearchResult> results;
    if (!pattern || pattern_size == 0) return results;

    for (const auto& [type, domain_ptr] : domains) {
        if (!domain_ptr || !domain_ptr->IsAvailable()) continue;
        auto hits = domain_ptr->Search(pattern, pattern_size);
        for (auto addr : hits) {
            SearchResult sr;
            sr.domain = type;
            sr.address = addr;
            results.push_back(sr);
        }
    }
    return results;
}

std::map<DomainType, DomainManager::DomainStats> DomainManager::GetDomainStatistics() const {
    std::map<DomainType, DomainStats> stats;
    for (const auto& [type, domain_ptr] : domains) {
        if (!domain_ptr) continue;
        DomainStats s{};
        s.total_size = domain_ptr->GetSize();
        s.used_size = 0;   // Unknown without allocator details
        s.free_size = 0;   // Unknown without allocator details
        s.read_count = 0;  // Not tracked in this minimal implementation
        s.write_count = 0; // Not tracked in this minimal implementation
        s.available = domain_ptr->IsAvailable();
        stats[type] = s;
    }
    return stats;
}

// Global functions
bool InitializeMemoryDomains() {
    if (!g_domain_manager) {
        g_domain_manager = std::make_unique<DomainManager>();
        return g_domain_manager->Initialize();
    }
    return true;
}

void ShutdownMemoryDomains() {
    if (g_domain_manager) {
        g_domain_manager->Shutdown();
        g_domain_manager.reset();
    }
}

DomainManager* GetDomainManager() {
    return g_domain_manager.get();
}

// Utility functions for domain type conversion
DomainType StringToDomainType(const std::string& str) {
    if (str == "MAIN_RAM") return DomainType::MAIN_RAM;
    if (str == "DOS_CONVENTIONAL") return DomainType::DOS_CONVENTIONAL;
    if (str == "DOS_UMB") return DomainType::DOS_UMB;
    if (str == "EMS_PAGES") return DomainType::EMS_PAGES;
    if (str == "XMS_EXTENDED") return DomainType::XMS_EXTENDED;
    if (str == "VIDEO_RAM") return DomainType::VIDEO_RAM;
    if (str == "BIOS_ROM") return DomainType::BIOS_ROM;
    return DomainType::MAIN_RAM; // Default
}

const char* DomainTypeToString(DomainType type) {
    switch (type) {
        case DomainType::MAIN_RAM: return "MAIN_RAM";
        case DomainType::DOS_CONVENTIONAL: return "DOS_CONVENTIONAL";
        case DomainType::DOS_UMB: return "DOS_UMB";
        case DomainType::EMS_PAGES: return "EMS_PAGES";
        case DomainType::XMS_EXTENDED: return "XMS_EXTENDED";
        case DomainType::VIDEO_RAM: return "VIDEO_RAM";
        case DomainType::BIOS_ROM: return "BIOS_ROM";
        default: return "UNKNOWN";
    }
}

const char* AccessResultToString(AccessResult result) {
    switch (result) {
        case AccessResult::SUCCESS: return "SUCCESS";
        case AccessResult::ADDRESS_OUT_OF_RANGE: return "ADDRESS_OUT_OF_RANGE";
        case AccessResult::READ_ONLY_VIOLATION: return "READ_ONLY_VIOLATION";
        case AccessResult::UNMAPPED_MEMORY: return "UNMAPPED_MEMORY";
        case AccessResult::ACCESS_DENIED: return "ACCESS_DENIED";
        case AccessResult::DOMAIN_UNAVAILABLE: return "DOMAIN_UNAVAILABLE";
        default: return "UNKNOWN";
    }
}

} // namespace MemoryDomains
