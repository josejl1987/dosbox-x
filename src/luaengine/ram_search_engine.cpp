#include "ram_search_engine.h"
#include "core_debug_interface.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <functional>
#include <condition_variable>
#include <future>

namespace LuaEngineRamSearch {

//=============================================================================
// SearchResult Implementation
//=============================================================================

std::string SearchResult::getCurrentValueString(WatchDisplayType display_type) const {
    return RamSearchEngine::formatValue(current_value, display_type, size);
}

std::string SearchResult::getPreviousValueString(WatchDisplayType display_type) const {
    return RamSearchEngine::formatValue(previous_value, display_type, size);
}

std::string SearchResult::getInitialValueString(WatchDisplayType display_type) const {
    return RamSearchEngine::formatValue(initial_value, display_type, size);
}

std::string SearchResult::getAddressString() const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << address;
    return ss.str();
}

std::string SearchResult::getFormattedValue() const {
    return RamSearchEngine::formatValue(current_value, WatchDisplayType::HEX, size);
}

std::string SearchResult::getFormattedPreviousValue() const {
    return RamSearchEngine::formatValue(previous_value, WatchDisplayType::HEX, size);
}

bool SearchResult::matchesFilter(const std::string& filter) const {
    if (filter.empty()) return true;
    
    std::string lower_filter = filter;
    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);
    
    // Check address
    std::string addr_str = getAddressString();
    std::transform(addr_str.begin(), addr_str.end(), addr_str.begin(), ::tolower);
    if (addr_str.find(lower_filter) != std::string::npos) return true;
    
    // Check current value
    std::string value_str = getFormattedValue();
    std::transform(value_str.begin(), value_str.end(), value_str.begin(), ::tolower);
    if (value_str.find(lower_filter) != std::string::npos) return true;
    
    // Check domain name
    std::string domain_lower = domain_name;
    std::transform(domain_lower.begin(), domain_lower.end(), domain_lower.begin(), ::tolower);
    if (domain_lower.find(lower_filter) != std::string::npos) return true;
    
    return false;
}

//=============================================================================
// RamSearchEngine Implementation
//=============================================================================

RamSearchEngine::RamSearchEngine()
    : current_domain_("DOS Conventional"),
      current_size_(WatchSize::BYTE_1), display_type_(WatchDisplayType::UNSIGNED),
      search_running_(false), search_cancelled_(false), cache_enabled_(true) {
    setupCodepageNames();
}

RamSearchEngine::~RamSearchEngine() {
    if (search_thread_.joinable()) {
        search_cancelled_ = true;
        search_thread_.join();
    }
}

void RamSearchEngine::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    // Store the reference to the adapter
    // We'll access it through GetGlobalMemoryDomainManager() when needed
    (void)memory_mgr; // Suppress unused parameter warning
}

//=============================================================================
// Search Operations
//=============================================================================

void RamSearchEngine::startNewSearch() {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return;
    
    auto* domain = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->getDomain(current_domain_);
    if (!domain) return;
    
    // Clear previous results and history
    results_.clear();
    undo_history_.clear();
    
    uint32_t domain_size = domain->getSize();
    uint32_t base_address = domain->getBaseAddress();
    uint32_t step_size = static_cast<uint32_t>(current_size_);
    
    // Reserve space for performance
    size_t estimated_results = domain_size / step_size;
    if (estimated_results > MAX_RESULTS) {
        estimated_results = MAX_RESULTS;
    }
    results_.reserve(estimated_results);
    
    // Scan entire domain
    for (uint32_t addr = base_address; addr < base_address + domain_size; addr += step_size) {
        if (results_.size() >= MAX_RESULTS) {
            break;  // Prevent excessive memory usage
        }
        
        if (domain->isAddressValid(addr)) {
            uint64_t value = readValue(addr, current_size_);
            results_.emplace_back(addr, value, current_size_);
        }
    }
    
    // Save initial state to undo history
    undo_history_.push_back(results_);
    if (undo_history_.size() > MAX_UNDO_HISTORY) {
        undo_history_.erase(undo_history_.begin());
    }
}

void RamSearchEngine::updateValues() {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return;
    
    for (auto& result : results_) {
        result.previous_value = result.current_value;
        result.current_value = readValue(result.address, current_size_);
        
        if (result.current_value != result.previous_value) {
            result.change_count++;
        }
    }
}

void RamSearchEngine::doSearch(SearchOperator op, CompareType compare_type, uint64_t value) {
    if (results_.empty()) return;
    
    // Save current state to undo history
    undo_history_.push_back(results_);
    if (undo_history_.size() > MAX_UNDO_HISTORY) {
        undo_history_.erase(undo_history_.begin());
    }
    
    // Update values first
    updateValues();
    
    // Filter results based on comparison
    std::vector<SearchResult> filtered_results;
    filtered_results.reserve(results_.size());
    
    for (const auto& result : results_) {
        uint64_t compare_value = value;
        uint64_t current_val = result.current_value;
        uint64_t comparison_val = result.previous_value;
        
        // Determine what to compare against
        switch (compare_type) {
            case CompareType::PREVIOUS_VALUE:
                comparison_val = result.previous_value;
                break;
            case CompareType::SPECIFIC_VALUE:
                comparison_val = value;
                break;
            case CompareType::INITIAL_VALUE:
                comparison_val = result.initial_value;
                break;
            case CompareType::CHANGE_COUNT:
                current_val = result.change_count;
                comparison_val = value;
                break;
        }
        
        if (compareValues(current_val, comparison_val, compare_value, op, current_size_)) {
            filtered_results.push_back(result);
        }
    }
    
    results_ = std::move(filtered_results);
}

void RamSearchEngine::undo() {
    if (undo_history_.empty()) return;
    
    results_ = undo_history_.back();
    undo_history_.pop_back();
}

void RamSearchEngine::clearResults() {
    results_.clear();
    undo_history_.clear();
}

//=============================================================================
// Settings
//=============================================================================

void RamSearchEngine::setDomain(const std::string& domain) {
    if (isValidDomain(domain)) {
        current_domain_ = domain;
    }
}

void RamSearchEngine::setSize(WatchSize size) {
    current_size_ = size;
}

void RamSearchEngine::setDisplayType(WatchDisplayType display_type) {
    display_type_ = display_type;
}

//=============================================================================
// Utility Methods
//=============================================================================

std::vector<std::string> RamSearchEngine::getAvailableDomains() const {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return {};
    return LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->getDomainNames();
}

bool RamSearchEngine::isValidDomain(const std::string& domain) const {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return false;
    return LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->getDomain(domain) != nullptr;
}

uint32_t RamSearchEngine::getAddressRange() const {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return 0;
    auto* domain = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->getDomain(current_domain_);
    if (!domain) return 0;
    return domain->getSize();
}

//=============================================================================
// Helper Methods
//=============================================================================

uint64_t RamSearchEngine::readValue(uint32_t address, WatchSize size) const {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return 0;
    
    switch (size) {
        case WatchSize::BYTE_1:
            return LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readByte(current_domain_, address);
        case WatchSize::BYTE_2:
            return LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readWord(current_domain_, address);
        case WatchSize::BYTE_4:
            return LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readDWord(current_domain_, address);
        case WatchSize::BYTE_8:
            // For 8-byte values, read as two 4-byte values
            {
                uint64_t low = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readDWord(current_domain_, address);
                uint64_t high = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readDWord(current_domain_, address + 4);
                return low | (high << 32);
            }
        default:
            return 0;
    }
}

bool RamSearchEngine::compareValues(uint64_t value1, uint64_t value2, uint64_t compare_value, 
                                   SearchOperator op, WatchSize size) const {
    // Convert to signed values if needed
    int64_t signed_value1 = static_cast<int64_t>(value1);
    int64_t signed_value2 = static_cast<int64_t>(value2);
    int64_t signed_compare = static_cast<int64_t>(compare_value);
    
    // Handle different size types for signed comparison
    switch (size) {
        case WatchSize::BYTE_1:
            signed_value1 = static_cast<int8_t>(value1);
            signed_value2 = static_cast<int8_t>(value2);
            signed_compare = static_cast<int8_t>(compare_value);
            break;
        case WatchSize::BYTE_2:
            signed_value1 = static_cast<int16_t>(value1);
            signed_value2 = static_cast<int16_t>(value2);
            signed_compare = static_cast<int16_t>(compare_value);
            break;
        case WatchSize::BYTE_4:
            signed_value1 = static_cast<int32_t>(value1);
            signed_value2 = static_cast<int32_t>(value2);
            signed_compare = static_cast<int32_t>(compare_value);
            break;
        case WatchSize::BYTE_8:
            // Already int64_t
            break;
    }
    
    switch (op) {
        case SearchOperator::EQUAL:
            return value1 == value2;
        case SearchOperator::NOT_EQUAL:
            return value1 != value2;
        case SearchOperator::LESS_THAN:
            return signed_value1 < signed_value2;
        case SearchOperator::GREATER_THAN:
            return signed_value1 > signed_value2;
        case SearchOperator::LESS_EQUAL:
            return signed_value1 <= signed_value2;
        case SearchOperator::GREATER_EQUAL:
            return signed_value1 >= signed_value2;
        case SearchOperator::DIFFERENT_BY:
            return std::abs(signed_value1 - signed_value2) == signed_compare;
        case SearchOperator::MODULO:
            return (value1 % compare_value) == 0;
        case SearchOperator::CHANGED:
            return value1 != value2;
        case SearchOperator::UNCHANGED:
            return value1 == value2;
        case SearchOperator::INCREASED:
            return signed_value1 > signed_value2;
        case SearchOperator::DECREASED:
            return signed_value1 < signed_value2;
        case SearchOperator::INCREASED_BY:
            return (signed_value1 - signed_value2) == signed_compare;
        case SearchOperator::DECREASED_BY:
            return (signed_value2 - signed_value1) == signed_compare;
        default:
            return false;
    }
}

std::string RamSearchEngine::formatValue(uint64_t value, WatchDisplayType display_type, WatchSize size) {
    std::stringstream ss;
    
    switch (display_type) {
        case WatchDisplayType::UNSIGNED:
            ss << value;
            break;
        case WatchDisplayType::SIGNED:
            switch (size) {
                case WatchSize::BYTE_1:
                    ss << static_cast<int8_t>(value);
                    break;
                case WatchSize::BYTE_2:
                    ss << static_cast<int16_t>(value);
                    break;
                case WatchSize::BYTE_4:
                    ss << static_cast<int32_t>(value);
                    break;
                case WatchSize::BYTE_8:
                    ss << static_cast<int64_t>(value);
                    break;
            }
            break;
        case WatchDisplayType::HEX:
            ss << "0x" << std::hex << std::uppercase << value;
            break;
        case WatchDisplayType::BINARY:
            // Convert to binary string
            {
                std::string binary;
                uint64_t temp = value;
                int bits = static_cast<int>(size) * 8;
                for (int i = bits - 1; i >= 0; --i) {
                    binary += (temp & (1ULL << i)) ? '1' : '0';
                    if (i > 0 && i % 4 == 0) binary += ' ';  // Add space every 4 bits
                }
                ss << binary;
            }
            break;
        case WatchDisplayType::FLOAT:
            if (size == WatchSize::BYTE_4) {
                float f_val = *reinterpret_cast<const float*>(&value);
                ss << f_val;
            } else if (size == WatchSize::BYTE_8) {
                double d_val = *reinterpret_cast<const double*>(&value);
                ss << d_val;
            } else {
                ss << value;  // Fallback to unsigned
            }
            break;
        default:
            ss << value;
            break;
    }
    
    return ss.str();
}

//=============================================================================
// Export/Import
//=============================================================================

void RamSearchEngine::exportResults(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    
    // Write header
    file << "# DOSBox-X RAM Search Results\n";
    file << "# Domain: " << current_domain_ << "\n";
    file << "# Size: " << static_cast<int>(current_size_) << "\n";
    file << "# Display Type: " << static_cast<int>(display_type_) << "\n";
    file << "# Results: " << results_.size() << "\n";
    file << "#\n";
    file << "# Address,Current,Previous,Initial,Changes\n";
    
    // Write results
    for (const auto& result : results_) {
        file << result.getAddressString() << ","
             << result.current_value << ","
             << result.previous_value << ","
             << result.initial_value << ","
             << result.change_count << "\n";
    }
}

void RamSearchEngine::importResults(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    
    results_.clear();
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        // Parse CSV line
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() >= 5) {
            uint32_t address = std::stoul(tokens[0], nullptr, 16);
            uint64_t current = std::stoull(tokens[1]);
            uint64_t previous = std::stoull(tokens[2]);
            uint64_t initial = std::stoull(tokens[3]);
            uint32_t changes = std::stoul(tokens[4]);
            
            SearchResult result(address, current, current_size_);
            result.previous_value = previous;
            result.initial_value = initial;
            result.change_count = changes;
            
            results_.push_back(result);
        }
    }
}

// Missing method implementations
void RamSearchEngine::setSearchValue(const std::string& value) {
    // This method would parse the value and set it for search comparison
    // For now, just store as a string (proper implementation would depend on display type)
    search_value_string_ = value;
}

void RamSearchEngine::setMaxResults(size_t max_results) {
    if (max_results > 0) {
        max_results_ = max_results;
    }
}

void RamSearchEngine::performSearch(SearchOperator op) {
    // This would perform the actual search operation
    // For now, just call the existing doSearch method
    doSearch(op, CompareType::SPECIFIC_VALUE, 0);
}

void RamSearchEngine::onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value) {
    // Update any results that match this address
    for (auto& result : results_) {
        if (result.address == address) {
            result.previous_value = result.current_value;
            result.current_value = new_value;
            if (result.previous_value != result.current_value) {
                result.change_count++;
            }
        }
    }
}

// Static member initialization
std::map<uint16_t, std::string> RamSearchEngine::codepage_names_;

// Additional missing method implementations
bool RamSearchEngine::initialize() {
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return false;
    setupCodepageNames();
    return true;
}

void RamSearchEngine::shutdown() {
    if (search_thread_.joinable()) {
        search_cancelled_ = true;
        search_thread_.join();
    }
    clearResults();
}

bool RamSearchEngine::startNewSearch(const SearchConfig& config) {
    current_config_ = config;
    startNewSearch();
    return true;
}

bool RamSearchEngine::startFilteredSearch(const SearchConfig& config) {
    current_config_ = config;
    // Apply filter based on config
    performSearch(config.operation);
    return true;
}

void RamSearchEngine::cancelSearch() {
    search_cancelled_ = true;
    if (search_thread_.joinable()) {
        search_thread_.join();
    }
    search_running_ = false;
}

SearchResult* RamSearchEngine::getResult(size_t index) {
    if (index >= results_.size()) return nullptr;
    return &results_[index];
}

const SearchResult* RamSearchEngine::getResult(size_t index) const {
    if (index >= results_.size()) return nullptr;
    return &results_[index];
}

bool RamSearchEngine::isDomainEnabled(const std::string& domain) const {
    // For now, assume all valid domains are enabled
    return isValidDomain(domain);
}

void RamSearchEngine::enableDomain(const std::string& domain, bool enabled) {
    // Implementation would depend on how domain enabling is tracked
    // For now, this is a no-op
}

std::string RamSearchEngine::getSearchOperatorName(SearchOperator op) const {
    switch (op) {
        case SearchOperator::EQUAL: return "Equal";
        case SearchOperator::NOT_EQUAL: return "Not Equal";
        case SearchOperator::LESS_THAN: return "Less Than";
        case SearchOperator::GREATER_THAN: return "Greater Than";
        case SearchOperator::LESS_EQUAL: return "Less or Equal";
        case SearchOperator::GREATER_EQUAL: return "Greater or Equal";
        case SearchOperator::DIFFERENT_BY: return "Different By";
        case SearchOperator::MODULO: return "Modulo";
        case SearchOperator::CHANGED: return "Changed";
        case SearchOperator::UNCHANGED: return "Unchanged";
        case SearchOperator::INCREASED: return "Increased";
        case SearchOperator::DECREASED: return "Decreased";
        case SearchOperator::INCREASED_BY: return "Increased By";
        case SearchOperator::DECREASED_BY: return "Decreased By";
        default: return "Unknown";
    }
}

std::string RamSearchEngine::getDataTypeName(SearchDataType type) const {
    switch (type) {
        case SearchDataType::BYTE: return "Byte";
        case SearchDataType::WORD: return "Word";
        case SearchDataType::DWORD: return "DWord";
        case SearchDataType::FLOAT: return "Float";
        case SearchDataType::DOUBLE: return "Double";
        case SearchDataType::STRING: return "String";
        case SearchDataType::ARRAY: return "Array";
        default: return "Unknown";
    }
}

std::vector<SearchResult> RamSearchEngine::filterResults(const std::string& filter) const {
    std::vector<SearchResult> filtered;
    for (const auto& result : results_) {
        if (result.matchesFilter(filter)) {
            filtered.push_back(result);
        }
    }
    return filtered;
}

void RamSearchEngine::sortResults(const std::function<bool(const SearchResult&, const SearchResult&)>& comparator) {
    std::sort(results_.begin(), results_.end(), comparator);
}

void RamSearchEngine::setProgressCallback(std::function<void(const SearchProgress&)> callback) {
    progress_callback_ = callback;
}

void RamSearchEngine::setDefaultConfig(const SearchConfig& config) {
    current_config_ = config;
}

void RamSearchEngine::setupCodepageNames() {
    if (codepage_names_.empty()) {
        codepage_names_[932] = "Japanese (Shift-JIS)";
        codepage_names_[936] = "Chinese Simplified (GBK)";
        codepage_names_[949] = "Korean";
        codepage_names_[950] = "Chinese Traditional (Big5)";
        codepage_names_[1252] = "Western European (Windows)";
        codepage_names_[65001] = "UTF-8";
    }
}

void RamSearchEngine::searchThreadFunc(const SearchConfig& config) {
    search_running_ = true;
    search_cancelled_ = false;
    
    // Reset search stats
    search_stats_.reset();
    search_stats_.search_start = std::chrono::steady_clock::now();
    
    // Perform the search based on config
    performSearch(config.operation);
    
    search_stats_.search_end = std::chrono::steady_clock::now();
    search_stats_.search_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        search_stats_.search_end - search_stats_.search_start);
    
    search_running_ = false;
    
    // Notify completion
    if (progress_callback_) {
        SearchProgress progress;
        progress.completed = true;
        progress.cancelled = search_cancelled_;
        progress.results_found = results_.size();
        progress_callback_(progress);
    }
}

void RamSearchEngine::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    address_cache_.clear();
}

size_t RamSearchEngine::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return address_cache_.size();
}

void RamSearchEngine::updateProgress(const SearchProgress& progress) {
    if (progress_callback_) {
        progress_callback_(progress);
    }
}

void RamSearchEngine::updateCache(uint32_t address, uint64_t value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Update cache with the specific address and value
    address_cache_[address] = value;
}

bool RamSearchEngine::hasCachedValue(uint32_t address) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return address_cache_.find(address) != address_cache_.end();
}

uint64_t RamSearchEngine::getCachedValue(uint32_t address) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = address_cache_.find(address);
    return (it != address_cache_.end()) ? it->second : 0;
}

std::string RamSearchEngine::getCodepageName(uint16_t codepage) const {
    auto it = codepage_names_.find(codepage);
    return (it != codepage_names_.end()) ? it->second : "Unknown";
}

std::string RamSearchEngine::convertToUTF8(const std::string& text, uint16_t codepage) const {
    // Use core debugger for proper codepage conversion if available
    if (LuaEngineDebug::g_core_debugger) {
        return LuaEngineDebug::g_core_debugger->convertToUTF8(
            reinterpret_cast<const uint8_t*>(text.data()), text.length(), codepage);
    }
    // Fallback: return as-is if core debugger not available
    return text;
}

std::string RamSearchEngine::convertFromUTF8(const std::string& text, uint16_t codepage) const {
    // Use core debugger for proper codepage conversion if available
    if (LuaEngineDebug::g_core_debugger) {
        std::vector<uint8_t> encoded = LuaEngineDebug::g_core_debugger->convertFromUTF8(text, codepage);
        return std::string(encoded.begin(), encoded.end());
    }
    // Fallback: return as-is if core debugger not available
    return text;
}

void RamSearchEngine::completeSearch() {
    search_running_ = false;
    if (progress_callback_) {
        SearchProgress progress;
        progress.completed = true;
        progress.results_found = results_.size();
        progress_callback_(progress);
    }
}

uint64_t RamSearchEngine::readValueFromMemory(uint32_t address, SearchDataType data_type, const std::string& domain) const {
    return readValue(address, current_size_);
}

std::string RamSearchEngine::readTextFromMemory(uint32_t address, size_t length, uint16_t codepage, const std::string& domain) const {
    std::string result;
    if (!LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()) return result;
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = LuaEngineMemoryDomains::GetGlobalMemoryDomainManager()->readByte(domain, address + i);
        if (byte == 0) break; // Null terminator
        result += static_cast<char>(byte);
    }
    
    return convertToUTF8(result, codepage);
}

uint64_t RamSearchEngine::parseValue(const std::string& value_str, SearchDataType data_type) const {
    try {
        if (value_str.empty()) return 0;
        
        // Handle hex values
        if (value_str.size() > 2 && value_str.substr(0, 2) == "0x") {
            return std::stoull(value_str, nullptr, 16);
        }
        
        switch (data_type) {
            case SearchDataType::BYTE:
            case SearchDataType::WORD:
            case SearchDataType::DWORD:
                return std::stoull(value_str);
            case SearchDataType::FLOAT: {
                float f = std::stof(value_str);
                return *reinterpret_cast<uint32_t*>(&f);
            }
            case SearchDataType::DOUBLE: {
                double d = std::stod(value_str);
                return *reinterpret_cast<uint64_t*>(&d);
            }
            default:
                return std::stoull(value_str);
        }
    } catch (const std::exception&) {
        return 0;
    }
}

void RamSearchEngine::searchForText(const std::string& text, uint16_t codepage) {
    // Implementation for text search
    // This would search for the text string in memory
}

void RamSearchEngine::searchForPointer(uint32_t target_address) {
    // Implementation for pointer search
    // This would search for addresses that point to target_address
}

void RamSearchEngine::searchForByteArray(const std::vector<uint8_t>& pattern) {
    // Implementation for byte array search
    // This would search for the specific byte pattern
}

void RamSearchEngine::searchDomain(const std::string& domain_name) {
    // Implementation for domain-specific search
    setDomain(domain_name);
    startNewSearch();
}

std::string RamSearchEngine::formatValue(uint64_t value, SearchDataType data_type, bool hex_format) const {
    return formatValue(value, hex_format ? WatchDisplayType::HEX : WatchDisplayType::UNSIGNED, current_size_);
}

} // namespace LuaEngineRamSearch