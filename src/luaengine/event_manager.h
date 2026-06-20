#pragma once

#include <functional>
#include <vector>
#include <map>
#include <array>
#include <unordered_set>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include "../../vs/lua/sol/sol.hpp"

// Forward declarations
// (CpuSnapshot removed - now defined as FastCpuSnapshot in LuaEngine.cpp)
namespace LuaEngineTraceLogger { class TraceLogger; }

namespace LuaEngineEvents {

enum class EventType {
    FRAME_START,
    FRAME_END,
    MEMORY_READ,
    MEMORY_WRITE,
    DOS_INTERRUPT,
    KEYBOARD_INPUT,
    MOUSE_INPUT,
    TIMER_TICK,
    CPU_STEP,
    BREAKPOINT_HIT,
    SAVESTATE_SAVE,
    SAVESTATE_LOAD,
    EVENT_TYPE_COUNT  // Must be last - used for array sizing
};

// Helper function for O(1) enum-to-index conversion
constexpr size_t toIndex(EventType type) {
    return static_cast<size_t>(type);
}

// Enum-based constants for memory event types (replaces string literals)
enum class MemoryEventType : uint8_t {
    READ = 0,
    WRITE = 1
};

// Enum-based constants for event table keys (replaces string lookups)
enum class EventTableKey : uint8_t {
    TYPE = 0,
    SEGMENT = 1,
    OFFSET = 2,
    ADDRESS = 3,
    VALUE = 4,
    SIZE = 5,
    // Interrupt-specific keys
    INTERRUPT_NUMBER = 6,
    AX = 7,
    BX = 8, 
    CX = 9,
    DX = 10,
    CS = 11,
    DS = 12,
    ES = 13,
    SS = 14
};

// Pre-computed string constants for Lua table keys (computed once, used many times)
struct EventTableKeys {
    static const char* TYPE;
    static const char* SEGMENT;
    static const char* OFFSET;
    static const char* ADDRESS;
    static const char* VALUE;
    static const char* SIZE;
    static const char* READ;
    static const char* WRITE;
    static const char* INTERRUPT_NUMBER;
    static const char* AX;
    static const char* BX;
    static const char* CX;
    static const char* DX;
    static const char* CS;
    static const char* DS;
    static const char* ES;
    static const char* SS;
};

struct EventContext {
    EventType type;
    std::map<std::string, sol::object> data;
    
    EventContext(EventType t) : type(t) {}
    
    template<typename T>
    void setData(const std::string& key, T value) {
        // This will be set by the Lua state when needed
    }
};

struct MemoryEventData {
    uint16_t segment;
    uint32_t offset;
    uint32_t physical_addr;
    uint64_t data;          // was: uint8_t value — widened per PR1-003
    uint8_t  access_size;   // was: size_t size — narrowed to uint8_t per PR1-003
    bool is_write;
};

struct InterruptEventData {
    uint8_t interrupt_num;
    uint16_t ax, bx, cx, dx;
    uint16_t cs, ds, es, ss;
};

struct LuaCallback {
    sol::protected_function function;
    std::string name;
    uint32_t id;
    bool enabled;
    
    LuaCallback(sol::protected_function func, const std::string& n, uint32_t i) 
        : function(func), name(n), id(i), enabled(true) {}
};

class EventManager {
private:
    // Event callback storage - O(1) array access instead of O(log n) map lookup
    std::array<std::vector<std::shared_ptr<LuaCallback>>, static_cast<size_t>(EventType::EVENT_TYPE_COUNT)> callbacks;
    std::mutex callback_mutex;
    uint32_t next_callback_id;
    sol::state* lua_state;
    LuaEngineTraceLogger::TraceLogger* trace_logger_;
    
    // Performance optimization members
    sol::table cached_event_table;  // Reusable event table
    bool performance_mode;          // Skip expensive operations in fast mode
    uint32_t frame_skip_counter;    // For frame skipping
    uint32_t frame_skip_interval;   // Skip N frames between callbacks
    std::chrono::steady_clock::time_point last_callback_time;
    
    // Memory event batching for performance
    struct MemoryEventBatch {
        std::vector<MemoryEventData> events;
        std::chrono::steady_clock::time_point last_flush;
        uint32_t flush_interval_ms;     // Flush batch every N milliseconds
        uint32_t max_batch_size;        // Maximum events before forced flush
        std::mutex batch_mutex;         // Protects concurrent access to batch
    } memory_batch;

    // Thread-local batching constants (lock-free fast path)
    static constexpr size_t THREAD_LOCAL_BATCH_LIMIT = 512;
    
    // Memory access filtering
    std::unordered_set<uint32_t> hot_memory_pages;  // Frequently accessed pages to throttle
    uint32_t memory_access_counter;
    uint32_t memory_throttle_threshold;  // Throttle after N accesses per frame
    
    // Memory callback filters
    struct MemoryFilter {
        uint32_t start_addr;
        uint32_t end_addr;
        bool read_enabled;
        bool write_enabled;
        std::shared_ptr<LuaCallback> callback;
        uint32_t id;  // Unique filter ID for indexing
    };
    std::vector<MemoryFilter> memory_filters;
    
    // Spatial indexing for fast memory filter lookup
    static constexpr uint32_t FILTER_INDEX_GRANULARITY = 0x1000;  // 4KB granularity
    static constexpr uint32_t FILTER_INDEX_SIZE = 0x100000 / FILTER_INDEX_GRANULARITY;  // 1MB address space / 4KB = 256 entries
    std::vector<std::vector<uint32_t>> memory_filter_index;  // Index maps address ranges to filter IDs

    // PHASE 3 OPTIMIZATION: Reusable Lua context tables to eliminate GC pressure
    // One pre-allocated table per event type, reused for every callback invocation
    std::array<sol::table, static_cast<size_t>(EventType::EVENT_TYPE_COUNT)> cached_event_contexts;
    bool event_contexts_initialized;

    // PHASE 3 STEP 3 OPTIMIZATION: Deferred callback batching
    // Queue callbacks to execute at frame boundaries instead of immediately
    struct DeferredMemoryEvent {
        sol::protected_function func;
        uint16_t segment;
        uint32_t offset;
        uint32_t physical_addr;
        uint64_t data;
        uint8_t  access_size;
        bool     is_write;
    };
    std::vector<DeferredMemoryEvent> deferred_callbacks;
    mutable std::mutex deferred_mutex;  // Mutable to allow locking in const methods
    bool enable_deferred_execution;

public:
    EventManager();
    ~EventManager() = default;

    // Initialize with Lua state
    void initialize(sol::state* lua, LuaEngineTraceLogger::TraceLogger* trace_logger = nullptr);
    
    // Register callbacks
    uint32_t registerCallback(EventType type, sol::protected_function callback, const std::string& name = "");
    bool unregisterCallback(uint32_t callback_id);
    bool enableCallback(uint32_t callback_id, bool enabled);
    
    // Memory-specific callback registration
    uint32_t registerMemoryCallback(uint32_t start_addr, uint32_t end_addr, 
                                   sol::protected_function callback, bool on_read = true, bool on_write = true);
    
    // Fire events
    void fireEvent(EventType type, const EventContext& context);
    void fireFrameEvent(EventType type);  // Zero-allocation frame event for performance
    void fireMemoryEvent(const MemoryEventData& data);
    void fireInterruptEvent(const InterruptEventData& data);
    
    // Utility functions
    std::vector<std::shared_ptr<LuaCallback>> getCallbacks(EventType type);
    void clearAllCallbacks();
    void clearCallbacks(EventType type);
    
    // Performance optimization controls
    void setPerformanceMode(bool enabled);
    void setFrameSkipInterval(uint32_t interval);  // 0 = no skipping, 1 = every frame, 2 = every 2nd frame, etc.
    bool shouldSkipFrame(EventType type);          // Check if this frame should be skipped
    void resetCachedEventTable();                  // Force recreation of cached table
    
    // Memory event performance controls
    void setMemoryBatchSize(uint32_t max_size);    // Set max events before forced flush
    void setMemoryFlushInterval(uint32_t ms);      // Set batch flush interval in milliseconds
    void setMemoryThrottleThreshold(uint32_t threshold);  // Set memory access throttle limit
    void flushMemoryEventBatch();                  // Force flush current batch
    void flushThreadLocalBatch();                  // Flush thread-local batch (lock-free optimization)
    bool shouldThrottleMemoryAccess(uint32_t address);  // Check if memory access should be throttled

    // PHASE 3 STEP 3: Deferred callback batching controls
    void setDeferredCallbackMode(bool enabled);    // Enable/disable deferred callback execution
    void executeDeferredCallbacks();               // Execute all queued callbacks (call at frame boundaries)
    size_t getDeferredCallbackCount() const;       // Get number of queued callbacks
    
    // Spatial indexing for memory filters
    void rebuildMemoryFilterIndex();               // Rebuild spatial index after filter changes
    void addFilterToIndex(const MemoryFilter& filter);  // Add filter to spatial index
    void removeFilterFromIndex(uint32_t filter_id);     // Remove filter from spatial index
    std::vector<uint32_t> getMatchingFilterIds(uint32_t address) const;  // Get filter IDs for address
    const EventManager::MemoryFilter* getFilterById(uint32_t filter_id) const;  // Find filter by ID
    
    // Statistics
    size_t getCallbackCount(EventType type) const;
    size_t getTotalCallbackCount() const;
};

} // namespace LuaEngineEvents
