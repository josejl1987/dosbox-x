#ifndef RAM_SEARCH_ENGINE_H
#define RAM_SEARCH_ENGINE_H

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <future>
#include "../../include/memory_domains.h"  // Use main memory domain system directly
#include "lua_memory_domains.h"  // Include the compatibility layer

namespace LuaEngineRamSearch {

// Forward declarations and types
struct SearchProgress {
    size_t addresses_scanned = 0;
    size_t total_addresses = 0;
    size_t results_found = 0;
    std::string current_domain;
    bool completed = false;
    bool cancelled = false;
    double getPercentComplete() const { return total_addresses > 0 ? (double)addresses_scanned / total_addresses * 100.0 : 0.0; }
};

// Forward declare SearchDataType enum for compatibility
enum class SearchDataType {
    BYTE = 1,
    WORD = 2,
    DWORD = 4,
    FLOAT,
    DOUBLE,
    STRING,
    ARRAY
};

// Data size for search
enum class WatchSize {
    BYTE_1 = 1,
    BYTE_2 = 2, 
    BYTE_4 = 4,
    BYTE_8 = 8
};

// Display type for values
enum class WatchDisplayType {
    UNSIGNED,
    SIGNED,
    HEX,
    BINARY,
    FLOAT
};

// Search comparison operators
enum class SearchOperator {
    EQUAL,
    NOT_EQUAL,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    DIFFERENT_BY,
    MODULO,
    CHANGED,
    UNCHANGED,
    INCREASED,
    DECREASED,
    INCREASED_BY,
    DECREASED_BY
};

// What to compare against
enum class CompareType {
    PREVIOUS_VALUE,
    SPECIFIC_VALUE,
    INITIAL_VALUE,
    CHANGE_COUNT
};

// Individual search result
struct SearchResult {
    uint32_t address;
    uint64_t current_value;
    uint64_t previous_value;
    uint64_t initial_value;
    uint32_t change_count;
    WatchSize size;
    
    // Additional fields needed by ram_search.cpp
    SearchDataType data_type;
    std::string domain_name;
    std::string current_text;
    std::string previous_text;
    uint16_t codepage;
    
    SearchResult(uint32_t addr, uint64_t val, WatchSize sz) 
        : address(addr), current_value(val), previous_value(val), 
          initial_value(val), change_count(0), size(sz), 
          data_type(SearchDataType::BYTE), codepage(932) {}
    
    // Helper methods for display
    std::string getCurrentValueString(WatchDisplayType display_type) const;
    std::string getPreviousValueString(WatchDisplayType display_type) const;
    std::string getInitialValueString(WatchDisplayType display_type) const;
    std::string getAddressString() const;
    
    // Additional compatibility methods for ram_search.cpp
    std::string getFormattedValue() const;
    std::string getFormattedPreviousValue() const;
    bool matchesFilter(const std::string& filter) const;
    
    // Helper for getting value as different types
    template<typename T>
    T getCurrentValueAs() const {
        return static_cast<T>(current_value);
    }
    
    template<typename T>
    T getPreviousValueAs() const {
        return static_cast<T>(previous_value);
    }
};

// Main RAM search engine class
class RamSearchEngine {
private:
    // Current search state  
    std::vector<SearchResult> results_;
    std::vector<std::vector<SearchResult>> undo_history_;
    
    // Search parameters
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    std::string current_domain_;
    WatchSize current_size_;
    WatchDisplayType display_type_;
    
    // Search settings
    static const size_t MAX_UNDO_HISTORY = 100;
    static const size_t MAX_RESULTS = 1000000;  // Limit results for performance
    
    // Search state
    std::string search_value_string_;
    size_t max_results_ = MAX_RESULTS;
    
    // Threading support (needed by ram_search.cpp)
    std::atomic<bool> search_running_;
    std::atomic<bool> search_cancelled_;
    std::thread search_thread_;
    
    // Results management (needed by ram_search.cpp)
    std::vector<SearchResult> current_results_;
    std::vector<SearchResult> previous_results_;
    mutable std::mutex results_mutex_;
    
    // Statistics and progress (needed by ram_search.cpp)
    struct SearchStats {
        size_t total_addresses_scanned = 0;
        size_t results_found = 0;
        size_t domains_searched = 0;
        std::chrono::milliseconds search_time{0};
        std::chrono::steady_clock::time_point search_start;
        std::chrono::steady_clock::time_point search_end;
        
        void reset() {
            total_addresses_scanned = 0;
            results_found = 0;
            domains_searched = 0;
            search_time = std::chrono::milliseconds(0);
        }
    } search_stats_;
    
    // Cache management (needed by ram_search.cpp)
    std::map<uint32_t, uint64_t> address_cache_;
    bool cache_enabled_ = true;
    mutable std::mutex cache_mutex_;
    
    // Domain manager (needed by ram_search.cpp)
    std::unique_ptr<LuaEngineMemoryDomains::MemoryDomainManager> domain_manager_;
    
public:
    // Configuration (needed by ram_search.cpp) - make public
    struct SearchConfig {
        SearchOperator operation;
        SearchDataType data_type;
        std::vector<std::string> enabled_domains;
        size_t max_results = 10000;
        uint32_t address_alignment = 1;
        std::string search_value;
        bool hex_values = false;
        uint16_t codepage = 932;
    } current_config_;
    
    // Helper methods
    uint64_t readValue(uint32_t address, WatchSize size) const;
    bool compareValues(uint64_t value1, uint64_t value2, uint64_t compare_value, 
                      SearchOperator op, WatchSize size) const;
    
    // Additional private methods needed by ram_search.cpp
    bool compareValues(uint64_t current, uint64_t previous, uint64_t target,
                      SearchOperator op, SearchDataType type) const;
    uint64_t readValueFromMemory(uint32_t address, SearchDataType type,
                                const std::string& domain) const;
    std::string readTextFromMemory(uint32_t address, size_t length,
                                  const std::string& domain, uint16_t codepage) const;
    void updateProgress(size_t addresses_scanned, size_t total_addresses,
                       size_t results_found, const std::string& current_domain);
    void completeSearch(bool cancelled = false);
    void searchDomain(const std::string& domain_name, const SearchConfig& config,
                     std::vector<SearchResult>& results);
    void performSearchInternal(const SearchConfig& config);
    void updateCache(uint32_t address, uint64_t value);
    
    // Progress tracking
    SearchProgress current_progress_;

public:
    // Static utility method for formatting values
    static std::string formatValue(uint64_t value, WatchDisplayType display_type, WatchSize size);

private:
    
public:
    RamSearchEngine();
    ~RamSearchEngine();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    bool initialize();  // Additional overload needed by ram_search.cpp
    void shutdown();     // Additional method needed by ram_search.cpp
    
    // Search operations
    void startNewSearch();
    bool startNewSearch(const SearchConfig& config);  // Additional overload
    bool startFilteredSearch(const SearchConfig& config);  // Additional overload  
    void updateValues();
    void doSearch(SearchOperator op, CompareType compare_type, uint64_t value = 0);
    void undo();
    void clearResults();
    void cancelSearch();  // Additional method needed by ram_search.cpp
    
    // Results access (needed by ram_search.cpp)
    SearchResult* getResult(size_t index);
    const SearchResult* getResult(size_t index) const;
    
    // Settings
    void setDomain(const std::string& domain);
    void setSize(WatchSize size);
    void setDisplayType(WatchDisplayType display_type);
    void setMaxResults(size_t max_results);
    void performSearch(SearchOperator op);
    void onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value);
    
    // Getters
    const std::vector<SearchResult>& getResults() const { return results_; }
    size_t getResultCount() const { return results_.size(); }
    const std::string& getCurrentDomain() const { return current_domain_; }
    WatchSize getCurrentSize() const { return current_size_; }
    WatchDisplayType getDisplayType() const { return display_type_; }
    bool canUndo() const { return !undo_history_.empty(); }
    
    // Statistics
    uint32_t getAddressRange() const;
    size_t getMaxResults() const { return MAX_RESULTS; }
    
    // Utility methods  
    std::vector<std::string> getAvailableDomains() const;
    bool isValidDomain(const std::string& domain) const;
    bool isDomainEnabled(const std::string& domain) const;
    void enableDomain(const std::string& domain, bool enabled = true);
    std::string getSearchOperatorName(SearchOperator op) const;
    std::string getDataTypeName(SearchDataType type) const;
    std::vector<SearchResult> filterResults(const std::string& filter) const;
    void sortResults(const std::function<bool(const SearchResult&, const SearchResult&)>& comparator);
    void setProgressCallback(std::function<void(const SearchProgress&)> callback);
    void setDefaultConfig(const SearchConfig& config);
    
    // Export/Import
    void exportResults(const std::string& filename) const;
    
    // Configuration methods called by DebugToolsManager
    void setSearchValue(const std::string& value);
    void importResults(const std::string& filename);
    
    // Additional methods needed by ram_search.cpp
    void setupCodepageNames();
    void searchThreadFunc(const SearchConfig& config);
    void clearCache();
    size_t getCacheSize() const;
    void updateProgress(const SearchProgress& progress);
    bool hasCachedValue(uint32_t address) const;
    uint64_t getCachedValue(uint32_t address) const;
    std::string getCodepageName(uint16_t codepage) const;
    std::string convertToUTF8(const std::string& text, uint16_t codepage) const;
    std::string convertFromUTF8(const std::string& text, uint16_t codepage) const;
    void completeSearch();
    std::string readTextFromMemory(uint32_t address, size_t length, uint16_t codepage = 932, const std::string& domain = "") const;
    uint64_t parseValue(const std::string& value_str, SearchDataType data_type) const;
    void searchForText(const std::string& text, uint16_t codepage = 932);
    void searchForPointer(uint32_t target_address);
    void searchForByteArray(const std::vector<uint8_t>& pattern);
    void searchDomain(const std::string& domain_name);
    std::string formatValue(uint64_t value, SearchDataType data_type, bool hex_format = false) const;
    
    // Methods expected by ram_search.cpp but delegated to main search engine
    bool startSearch(const SearchConfig& config) { return startNewSearch(config); }
    // startFilteredSearch already declared above
    // getAvailableDomains, etc. are declared below in the public section
    
    // SearchProgress is defined at namespace level above
    
    // Progress callback
    std::function<void(const SearchProgress&)> progress_callback_;
    
    // Static member for codepage names
    static std::map<uint16_t, std::string> codepage_names_;
};

} // namespace LuaEngineRamSearch

#endif // RAM_SEARCH_ENGINE_H
