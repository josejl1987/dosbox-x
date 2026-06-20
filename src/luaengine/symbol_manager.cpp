#include "symbol_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "symbolic_breakpoints.h"
#include "mem.h"
#include "paging.h"
#include "cpu.h"

namespace LuaEngineSymbols {

// Global symbol manager instance - now managed by LuaEngine
// This is initialized in LuaEngine::LUAENGINE_Init()
SymbolManager* g_symbol_manager = nullptr;

SymbolManager::SymbolManager() : loaded_format_(SymbolFormat::UNKNOWN) {
}

SymbolManager::~SymbolManager() {
}

bool SymbolManager::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if(!file.is_open()) {
        return false;
    }

    clearSymbols();

    auto parse_number = [](const std::string& text, uint32_t& out) -> bool {
        try {
            size_t idx = 0;
            out = static_cast<uint32_t>(std::stoul(text, &idx, 0));
            return idx == text.size();
        }
        catch(...) {
            return false;
        }
    };

    auto trim_line = [](const std::string& line) -> std::string {
        const auto first = line.find_first_not_of(" \t\r\n");
        if(first == std::string::npos) {
            return "";
        }
        const auto last = line.find_last_not_of(" \t\r\n");
        return line.substr(first, last - first + 1);
    };

    std::string line;
    size_t loaded = 0;
    while(std::getline(file, line)) {
        const std::string trimmed = trim_line(line);
        if(trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        std::istringstream iss(trimmed);
        std::string addr_str;
        std::string name;
        if(!(iss >> addr_str >> name)) {
            continue;
        }

        uint32_t address = INVALID_ADDRESS;
        uint16_t segment = 0;
        uint16_t offset = 0;

        const auto colon = addr_str.find(':');
        if(colon != std::string::npos) {
            std::string seg_part = addr_str.substr(0, colon);
            std::string off_part = addr_str.substr(colon + 1);

            uint32_t seg_val = 0;
            uint32_t off_val = 0;
            if(!parse_number(seg_part, seg_val) || !parse_number(off_part, off_val)) {
                continue;
            }

            segment = static_cast<uint16_t>(seg_val & 0xFFFF);
            offset = static_cast<uint16_t>(off_val & 0xFFFF);
            address = segmentOffsetToLinear(segment, offset);
        }
        else {
            uint32_t linear = 0;
            if(!parse_number(addr_str, linear)) {
                continue;
            }
            address = linear;
        }

        if(address == INVALID_ADDRESS) {
            continue;
        }

        Symbol sym;
        sym.address = address;
        sym.segment = segment;
        sym.offset = offset;
        sym.name = name;
        addSymbol(sym);
        ++loaded;
    }

    loaded_filename_ = filename;
    loaded_format_ = SymbolFormat::UNKNOWN;
    return loaded > 0;
}



void SymbolManager::clearSymbols() {
    symbols_by_address_.clear();
    symbols_by_name_.clear();
    segment_mappings_.clear();
    segment_by_number_.clear();
    loaded_filename_.clear();
    loaded_format_ = SymbolFormat::UNKNOWN;

    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }
}

std::string SymbolManager::getSymbolName(uint32_t address, bool include_offset) {
    if (symbols_by_address_.empty()) {
        return "";
    }
    
    // Find exact match first
    auto it = symbols_by_address_.find(address);
    if (it != symbols_by_address_.end()) {
        return it->second.name;
    }
    
    if (!include_offset) {
        return "";
    }
    
    // Find nearest symbol before this address
    auto lower = symbols_by_address_.lower_bound(address);
    if (lower != symbols_by_address_.begin()) {
        --lower;
        const Symbol& symbol = lower->second;
        
        // Check if address is within reasonable range of symbol
        uint32_t offset = address - symbol.address;
        if (offset < 0x10000) { // 64KB max offset
            return formatSymbolWithOffset(symbol, address);
        }
    }
    
    return "";
}

uint32_t SymbolManager::getSymbolAddress(const std::string& name) {
    auto it = symbols_by_name_.find(name);
    return (it != symbols_by_name_.end()) ? it->second : INVALID_ADDRESS;
}

Symbol* SymbolManager::findSymbol(uint32_t address) {
    auto it = symbols_by_address_.find(address);
    return (it != symbols_by_address_.end()) ? &it->second : nullptr;
}

Symbol* SymbolManager::findSymbolByName(const std::string& name) {
    auto it = symbols_by_name_.find(name);
    if (it != symbols_by_name_.end()) {
        auto addr_it = symbols_by_address_.find(it->second);
        return (addr_it != symbols_by_address_.end()) ? &addr_it->second : nullptr;
    }
    return nullptr;
}

std::vector<Symbol> SymbolManager::getAllSymbols() const {
    std::vector<Symbol> result;
    result.reserve(symbols_by_address_.size());
    
    for (const auto& pair : symbols_by_address_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<Symbol> SymbolManager::getSymbolsInRange(uint32_t start_addr, uint32_t end_addr) const {
    std::vector<Symbol> result;
    
    auto start_it = symbols_by_address_.lower_bound(start_addr);
    auto end_it = symbols_by_address_.upper_bound(end_addr);
    
    for (auto it = start_it; it != end_it; ++it) {
        result.push_back(it->second);
    }
    
    return result;
}

std::vector<std::string> SymbolManager::getSymbolNames() const {
    std::vector<std::string> result;
    result.reserve(symbols_by_name_.size());
    
    for (const auto& pair : symbols_by_name_) {
        result.push_back(pair.first);
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

uint32_t SymbolManager::segmentOffsetToLinear(uint16_t segment, uint16_t offset) {
    return (static_cast<uint32_t>(segment) << 4) + offset;
}

void SymbolManager::linearToSegmentOffset(uint32_t linear, uint16_t& segment, uint16_t& offset) {
    // Use the segment mapping system to find the appropriate segment
    // Check if we have a segment mapping that contains this linear address

    for (const auto& [seg_name, mapping] : segment_mappings_) {
        if (mapping.enabled && linear >= mapping.memory_base &&
            linear < mapping.memory_base + mapping.size) {
            // Found a segment that contains this linear address
            // Convert the segment name/number back to a segment value
            for (const auto& [seg_num, name] : segment_by_number_) {
                if (name == seg_name) {
                    segment = seg_num;
                    offset = static_cast<uint16_t>(linear - mapping.memory_base);
                    return;
                }
            }
        }
    }

    // Fallback: Use simple linear address to segment:offset conversion
    // This assumes standard memory model where segment = linear >> 4
    segment = static_cast<uint16_t>(linear >> 4);
    offset = static_cast<uint16_t>(linear & 0xFFFF);
}

void SymbolManager::addSymbol(const Symbol& symbol) {
    Symbol mapped_symbol = symbol;
    updateSymbolAddress(mapped_symbol);
    
    symbols_by_address_[mapped_symbol.address] = mapped_symbol;
    symbols_by_name_[mapped_symbol.name] = mapped_symbol.address;
}

void SymbolManager::removeSymbol(uint32_t address) {
    auto it = symbols_by_address_.find(address);
    if (it != symbols_by_address_.end()) {
        symbols_by_name_.erase(it->second.name);
        symbols_by_address_.erase(it);
    }
}

void SymbolManager::removeSymbol(const std::string& name) {
    auto it = symbols_by_name_.find(name);
    if (it != symbols_by_name_.end()) {
        symbols_by_address_.erase(it->second);
        symbols_by_name_.erase(it);
    }
}



std::string SymbolManager::formatSymbolWithOffset(const Symbol& symbol, uint32_t address) {
    uint32_t offset = address - symbol.address;
    if (offset == 0) {
        return symbol.name;
    } else {
        std::stringstream ss;
        ss << symbol.name << "+0x" << std::hex << std::uppercase << offset;
        return ss.str();
    }
}

// Segment mapping methods
void SymbolManager::addSegmentMapping(const SegmentMapping& mapping) {
    segment_mappings_[mapping.segment_name] = mapping;
    segment_by_number_[mapping.file_segment] = mapping.segment_name;
}

void SymbolManager::removeSegmentMapping(const std::string& segment_name) {
    auto it = segment_mappings_.find(segment_name);
    if (it != segment_mappings_.end()) {
        segment_by_number_.erase(it->second.file_segment);
        segment_mappings_.erase(it);
    }
}

void SymbolManager::updateSegmentMapping(const std::string& segment_name, uint32_t memory_base) {
    auto it = segment_mappings_.find(segment_name);
    if (it != segment_mappings_.end()) {
        it->second.memory_base = memory_base;
        // Remap all symbols that use this segment
        remapAllSymbols();
    }
}

void SymbolManager::enableSegmentMapping(const std::string& segment_name, bool enabled) {
    auto it = segment_mappings_.find(segment_name);
    if (it != segment_mappings_.end()) {
        it->second.enabled = enabled;
        // Remap all symbols
        remapAllSymbols();
    }
}

std::vector<SegmentMapping> SymbolManager::getSegmentMappings() const {
    std::vector<SegmentMapping> result;
    result.reserve(segment_mappings_.size());
    
    for (const auto& pair : segment_mappings_) {
        result.push_back(pair.second);
    }
    
    return result;
}

SegmentMapping* SymbolManager::findSegmentMapping(const std::string& segment_name) {
    auto it = segment_mappings_.find(segment_name);
    return (it != segment_mappings_.end()) ? &it->second : nullptr;
}

SegmentMapping* SymbolManager::findSegmentMapping(uint16_t file_segment) {
    auto it = segment_by_number_.find(file_segment);
    if (it != segment_by_number_.end()) {
        return findSegmentMapping(it->second);
    }
    return nullptr;
}

void SymbolManager::clearSegmentMappings() {
    segment_mappings_.clear();
    segment_by_number_.clear();
    // Remap all symbols to use default addressing
    remapAllSymbols();
}

void SymbolManager::autoDetectSegments() {
    // Auto-detect common segment patterns and create default mappings
    std::set<std::string> segment_names;
    std::set<uint16_t> segment_numbers;
    
    // Collect all unique segments from symbols
    for (const auto& pair : symbols_by_address_) {
        const Symbol& symbol = pair.second;
        if (!symbol.segment_name.empty()) {
            segment_names.insert(symbol.segment_name);
        }
        segment_numbers.insert(symbol.segment);
    }
    
    // Create default mappings for detected segments
    uint32_t base_address = 0x10000; // Start at 1000:0000
    
    for (const std::string& seg_name : segment_names) {
        if (segment_mappings_.find(seg_name) == segment_mappings_.end()) {
            // Find the file segment number for this segment name
            uint16_t file_seg = 0x1000; // Default
            for (const auto& pair : symbols_by_address_) {
                if (pair.second.segment_name == seg_name) {
                    file_seg = pair.second.segment;
                    break;
                }
            }
            
            SegmentMapping mapping(seg_name, file_seg, base_address);
            addSegmentMapping(mapping);
            
            base_address += 0x10000; // Next segment at next 64KB boundary
        }
    }
    
    // Create mappings for segments without names
    for (uint16_t seg_num : segment_numbers) {
        if (segment_by_number_.find(seg_num) == segment_by_number_.end()) {
            std::stringstream ss;
            ss << "SEG_" << std::hex << std::uppercase << seg_num;
            std::string seg_name = ss.str();
            
            SegmentMapping mapping(seg_name, seg_num, base_address);
            addSegmentMapping(mapping);
            
            base_address += 0x10000;
        }
    }
    
    // Remap all symbols with new mappings
    remapAllSymbols();
}

void SymbolManager::remapAllSymbols() {
    // Rebuild the address maps with updated segment mappings
    std::map<uint32_t, Symbol> new_symbols_by_address;
    std::map<std::string, uint32_t> new_symbols_by_name;
    
    for (auto& pair : symbols_by_address_) {
        Symbol& symbol = pair.second;
        updateSymbolAddress(symbol);
        
        new_symbols_by_address[symbol.address] = symbol;
        new_symbols_by_name[symbol.name] = symbol.address;
    }
    
    symbols_by_address_ = std::move(new_symbols_by_address);
    symbols_by_name_ = std::move(new_symbols_by_name);

    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }
}

void SymbolManager::recalculateAllSymbols() {
    remapAllSymbols();
}

uint32_t SymbolManager::mapAddressToMemory(uint16_t segment, uint16_t offset, const std::string& segment_name) {
    // First try to find mapping by segment name
    if (!segment_name.empty()) {
        auto mapping = findSegmentMapping(segment_name);
        if (mapping && mapping->enabled) {
            return mapping->memory_base + offset;
        }
    }
    
    // Then try to find mapping by segment number
    auto mapping = findSegmentMapping(segment);
    if (mapping && mapping->enabled) {
        return mapping->memory_base + offset;
    }
    
    // Fallback to default segment calculation
    return segmentOffsetToLinear(segment, offset);
}

void SymbolManager::updateSymbolAddress(Symbol& symbol) {
    // Store original file address if not already stored
    if (symbol.file_address == 0) {
        symbol.file_address = symbol.address;
    }
    
    // Calculate new address using segment mapping
    symbol.address = mapAddressToMemory(symbol.segment, symbol.offset, symbol.segment_name);
}

//=============================================================================
// PR6: Annotation management
//=============================================================================

void SymbolManager::addAnnotation(const Annotation& ann) {
    annotations_by_addr_.insert({ann.address, ann});
    // ponytail: simple text index — lowercase key for case-insensitive search
    std::string key = ann.comment;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    annotations_by_text_[key].push_back(ann.address);
}

std::vector<Annotation> SymbolManager::getAnnotationsByAddress(uint32_t address) const {
    std::vector<Annotation> result;
    auto range = annotations_by_addr_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

std::vector<Annotation> SymbolManager::getAnnotationsByText(const std::string& substring) const {
    std::vector<Annotation> result;
    std::string key = substring;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    for (const auto& kv : annotations_by_text_) {
        if (kv.first.find(key) != std::string::npos) {
            for (uint32_t addr : kv.second) {
                auto range = annotations_by_addr_.equal_range(addr);
                for (auto it = range.first; it != range.second; ++it) {
                    // Only include annotations whose comment matches the substring
                    std::string cmt = it->second.comment;
                    std::transform(cmt.begin(), cmt.end(), cmt.begin(), ::tolower);
                    if (cmt.find(key) != std::string::npos) {
                        result.push_back(it->second);
                    }
                }
            }
        }
    }
    return result;
}

bool SymbolManager::removeAnnotation(uint32_t address, const std::string& comment) {
    auto range = annotations_by_addr_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.comment == comment) {
            // Remove from text index
            std::string key = it->second.comment;
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            auto idx_it = annotations_by_text_.find(key);
            if (idx_it != annotations_by_text_.end()) {
                auto& vec = idx_it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), address), vec.end());
                if (vec.empty()) annotations_by_text_.erase(idx_it);
            }
            annotations_by_addr_.erase(it);
            return true;
        }
    }
    return false;
}

void SymbolManager::clearAnnotations() {
    annotations_by_addr_.clear();
    annotations_by_text_.clear();
}

//=============================================================================
// PR6: Typed data range management
//=============================================================================

bool SymbolManager::addTypedDataRange(const TypedDataRange& range) {
    // Validate no overlap within same module
    for (const auto& existing : typed_ranges_) {
        if (existing.module == range.module) {
            uint32_t ex_end = existing.start_address + existing.length;
            uint32_t new_end = range.start_address + range.length;
            if (range.start_address < ex_end && new_end > existing.start_address) {
                return false;  // Overlap detected
            }
        }
    }
    typed_ranges_.push_back(range);
    return true;
}

std::vector<TypedDataRange> SymbolManager::getTypedDataRangesAt(uint32_t address) const {
    std::vector<TypedDataRange> result;
    for (const auto& r : typed_ranges_) {
        if (address >= r.start_address && address < r.start_address + r.length) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<TypedDataRange> SymbolManager::getAllTypedDataRanges(const std::string& module) const {
    std::vector<TypedDataRange> result;
    for (const auto& r : typed_ranges_) {
        if (module.empty() || r.module == module) {
            result.push_back(r);
        }
    }
    return result;
}

bool SymbolManager::removeTypedDataRange(uint32_t start_address) {
    for (auto it = typed_ranges_.begin(); it != typed_ranges_.end(); ++it) {
        if (it->start_address == start_address) {
            typed_ranges_.erase(it);
            return true;
        }
    }
    return false;
}

void SymbolManager::clearTypedDataRanges() {
    typed_ranges_.clear();
}

} // namespace LuaEngineSymbols
