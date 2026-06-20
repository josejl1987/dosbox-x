// Memory API Registration for DOSBox-X Lua Engine
// Separated from main LuaEngine.cpp for better organization

#include "luaengine.h"
#include "lua_memory_domains.h"

// Required includes for memory operations
#include "mem.h"
#include "paging.h"
#include "cpu.h"         // For ::GetAddress function
#include "debug.h"       // For DEBUG_ShowMsg

// Standard library includes
#include <vector>        // For std::vector
#include <algorithm>     // For std::min
#include <cctype>        // For std::toupper

// Forward declarations
extern LuaEngine luaEngine;

// Memory operation result structure for better error reporting
struct MemoryOperationResult {
    bool success;
    std::string error_message;
    size_t bytes_processed;
    uint32_t failed_address;
    
    MemoryOperationResult() : success(true), bytes_processed(0), failed_address(0) {}
    
    void setError(const std::string& message, uint32_t address = 0) {
        success = false;
        error_message = message;
        failed_address = address;
    }
};

// Memory Utility Functions
extern bool WriteBytes(uint16_t seg, uint32_t ofs, const std::vector<uint8_t>& data);
extern std::vector<uint8_t> GetBytes(uint16_t seg, uint32_t ofs, size_t size);

// Enhanced memory read function with error reporting
std::pair<std::vector<uint8_t>, MemoryOperationResult> GetBytesWithError(uint16_t seg, uint32_t ofs, size_t size) {
    MemoryOperationResult result;
    std::vector<uint8_t> buffer;
    
    if (size == 0) {
        result.setError("Invalid size: cannot read 0 bytes");
        return {buffer, result};
    }
    
    if (size > 1024 * 1024) { // 1MB limit for safety
        result.setError("Size too large: maximum 1MB allowed");
        return {buffer, result};
    }
    
    buffer.reserve(size);
    
    for (size_t i = 0; i < size; ++i) {
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, ofs + i));
        uint8_t value;
        
        if (!mem_readb_checked(phys, &value)) {
            buffer.push_back(value);
            result.bytes_processed++;
        } else {
            // Memory read failed
            result.setError("Memory read failed at address", phys);
            result.failed_address = (static_cast<uint32_t>(seg) << 16) | (ofs + i);
            break;
        }
    }
    
    return {buffer, result};
}

#ifdef C_LUA

void LuaEngine::registerMemoryAPI() {
    auto memory_table = lua.create_table();

    // Standard API functions (with error checking)
    memory_table["read"] = [this](uint16_t seg, uint32_t offset, size_t size) -> sol::table {
        if(sol2_cache.fast_memory_access) {
            // Fast path: reuse cached table and minimal error checking
            sol2_cache.cached_memory_result.clear();
            sol2_cache.temp_memory_buffer.clear();
            sol2_cache.temp_memory_buffer.reserve(size);

            // Optimize: calculate base address once and use offset arithmetic
            PhysPt base_phys = static_cast<PhysPt>(::GetAddress(seg, offset));
            
            for(size_t i = 0; i < size; ++i) {
                uint8_t value;
                if(!mem_readb_checked(base_phys + i, &value)) {
                    sol2_cache.temp_memory_buffer.push_back(value);
                }
                else {
                    break; // Stop on first error in fast mode
                }
            }

            // Populate cached table efficiently - avoid repeated index calculations
            const uint8_t* data_ptr = sol2_cache.temp_memory_buffer.data();
            size_t buffer_size = sol2_cache.temp_memory_buffer.size();
            
            for(size_t i = 0; i < buffer_size; ++i) {
                sol2_cache.cached_memory_result[i + 1] = data_ptr[i];
            }
            return sol2_cache.cached_memory_result;
        }
        else {
            // Standard path: full error checking and new table creation
            auto result = lua.create_table();
            auto bytes = GetBytes(seg, offset, size);
            for(size_t i = 0; i < bytes.size(); ++i) {
                result[i + 1] = bytes[i];
            }
            return result;
        }
        };

    memory_table["write"] = [this](uint16_t seg, uint32_t offset, sol::table data) -> bool {
        if(sol2_cache.fast_memory_access) {
            // Fast path: direct memory operations without full validation
            sol2_cache.temp_memory_buffer.clear();
            sol2_cache.temp_memory_buffer.reserve(data.size());

            for(size_t i = 1; i <= data.size(); ++i) {
                sol::optional<uint8_t> byte = data[i];
                if(byte) {
                    sol2_cache.temp_memory_buffer.push_back(byte.value());
                }
            }
            return WriteBytes(seg, offset, sol2_cache.temp_memory_buffer);
        }
        else {
            // Standard path with full validation
            std::vector<uint8_t> bytes;
            for(size_t i = 1; i <= data.size(); ++i) {
                sol::optional<uint8_t> byte = data[i];
                if(byte) {
                    bytes.push_back(byte.value());
                }
            }
            return WriteBytes(seg, offset, bytes);
        }
        };

    memory_table["readbyte"] = [this](uint16_t seg, uint32_t offset) -> sol::optional<uint8_t> {
        uint8_t value;
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, offset));
        if(!mem_readb_checked(phys, &value)) {
            // Fire memory read event (skip in fast mode if minimal error checking enabled)
            if(event_manager && !sol2_cache.minimal_error_checking) {
                LuaEngineEvents::MemoryEventData mem_event;
                mem_event.segment = seg;
                mem_event.offset = offset;
                mem_event.physical_addr = phys;
                mem_event.value = value;
                mem_event.size = 1;
                mem_event.is_write = false;
                event_manager->fireMemoryEvent(mem_event);
            }
            return value;
        }
        return sol::nullopt;
        };

    memory_table["writebyte"] = [this](uint16_t seg, uint32_t offset, uint8_t value) -> bool {
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, offset));
        uint8_t dummy;
        if(!mem_readb_checked(phys, &dummy)) {
            // Fire memory write event (skip in fast mode if minimal error checking enabled)
            if(event_manager && !sol2_cache.minimal_error_checking) {
                LuaEngineEvents::MemoryEventData mem_event;
                mem_event.segment = seg;
                mem_event.offset = offset;
                mem_event.physical_addr = phys;
                mem_event.value = value;
                mem_event.size = 1;
                mem_event.is_write = true;
                event_manager->fireMemoryEvent(mem_event);
            }
            mem_writeb(phys, value);
            return true;
        }
        return false;
        };

    // Word operations
    memory_table["readword"] = [this](uint16_t seg, uint32_t offset) -> sol::optional<uint16_t> {
        uint16_t value;
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, offset));
        if(!mem_readw_checked(phys, &value)) {
            if(event_manager && !sol2_cache.minimal_error_checking) {
                LuaEngineEvents::MemoryEventData mem_event;
                mem_event.segment = seg;
                mem_event.offset = offset;
                mem_event.physical_addr = phys;
                mem_event.value = value;
                mem_event.size = 2;
                mem_event.is_write = false;
                event_manager->fireMemoryEvent(mem_event);
            }
            return value;
        }
        return sol::nullopt;
        };

    memory_table["writeword"] = [this](uint16_t seg, uint32_t offset, uint16_t value) -> bool {
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, offset));
        uint16_t dummy;
        if(!mem_readw_checked(phys, &dummy)) {
            if(event_manager && !sol2_cache.minimal_error_checking) {
                LuaEngineEvents::MemoryEventData mem_event;
                mem_event.segment = seg;
                mem_event.offset = offset;
                mem_event.physical_addr = phys;
                mem_event.value = value;
                mem_event.size = 2;
                mem_event.is_write = true;
                event_manager->fireMemoryEvent(mem_event);
            }
            mem_writew(phys, value);
            return true;
        }
        return false;
        };

    // Utility functions
    memory_table["get_address"] = [](uint16_t seg, uint32_t offset) -> uint32_t {
        return static_cast<uint32_t>(::GetAddress(seg, offset));
        };

    memory_table["is_valid_address"] = [](uint16_t seg, uint32_t offset) -> bool {
        PhysPt phys = static_cast<PhysPt>(::GetAddress(seg, offset));
        uint8_t dummy;
        return !mem_readb_checked(phys, &dummy);
        };

    // Performance-oriented bulk operations
    memory_table["read_range"] = [this](uint16_t seg, uint32_t start_offset, uint32_t end_offset) -> sol::table {
        auto result = lua.create_table();
        if(end_offset <= start_offset) return result;
        
        size_t size = end_offset - start_offset;
        auto bytes = GetBytes(seg, start_offset, size);
        
        for(size_t i = 0; i < bytes.size(); ++i) {
            result[start_offset + i] = bytes[i];
        }
        return result;
        };

    // Simple pattern search over DOS memory domains (conventional and UMB)
    memory_table["search"] = [this](sol::object pattern_obj, sol::optional<std::string> domain_name) -> sol::table {
        auto result = lua.create_table();

        auto* domain_manager = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager();

        // Convert Lua table pattern into byte vector
        std::vector<uint8_t> search_pattern;
        auto fill_from_table = [&](const sol::table& tbl) {
            search_pattern.reserve(tbl.size());
            for (size_t i = 1; i <= tbl.size(); ++i) {
                sol::optional<uint8_t> byte = tbl[i];
                if (byte) search_pattern.push_back(byte.value());
            }
        };
        auto fill_from_string = [&](const std::string& s) {
            search_pattern.reserve(s.size());
            for (unsigned char c : s) search_pattern.push_back(c);
        };

        if (pattern_obj.is<sol::table>()) {
            fill_from_table(pattern_obj.as<sol::table>());
        } else if (pattern_obj.is<std::string>()) {
            fill_from_string(pattern_obj.as<std::string>());
        }

        if (search_pattern.empty()) return result;

        // Normalize domain name (strip spaces/underscores and uppercase)
        auto normalize = [](std::string s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == ' ' || c == '_' || c == '-') continue;
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            return out;
        };

        struct DomainEntry {
            LuaEngineMemoryDomains::MemoryDomain* domain;
            std::string label;
        };

        auto add_domain = [&](LuaEngineMemoryDomains::MemoryDomainType type) -> DomainEntry {
            DomainEntry entry{nullptr, ""};
            auto* dom = domain_manager->GetDomain(type);
            if (!dom) return entry;

            // Use the adapter's reported name so Lua gets the friendly label
            entry.domain = dom;
            entry.label = dom->getName();
            return entry;
        };

        std::vector<DomainEntry> domains_to_search;
        bool used_adapter_domains = false;
        std::string norm = domain_name ? normalize(domain_name.value()) : std::string();
        if (domain_manager) {
            bool restrict = !norm.empty();
            if (restrict) {
                if (norm == "DOSCONVENTIONAL") {
                    auto entry = add_domain(LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL);
                    if (entry.domain) {
                        domains_to_search.push_back(entry);
                        used_adapter_domains = true;
                    }
                } else if (norm == "DOSUMB") {
                    auto entry = add_domain(LuaEngineMemoryDomains::MemoryDomainType::DOS_UMB);
                    if (entry.domain) {
                        domains_to_search.push_back(entry);
                        used_adapter_domains = true;
                    }
                }
                // If no match, fall through to search all domains
            }

            if (!restrict || !used_adapter_domains) {
                auto conventional = add_domain(LuaEngineMemoryDomains::MemoryDomainType::DOS_CONVENTIONAL);
                if (conventional.domain) domains_to_search.push_back(conventional);
                auto umb = add_domain(LuaEngineMemoryDomains::MemoryDomainType::DOS_UMB);
                if (umb.domain) domains_to_search.push_back(umb);
                used_adapter_domains = !domains_to_search.empty();
            }
        }

        auto search_domain = [&](const DomainEntry& dom, int& out_index) {
            if (!dom.domain) return;

            const uint32_t base = dom.domain->getBaseAddress();
            const uint32_t size = dom.domain->getSize();
            if (size < search_pattern.size()) return;

            // Use adapter to read domain contents to respect domain mappings
            auto buffer = dom.domain->readBlock(base, size);
            if (buffer.size() < search_pattern.size()) return;

            auto it = std::search(buffer.begin(), buffer.end(),
                                  search_pattern.begin(), search_pattern.end());
            while (it != buffer.end()) {
                uint32_t offset = static_cast<uint32_t>(std::distance(buffer.begin(), it));
                auto entry = lua.create_table();
                entry["address"] = base + offset;
                entry["domain"] = dom.label;
                result[out_index++] = entry;

                it = std::search(it + 1, buffer.end(),
                                 search_pattern.begin(), search_pattern.end());
            }
        };

        int idx = 1;
        if (used_adapter_domains) {
            for (const auto& dom : domains_to_search) {
                search_domain(dom, idx);
            }
        } else {
            // Fallback to legacy direct memory scan if adapter domains aren't available
            struct LegacyDomainRange {
                uint32_t start;
                uint32_t size;
                const char* label;
            };

            LegacyDomainRange conventional{0x00000, 0xA0000, "DOS Conventional"};
            LegacyDomainRange umb{0xA0000, 0x60000, "DOS UMB"};

            std::vector<LegacyDomainRange> legacy_domains;
            bool restrict = !norm.empty();
            if (restrict) {
                if (norm == "DOSCONVENTIONAL") {
                    legacy_domains.push_back(conventional);
                } else if (norm == "DOSUMB") {
                    legacy_domains.push_back(umb);
                }
                // If not matched, fall through to search all
            }
            if (!restrict || legacy_domains.empty()) {
                legacy_domains.push_back(conventional);
                legacy_domains.push_back(umb);
            }

            auto legacy_search = [&](const LegacyDomainRange& dom, int& out_index) {
                if (dom.size < search_pattern.size()) return;

                std::vector<uint8_t> buffer;
                buffer.resize(dom.size);
                for (uint32_t i = 0; i < dom.size; ++i) {
                    buffer[i] = mem_readb(static_cast<PhysPt>(dom.start + i));
                }

                auto it = std::search(buffer.begin(), buffer.end(),
                                      search_pattern.begin(), search_pattern.end());
                while (it != buffer.end()) {
                    uint32_t offset = static_cast<uint32_t>(std::distance(buffer.begin(), it));
                    auto entry = lua.create_table();
                    entry["address"] = dom.start + offset;
                    entry["domain"] = dom.label;
                    result[out_index++] = entry;

                    it = std::search(it + 1, buffer.end(),
                                     search_pattern.begin(), search_pattern.end());
                }
            };

            for (const auto& dom : legacy_domains) {
                legacy_search(dom, idx);
            }
        }

        return result;
    };

    // Performance control
    memory_table["set_fast_mode"] = [this](bool enable) {
        sol2_cache.fast_memory_access = enable;
        DEBUG_ShowMsg("LuaEngine: Memory fast mode %s", enable ? "enabled" : "disabled");
        };

    // Enhanced memory read with detailed error reporting
    memory_table["read_safe"] = [this](uint16_t seg, uint32_t offset, size_t size) -> sol::table {
        auto [bytes, result] = GetBytesWithError(seg, offset, size);
        
        auto response = lua.create_table();
        response["success"] = result.success;
        response["bytes_read"] = result.bytes_processed;
        
        if (result.success) {
            auto data = lua.create_table();
            for (size_t i = 0; i < bytes.size(); ++i) {
                data[i + 1] = bytes[i];
            }
            response["data"] = data;
        } else {
            response["error"] = result.error_message;
            response["failed_address"] = result.failed_address;
        }
        
        return response;
        };

    lua["memory"] = memory_table;
    luaEngine.log_info("Memory API registered with optimized performance paths");
}
#endif
