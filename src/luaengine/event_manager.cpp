#include "event_manager.h"
#include "luaengine.h"
#include "debug.h"
#include "logging.h" // For DEBUG_ShowMsg
#include "trace_logger.h"

namespace LuaEngineEvents {

// ========== Static String Constants Definition ==========
// Pre-computed string literals to avoid repeated string creation and comparisons

const char* EventTableKeys::TYPE = "type";
const char* EventTableKeys::SEGMENT = "segment";
const char* EventTableKeys::OFFSET = "offset";
const char* EventTableKeys::ADDRESS = "address";
const char* EventTableKeys::VALUE = "value";
const char* EventTableKeys::SIZE = "size";
const char* EventTableKeys::READ = "read";
const char* EventTableKeys::WRITE = "write";
const char* EventTableKeys::INTERRUPT_NUMBER = "number";
const char* EventTableKeys::AX = "ax";
const char* EventTableKeys::BX = "bx";
const char* EventTableKeys::CX = "cx";
const char* EventTableKeys::DX = "dx";
const char* EventTableKeys::CS = "cs";
const char* EventTableKeys::DS = "ds";
const char* EventTableKeys::ES = "es";
const char* EventTableKeys::SS = "ss";

// Performance and batching constants
namespace {
    constexpr uint32_t DEFAULT_MEMORY_THROTTLE_THRESHOLD = 1000;  // Throttle after N accesses per frame
    constexpr uint32_t DEFAULT_BATCH_FLUSH_INTERVAL_MS = 16;      // Flush every 16ms (~60fps)
    constexpr size_t DEFAULT_MAX_BATCH_SIZE = 100;               // Max events per batch
    constexpr size_t DEFAULT_FILTER_BUCKET_RESERVE = 4;          // Most buckets will have few filters

    // Single thread-local batch shared by fireMemoryEvent() and flushThreadLocalBatch()
    // PR1-002: unified variable — was duplicated in both functions (defect 3a)
    static thread_local std::vector<MemoryEventData> t_local_batch;
}

EventManager::EventManager()
    : next_callback_id(1), lua_state(nullptr), trace_logger_(nullptr), performance_mode(false),
      frame_skip_counter(0), frame_skip_interval(0), memory_access_counter(0),
      memory_throttle_threshold(DEFAULT_MEMORY_THROTTLE_THRESHOLD),
      event_contexts_initialized(false),  // PHASE 3: Initialize reusable context flag
      enable_deferred_execution(false) {  // PHASE 3 STEP 3: Deferred callbacks disabled by default
    last_callback_time = std::chrono::steady_clock::now();
    
    // Initialize memory batching with performance-friendly defaults
    memory_batch.flush_interval_ms = DEFAULT_BATCH_FLUSH_INTERVAL_MS;
    memory_batch.max_batch_size = DEFAULT_MAX_BATCH_SIZE;
    memory_batch.last_flush = std::chrono::steady_clock::now();
    memory_batch.events.reserve(memory_batch.max_batch_size);  // Pre-allocate for known capacity
    
    // Initialize spatial indexing system
    memory_filter_index.resize(FILTER_INDEX_SIZE);
    for (auto& bucket : memory_filter_index) {
        bucket.reserve(DEFAULT_FILTER_BUCKET_RESERVE);
    }
}

void EventManager::initialize(sol::state* lua, LuaEngineTraceLogger::TraceLogger* trace_logger) {
    lua_state = lua;
    trace_logger_ = trace_logger;

    // Create cached event table for reuse (legacy)
    if (lua_state) {
        cached_event_table = lua_state->create_table();

        // PHASE 3 OPTIMIZATION: Create reusable context tables for each event type
        // This eliminates table allocation overhead on every event fire
        if (!event_contexts_initialized) {
            for (size_t i = 0; i < cached_event_contexts.size(); i++) {
                cached_event_contexts[i] = lua_state->create_table();
            }
            event_contexts_initialized = true;
            DEBUG_ShowMsg("LuaEngine: EventManager created %zu reusable context tables",
                         cached_event_contexts.size());
        }
    }

    DEBUG_ShowMsg("LuaEngine: EventManager initialized with%s TraceLogger",
                  trace_logger_ ? "" : "out");
}

uint32_t EventManager::registerCallback(EventType type, sol::protected_function callback, const std::string& name) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    auto lua_callback = std::make_shared<LuaCallback>(callback, name, next_callback_id);
    callbacks[toIndex(type)].push_back(lua_callback);
    
    DEBUG_ShowMsg("LuaEngine: Registered callback %s (ID: %u) for event type %d", 
                  name.empty() ? "unnamed" : name.c_str(), next_callback_id, static_cast<int>(type));
    
    return next_callback_id++;
}

bool EventManager::unregisterCallback(uint32_t callback_id) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    for (auto& callback_list : callbacks) {
        auto it = std::remove_if(callback_list.begin(), callback_list.end(),
                                [callback_id](const std::shared_ptr<LuaCallback>& cb) {
                                    return cb->id == callback_id;
                                });
        
        if (it != callback_list.end()) {
            DEBUG_ShowMsg("LuaEngine: Unregistered callback ID: %u", callback_id);
            callback_list.erase(it, callback_list.end());
            return true;
        }
    }
    
    // Check memory filters
    auto memory_it = std::remove_if(memory_filters.begin(), memory_filters.end(),
                                   [callback_id](const MemoryFilter& filter) {
                                       return filter.callback->id == callback_id;
                                   });
    
    if (memory_it != memory_filters.end()) {
        // Remove from spatial index before removing from vector
        for (auto it_to_remove = memory_it; it_to_remove != memory_filters.end(); ++it_to_remove) {
            removeFilterFromIndex(it_to_remove->id);
        }
        
        memory_filters.erase(memory_it, memory_filters.end());
        DEBUG_ShowMsg("LuaEngine: Unregistered memory callback ID: %u", callback_id);
        return true;
    }
    
    return false;
}

bool EventManager::enableCallback(uint32_t callback_id, bool enabled) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    for (auto& callback_list : callbacks) {
        for (auto& cb : callback_list) {
            if (cb->id == callback_id) {
                cb->enabled = enabled;
                DEBUG_ShowMsg("LuaEngine: %s callback ID: %u", 
                             enabled ? "Enabled" : "Disabled", callback_id);
                return true;
            }
        }
    }
    
    return false;
}

uint32_t EventManager::registerMemoryCallback(uint32_t start_addr, uint32_t end_addr, 
                                             sol::protected_function callback, bool on_read, bool on_write) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    auto lua_callback = std::make_shared<LuaCallback>(callback, "memory_callback", next_callback_id);
    
    MemoryFilter filter;
    filter.start_addr = start_addr;
    filter.end_addr = end_addr;
    filter.read_enabled = on_read;
    filter.write_enabled = on_write;
    filter.callback = lua_callback;
    filter.id = next_callback_id;  // Assign unique ID for spatial indexing
    
    memory_filters.push_back(filter);
    
    // Add to spatial index for fast lookup
    addFilterToIndex(filter);
    
    DEBUG_ShowMsg("LuaEngine: Registered memory callback (ID: %u) for range 0x%08X-0x%08X", 
                  next_callback_id, start_addr, end_addr);
    
    return next_callback_id++;
}

void EventManager::fireEvent(EventType type, const EventContext& context) {
    // Fast path: early exit if no callbacks or frame should be skipped
    if (shouldSkipFrame(type)) {
        return;
    }

    // Thread-safe double-checked locking pattern
    size_t type_index = toIndex(type);

    // First check with shared lock (read-only access)
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (callbacks[type_index].empty()) {
            return; // No callbacks for this event type
        }
    }

    // Proceed with callback execution under lock
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    // Performance mode: minimal overhead
    if (performance_mode && lua_state) {
        // Reuse cached event table instead of creating new one
        cached_event_table["type"] = static_cast<int>(type);
        
        // Only add essential context data in performance mode
        if (!context.data.empty()) {
            for (const auto& [key, value] : context.data) {
                cached_event_table[key] = value;
            }
        }
        
        // Fast callback execution without exception handling
        for (const auto& callback : callbacks[type_index]) {
            if (callback->enabled) {
                callback->function(cached_event_table);
            }
        }
        
        // Clear table for next use
        cached_event_table.clear();
    } else {
        // Standard mode: full error handling and new table creation
        for (const auto& callback : callbacks[type_index]) {
            if (!callback->enabled) continue;
            
            try {
                if (lua_state) {
                    auto event_table = lua_state->create_table();
                    event_table["type"] = static_cast<int>(type);
                    
                    // Add context data to the table
                    for (const auto& [key, value] : context.data) {
                        event_table[key] = value;
                    }
                    
                    // Call the Lua function with the event table
                    callback->function(event_table);
                }
            } catch (const std::exception& e) {
                DEBUG_ShowMsg("LuaEngine: Error in event callback %s: %s", 
                             callback->name.c_str(), e.what());
            }
        }
    }
}

void EventManager::fireFrameEvent(EventType type) {
    // Ultra-fast path: early exit if no callbacks or frame should be skipped
    if (shouldSkipFrame(type)) {
        return;
    }

    size_t type_index = toIndex(type);

    // Snapshot enabled callbacks under lock, then invoke without lock
    // PR1-012 + PR1-014: copy shared_ptrs under lock, release, invoke (defect 14)
    std::vector<std::shared_ptr<LuaCallback>> active_callbacks;
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (callbacks[type_index].empty()) {
            return;
        }
        for (const auto& cb : callbacks[type_index]) {
            if (cb->enabled) {
                active_callbacks.push_back(cb);
            }
        }
    }
    // callback_mutex is now released — re-entrant fireFrameEvent will not deadlock

    // Performance mode: minimal overhead using cached table
    if (performance_mode && lua_state) {
        cached_event_table["type"] = static_cast<int>(type);

        for (const auto& callback : active_callbacks) {
            // PR1-015: try/catch in performance mode (defect 15)
            try {
                callback->function(cached_event_table);
            } catch (const std::exception& e) {
                DEBUG_ShowMsg("LuaEngine: Error in perf frame event callback %s: %s",
                             callback->name.c_str(), e.what());
            }
        }

        cached_event_table.clear();
    } else {
        // Standard mode: full error handling but still avoid EventContext allocation
        for (const auto& callback : active_callbacks) {
            try {
                if (lua_state) {
                    auto event_table = lua_state->create_table();
                    event_table["type"] = static_cast<int>(type);

                    callback->function(event_table);
                }
            } catch (const std::exception& e) {
                DEBUG_ShowMsg("LuaEngine: Error in frame event callback %s: %s",
                             callback->name.c_str(), e.what());
            }
        }
    }
}

void EventManager::fireMemoryEvent(const MemoryEventData& data) {
    // Log to trace logger if enabled
    if (trace_logger_ && trace_logger_->getLogMemoryAccess()) {
        if (data.is_write) {
            trace_logger_->logMemoryWrite(data.physical_addr, data.data, data.access_size, "SYSTEM");
        } else {
            trace_logger_->logMemoryRead(data.physical_addr, data.data, data.access_size, "SYSTEM");
        }
    }

    // Fast path: early exit if no memory filters
    if (memory_filters.empty()) {
        return;
    }

    // Performance optimization: throttle high-frequency memory accesses
    // Exact-feature events (watchpoints, CDL) bypass throttle unconditionally
    if (!(g_instrumentation_features.load(std::memory_order_relaxed) & INSTR_EXACT_MEMORY)) {
        if (shouldThrottleMemoryAccess(data.physical_addr)) {
            return;
        }
    }

    // Use spatial indexing to get potentially matching filters (O(1) instead of O(n))
    auto filter_ids = getMatchingFilterIds(data.physical_addr);
    if (filter_ids.empty()) {
        return;  // No filters in this address range
    }

    // In performance mode, use thread-local batching to eliminate lock contention
    if (performance_mode) {
        // Quick validation: check if ANY filter would actually match before batching
        bool has_matching_filter = false;
        for (uint32_t filter_id : filter_ids) {
            const EventManager::MemoryFilter* filter = getFilterById(filter_id);
            if (filter && filter->callback->enabled &&
                data.physical_addr >= filter->start_addr &&
                data.physical_addr <= filter->end_addr &&
                ((data.is_write && filter->write_enabled) || (!data.is_write && filter->read_enabled))) {
                has_matching_filter = true;
                break;
            }
        }

        if (!has_matching_filter) {
            return;
        }

        // OPTIMIZATION: Thread-local batching (lock-free fast path)
        // Accumulate events in thread-local storage, only lock when full

        // Reserve capacity on first use
        if (t_local_batch.capacity() == 0) {
            t_local_batch.reserve(THREAD_LOCAL_BATCH_LIMIT);
        }

        // Push to local buffer (zero contention, no locks!)
        t_local_batch.push_back(data);

        // Flush only when the local batch is full
        if (t_local_batch.size() >= THREAD_LOCAL_BATCH_LIMIT) {
            // Now we need to lock and flush to main batch
            std::unique_lock<std::mutex> batch_lock(memory_batch.batch_mutex);

            // Bulk move elements to main queue
            memory_batch.events.insert(
                memory_batch.events.end(),
                t_local_batch.begin(),
                t_local_batch.end()
            );
            t_local_batch.clear();

            // Check if we need to flush the main batch
            auto now = std::chrono::steady_clock::now();
            bool should_flush = (memory_batch.events.size() >= memory_batch.max_batch_size) ||
                               (std::chrono::duration_cast<std::chrono::milliseconds>(now - memory_batch.last_flush).count()
                                >= memory_batch.flush_interval_ms);

            if (should_flush) {
                // Release lock before flushing (flushMemoryEventBatch will reacquire callback_mutex)
                // PR1-001: unique_lock::unlock() instead of explicit destructor to avoid double-unlock UB
                batch_lock.unlock();
                flushMemoryEventBatch();
            }
        }
    } else {
        // Standard mode: immediate processing with full error handling using spatial indexing
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        for (uint32_t filter_id : filter_ids) {
            const EventManager::MemoryFilter* filter = getFilterById(filter_id);
            if (!filter || !filter->callback->enabled) continue;
            
            // Address range check (more precise than spatial index)
            if (data.physical_addr < filter->start_addr || data.physical_addr > filter->end_addr) {
                continue;
            }
            
            // Event type check
            if ((data.is_write && !filter->write_enabled) || (!data.is_write && !filter->read_enabled)) {
                continue;
            }
            
            try {
                if (lua_state) {
                    // PHASE 3 OPTIMIZATION: Reuse pre-allocated context table instead of creating new one
                    // This eliminates allocation overhead and GC pressure (30-50% reduction!)
                    sol::table& event_table = cached_event_contexts[toIndex(
                        data.is_write ? EventType::MEMORY_WRITE : EventType::MEMORY_READ
                    )];

                    // Update the table fields (zero allocations!)
                    event_table[EventTableKeys::TYPE] = data.is_write ? EventTableKeys::WRITE : EventTableKeys::READ;
                    event_table[EventTableKeys::SEGMENT] = data.segment;
                    event_table[EventTableKeys::OFFSET] = data.offset;
                    event_table[EventTableKeys::ADDRESS] = data.physical_addr;
                    event_table[EventTableKeys::VALUE] = data.data;
                    event_table[EventTableKeys::SIZE] = static_cast<size_t>(data.access_size);

                    // PHASE 3 STEP 3: Defer callback execution if enabled
                    if (enable_deferred_execution) {
                        // Queue for batch execution at frame boundary
                        // PR1-007: store native struct instead of sol::table reference (defect 6)
                        std::lock_guard<std::mutex> deferred_lock(deferred_mutex);
                        deferred_callbacks.push_back({
                            filter->callback->function,
                            data.segment,
                            data.offset,
                            data.physical_addr,
                            data.data,
                            data.access_size,
                            data.is_write
                        });
                    } else {
                        // Immediate execution (legacy path)
                        filter->callback->function(event_table);
                    }
                }
            } catch (const std::exception& e) {
                DEBUG_ShowMsg("LuaEngine: Error in memory callback: %s", e.what());
            }
        }
    }
}

void EventManager::fireInterruptEvent(const InterruptEventData& data) {
    // OPTIMIZATION: Use performance mode with cached table to eliminate allocations
    if (performance_mode && lua_state) {
        // Reuse cached_event_table instead of creating new table
        cached_event_table["type"] = static_cast<int>(EventType::DOS_INTERRUPT);
        cached_event_table[EventTableKeys::INTERRUPT_NUMBER] = data.interrupt_num;
        cached_event_table[EventTableKeys::AX] = data.ax;
        cached_event_table[EventTableKeys::BX] = data.bx;
        cached_event_table[EventTableKeys::CX] = data.cx;
        cached_event_table[EventTableKeys::DX] = data.dx;
        cached_event_table[EventTableKeys::CS] = data.cs;
        cached_event_table[EventTableKeys::DS] = data.ds;
        cached_event_table[EventTableKeys::ES] = data.es;
        cached_event_table[EventTableKeys::SS] = data.ss;

        // Fast callback execution without exception handling
        std::lock_guard<std::mutex> lock(callback_mutex);
        size_t type_index = toIndex(EventType::DOS_INTERRUPT);
        for (const auto& callback : callbacks[type_index]) {
            if (callback->enabled) {
                callback->function(cached_event_table);
            }
        }

        // Clear table for next use
        cached_event_table.clear();
    } else {
        // Standard mode: create new table with full error handling
        EventContext context(EventType::DOS_INTERRUPT);

        if (lua_state) {
            context.data["interrupt"] = lua_state->create_table_with(
                EventTableKeys::INTERRUPT_NUMBER, data.interrupt_num,
                EventTableKeys::AX, data.ax,
                EventTableKeys::BX, data.bx,
                EventTableKeys::CX, data.cx,
                EventTableKeys::DX, data.dx,
                EventTableKeys::CS, data.cs,
                EventTableKeys::DS, data.ds,
                EventTableKeys::ES, data.es,
                EventTableKeys::SS, data.ss
            );
        }

        fireEvent(EventType::DOS_INTERRUPT, context);
    }
}

std::vector<std::shared_ptr<LuaCallback>> EventManager::getCallbacks(EventType type) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    
    size_t type_index = toIndex(type);
    return callbacks[type_index];
}

void EventManager::clearAllCallbacks() {
    std::lock_guard<std::mutex> lock(callback_mutex);
    for (auto& callback_list : callbacks) {
        callback_list.clear();
    }
    memory_filters.clear();
    
    // Clear spatial index
    for (auto& bucket : memory_filter_index) {
        bucket.clear();
    }
    
    DEBUG_ShowMsg("LuaEngine: Cleared all event callbacks and spatial index");
}

void EventManager::clearCallbacks(EventType type) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    size_t type_index = toIndex(type);
    callbacks[type_index].clear();
    DEBUG_ShowMsg("LuaEngine: Cleared callbacks for event type %d", static_cast<int>(type));
}

size_t EventManager::getCallbackCount(EventType type) const {
    size_t type_index = toIndex(type);
    return callbacks[type_index].size();
}

size_t EventManager::getTotalCallbackCount() const {
    size_t total = 0;
    for (const auto& callback_list : callbacks) {
        total += callback_list.size();
    }
    total += memory_filters.size();
    return total;
}

// Performance optimization methods
void EventManager::setPerformanceMode(bool enabled) {
    performance_mode = enabled;
    if (enabled) {
        DEBUG_ShowMsg("LuaEngine: Performance mode enabled - callbacks will run with minimal overhead");
    } else {
        DEBUG_ShowMsg("LuaEngine: Performance mode disabled - full error handling restored");
    }
}

void EventManager::setFrameSkipInterval(uint32_t interval) {
    frame_skip_interval = interval;
    frame_skip_counter = 0;
    DEBUG_ShowMsg("LuaEngine: Frame skip interval set to %u", interval);
}

bool EventManager::shouldSkipFrame(EventType type) {
    // Never skip critical events
    if (type == EventType::SAVESTATE_SAVE || type == EventType::SAVESTATE_LOAD || 
        type == EventType::BREAKPOINT_HIT) {
        return false;
    }
    

    
    return false;
}

void EventManager::resetCachedEventTable() {
    if (lua_state) {
        cached_event_table = lua_state->create_table();
        DEBUG_ShowMsg("LuaEngine: Cached event table reset");
    }
}

// Memory performance optimization methods
void EventManager::setMemoryBatchSize(uint32_t max_size) {
    memory_batch.max_batch_size = max_size;
    DEBUG_ShowMsg("LuaEngine: Memory batch size set to %u", max_size);
}

void EventManager::setMemoryFlushInterval(uint32_t ms) {
    memory_batch.flush_interval_ms = ms;
    DEBUG_ShowMsg("LuaEngine: Memory flush interval set to %u ms", ms);
}

void EventManager::setMemoryThrottleThreshold(uint32_t threshold) {
    memory_throttle_threshold = threshold;
    DEBUG_ShowMsg("LuaEngine: Memory throttle threshold set to %u", threshold);
}

void EventManager::flushThreadLocalBatch() {
    // FIX: Uses file-scope t_local_batch (PR1-002 — duplicate removed, defect 3a)
    if (t_local_batch.empty()) {
        return;
    }

    // Lock only to move data from thread-local to main batch
    std::lock_guard<std::mutex> batch_lock(memory_batch.batch_mutex);

    // Bulk move elements to main queue
    memory_batch.events.insert(
        memory_batch.events.end(),
        t_local_batch.begin(),
        t_local_batch.end()
    );
    t_local_batch.clear();
}

void EventManager::flushMemoryEventBatch() {
    // Thread-safe batch processing with proper locking order
    std::vector<MemoryEventData> events_to_process;

    // First, safely copy events under batch mutex
    {
        std::lock_guard<std::mutex> batch_lock(memory_batch.batch_mutex);
        if (memory_batch.events.empty() || !lua_state) {
            return;
        }
        events_to_process = memory_batch.events;
        memory_batch.events.clear();
        memory_batch.last_flush = std::chrono::steady_clock::now();
    }

    // Then process events under callback mutex
    std::lock_guard<std::mutex> callback_lock(callback_mutex);

    // Process batched events efficiently using spatial indexing
    for (const auto& data : events_to_process) {
        // Use spatial indexing to get potentially matching filters
        auto filter_ids = getMatchingFilterIds(data.physical_addr);
        
        for (uint32_t filter_id : filter_ids) {
            const EventManager::MemoryFilter* filter = getFilterById(filter_id);
            if (!filter || !filter->callback->enabled) continue;
            
            // Address range and type checks (more precise than spatial index)
            if (data.physical_addr < filter->start_addr || data.physical_addr > filter->end_addr) {
                continue;
            }
            
            if ((data.is_write && !filter->write_enabled) || (!data.is_write && !filter->read_enabled)) {
                continue;
            }
            
            // Use cached table for maximum performance
            cached_event_table[EventTableKeys::TYPE] = data.is_write ? EventTableKeys::WRITE : EventTableKeys::READ;
            cached_event_table[EventTableKeys::SEGMENT] = data.segment;
            cached_event_table[EventTableKeys::OFFSET] = data.offset;
            cached_event_table[EventTableKeys::ADDRESS] = data.physical_addr;
            cached_event_table[EventTableKeys::VALUE] = data.data;
            cached_event_table[EventTableKeys::SIZE] = static_cast<size_t>(data.access_size);
            
            // Call without exception handling for maximum speed
            filter->callback->function(cached_event_table);
            cached_event_table.clear();
        }
    }

    // Batch was already reset in the batch lock section above
}

bool EventManager::shouldThrottleMemoryAccess(uint32_t address) {
    memory_access_counter++;
    
    // Reset counter periodically (frame-based throttling)
    if (memory_access_counter > memory_throttle_threshold * 2) {
        memory_access_counter = 0;
        hot_memory_pages.clear();  // Reset hot page tracking
    }
    
    // Check if we've exceeded the throttle threshold
    if (memory_access_counter > memory_throttle_threshold) {
        // Track hot memory pages (4KB page granularity)
        uint32_t page = address >> 12;  // 4KB pages
        
        if (hot_memory_pages.find(page) != hot_memory_pages.end()) {
            // This page is already hot, throttle it
            return true;
        } else if (hot_memory_pages.size() < 64) {  // Limit hot page tracking
            hot_memory_pages.insert(page);
        }
        
        // Throttle every Nth access when over threshold
        return (memory_access_counter % 10) != 0;  // Only process every 10th access
    }
    
    return false;
}

// ========== Spatial Indexing Implementation ==========

void EventManager::rebuildMemoryFilterIndex() {
    // Clear existing index
    for (auto& bucket : memory_filter_index) {
        bucket.clear();
    }
    
    // Rebuild index from current filters
    for (const auto& filter : memory_filters) {
        addFilterToIndex(filter);
    }
    
    DEBUG_ShowMsg("LuaEngine: Rebuilt memory filter spatial index with %zu filters", memory_filters.size());
}

void EventManager::addFilterToIndex(const MemoryFilter& filter) {
    // Calculate index range based on filter address range
    uint32_t start_index = filter.start_addr / FILTER_INDEX_GRANULARITY;
    uint32_t end_index = filter.end_addr / FILTER_INDEX_GRANULARITY;
    
    // Clamp to valid index range
    start_index = std::min(start_index, FILTER_INDEX_SIZE - 1);
    end_index = std::min(end_index, FILTER_INDEX_SIZE - 1);
    
    // Add filter ID to all relevant index buckets
    for (uint32_t i = start_index; i <= end_index; ++i) {
        memory_filter_index[i].push_back(filter.id);
    }
}

void EventManager::removeFilterFromIndex(uint32_t filter_id) {
    // Remove filter ID from all index buckets
    for (auto& bucket : memory_filter_index) {
        bucket.erase(std::remove(bucket.begin(), bucket.end(), filter_id), bucket.end());
    }
}

std::vector<uint32_t> EventManager::getMatchingFilterIds(uint32_t address) const {
    uint32_t index = address / FILTER_INDEX_GRANULARITY;
    
    if (index >= FILTER_INDEX_SIZE) {
        return {};  // Address out of indexed range
    }
    
    return memory_filter_index[index];
}

// Helper function to find filter by ID
const EventManager::MemoryFilter* EventManager::getFilterById(uint32_t filter_id) const {
    for (const auto& filter : memory_filters) {
        if (filter.id == filter_id) {
            return &filter;
        }
    }
    return nullptr;
}

// PHASE 3 STEP 3: Deferred callback batching implementation
void EventManager::setDeferredCallbackMode(bool enabled) {
    enable_deferred_execution = enabled;
    DEBUG_ShowMsg("LuaEngine: Deferred callback execution %s", enabled ? "enabled" : "disabled");
}

void EventManager::executeDeferredCallbacks() {
    // Step 1: Extract all callbacks under lock
    std::vector<DeferredMemoryEvent> callbacks_to_execute;
    {
        std::lock_guard<std::mutex> lock(deferred_mutex);
        if (deferred_callbacks.empty()) {
            return;  // Nothing to do
        }
        callbacks_to_execute.swap(deferred_callbacks);
    }

    // Step 2: Execute all callbacks without holding the deferred lock
    // This allows new callbacks to be queued while we're executing
    // PR1-007: build fresh sol::table from native struct data (defect 6)
    for (auto& deferred : callbacks_to_execute) {
        try {
            if (lua_state) {
                auto ctx = lua_state->create_table();
                ctx[EventTableKeys::TYPE]   = deferred.is_write ? EventTableKeys::WRITE : EventTableKeys::READ;
                ctx[EventTableKeys::SEGMENT] = deferred.segment;
                ctx[EventTableKeys::OFFSET]  = deferred.offset;
                ctx[EventTableKeys::ADDRESS] = deferred.physical_addr;
                ctx[EventTableKeys::VALUE]   = deferred.data;
                ctx[EventTableKeys::SIZE]    = static_cast<size_t>(deferred.access_size);
                deferred.func(ctx);
            }
        } catch (const std::exception& e) {
            DEBUG_ShowMsg("LuaEngine: Error in deferred callback: %s", e.what());
        }
    }
}

size_t EventManager::getDeferredCallbackCount() const {
    std::lock_guard<std::mutex> lock(deferred_mutex);
    return deferred_callbacks.size();
}

} // namespace LuaEngineEvents
