#ifndef WATCH_LIST_H
#define WATCH_LIST_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <chrono>
#include "lua_memory_domains.h"
#include "ram_search_engine.h"

namespace LuaEngineWatchList {

// Forward declaration
class WatchList;

// Use WatchSize from ram_search_engine.h
using WatchSize = LuaEngineRamSearch::WatchSize;

// Watch display type enum
enum class WatchDisplayType {
    HEXADECIMAL,
    DECIMAL,
    BINARY,
    SIGNED_DECIMAL,
    FLOAT,
    DOUBLE,
    STRING_SJIS,      // Japanese Shift JIS
    STRING_UTF8,      // UTF-8
    STRING_ASCII      // ASCII
};

// Individual watch entry
class Watch {
private:
    uint32_t address_;
    std::string domain_;
    WatchSize size_;
    WatchDisplayType display_type_;
    std::string notes_;
    
    // Values
    uint64_t current_value_;
    uint64_t previous_value_;
    uint64_t initial_value_;
    
    // Freeze state
    bool frozen_;
    uint64_t frozen_value_;
    
    // Change tracking
    uint32_t change_count_;
    bool value_changed_;
    
    // Reference to memory manager
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    
    // Text encoding support
    uint16_t codepage_;
    size_t string_length_;
    
public:
    Watch(uint32_t address, const std::string& domain, WatchSize size);
    ~Watch();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Value management
    void updateValue();
    void updateValueFromCache(uint64_t cached_value);  // For batch processing
    void poke(uint64_t value);
    void setInitialValue(uint64_t value);
    
    // Freeze management
    void freeze();
    void freeze(uint64_t value);
    void unfreeze();
    void applyFrozenValue();
    
    // Properties
    void setNotes(const std::string& notes) { notes_ = notes; }
    void setDisplayType(WatchDisplayType type) { display_type_ = type; }
    void setSize(WatchSize size) { size_ = size; }
    void setDomain(const std::string& domain) { domain_ = domain; }
    void setCodepage(uint16_t codepage) { codepage_ = codepage; }
    void setStringLength(size_t length) { string_length_ = length; }
    
    // Accessors
    uint32_t getAddress() const { return address_; }
    const std::string& getDomain() const { return domain_; }
    WatchSize getSize() const { return size_; }
    WatchDisplayType getDisplayType() const { return display_type_; }
    const std::string& getNotes() const { return notes_; }
    uint16_t getCodepage() const { return codepage_; }
    size_t getStringLength() const { return string_length_; }
    
    uint64_t getCurrentValue() const { return current_value_; }
    uint64_t getPreviousValue() const { return previous_value_; }
    uint64_t getInitialValue() const { return initial_value_; }
    uint64_t getFrozenValue() const { return frozen_value_; }
    
    bool isFrozen() const { return frozen_; }
    bool hasValueChanged() const { return value_changed_; }
    uint32_t getChangeCount() const { return change_count_; }
    
    // Display methods
    std::string getAddressString() const;
    std::string getCurrentValueString() const;
    std::string getPreviousValueString() const;
    std::string getInitialValueString() const;
    std::string getFrozenValueString() const;
    std::string formatValue(uint64_t value) const;
    std::string readStringValue() const;
    
    // Serialization
    std::string serialize() const;
    bool deserialize(const std::string& data);
};

// Watch list container
class WatchList {
private:
    std::vector<std::unique_ptr<Watch>> watches_;
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    
    // Batch processing optimization
    struct BatchReadRequest {
        uint32_t start_address;
        uint32_t end_address;
        std::string domain;
        std::vector<Watch*> watches; // Watches in this range
    };
    
    // Cached batch read data
    std::map<std::string, std::vector<BatchReadRequest>> batch_requests_;
    std::unordered_map<uint32_t, uint64_t> address_cache_;  // Address -> Value cache
    bool cache_dirty_;
    
    // Update management
    std::mutex update_mutex_;
    std::chrono::steady_clock::time_point last_update_;
    int update_interval_ms_;
    
    // File format version
    static const int FILE_FORMAT_VERSION = 1;
    
public:
    WatchList();
    ~WatchList();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Watch management
    void addWatch(std::unique_ptr<Watch> watch);
    size_t addWatch(uint32_t address, const std::string& domain, WatchSize size);
    void removeWatch(size_t index);
    void removeWatch(uint32_t address, const std::string& domain);
    void clearWatches();
    
    // Batch operations
    void updateAllValues();
    void updateAllValuesBatched();  // High-performance batch update
    void applyAllFrozenValues();
    void unfreezeAll();
    
    // Performance optimization
    void rebuildBatchRequests();
    void clearCache();
    
    // File operations
    bool saveToFile(const std::string& filename) const;
    bool loadFromFile(const std::string& filename);
    bool exportToFile(const std::string& filename) const;
    
    // Search and access
    Watch* findWatch(uint32_t address, const std::string& domain);
    const Watch* findWatch(uint32_t address, const std::string& domain) const;
    
    // Accessors
    const std::vector<std::unique_ptr<Watch>>& getWatches() const { return watches_; }
    size_t getWatchCount() const { return watches_.size(); }
    
    Watch* getWatch(size_t index) { 
        return (index < watches_.size()) ? watches_[index].get() : nullptr; 
    }
    
    const Watch* getWatch(size_t index) const { 
        return (index < watches_.size()) ? watches_[index].get() : nullptr; 
    }
    
    // Statistics
    size_t getFrozenCount() const;
    size_t getChangedCount() const;
    
    // Import from search results
    void importFromSearchResults(const std::vector<LuaEngineRamSearch::SearchResult>& results, 
                                const std::string& domain);
    
    // Text encoding utilities
    std::string getSizeString(WatchSize size) const;
    std::string getDisplayTypeString(WatchDisplayType type) const;
    WatchSize stringToSize(const std::string& str) const;
    WatchDisplayType stringToDisplayType(const std::string& str) const;
    
    // Additional methods called by DebugToolsManager
    void freezeWatch(size_t watch_id, uint64_t value);
    void setMaxWatches(int max_watches);
    void setUpdateInterval(int interval);
    void onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value);
    uint64_t getCachedValue(uint32_t address) const;
    uint64_t getCachedValue(uint32_t address, WatchSize size) const;
};

} // namespace LuaEngineWatchList

#endif // WATCH_LIST_H