#ifndef TRACE_LOGGER_H
#define TRACE_LOGGER_H

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <chrono>
#include <functional>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <fstream>
#include "core_debug_interface.h"
#include "lua_memory_domains.h"

namespace LuaEngineSymbols { class SymbolManager; }

namespace LuaEngineTraceLogger {

// Forward declarations
class TraceLogger;
using MemoryDomainManager = LuaEngineMemoryDomains::MemoryDomainManager;

// Enhanced trace event types
enum class TraceEventType {
    INSTRUCTION,        // CPU instruction execution
    MEMORY_READ,        // Memory read operation
    MEMORY_WRITE,       // Memory write operation
    REGISTER_CHANGE,    // CPU register change
    INTERRUPT,          // Interrupt call
    FUNCTION_CALL,      // Function/procedure call
    FUNCTION_RETURN,    // Function/procedure return
    BRANCH_TAKEN,       // Conditional branch taken
    BRANCH_NOT_TAKEN,   // Conditional branch not taken
    EXCEPTION,          // CPU exception
    IO_READ,            // I/O port read
    IO_WRITE,           // I/O port write
    STRING_ACCESS,      // String/text access with SJIS support
    CUSTOM              // Custom user-defined event
};

// Memory access information with SJIS text decoding
struct MemoryAccess {
    uint32_t address;
    uint32_t value;
    size_t size;
    std::string domain;
    bool is_write;
    
    // Japanese text support
    std::string decoded_text_sjis;
    std::string decoded_text_utf8;
    uint16_t codepage;
    bool is_text_data;
    
    MemoryAccess() : address(0), value(0), size(0), is_write(false), 
                    codepage(932), is_text_data(false) {}
};

// OPTIMIZATION: Fixed register snapshot (cache-friendly, zero allocations)
struct FastRegisterSnapshot {
    // Fixed memory layout (cache friendly)
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint16_t cs, ds, es, fs, gs, ss;
    uint32_t eflags;

    // Validity bitmask for lazy loading (optimization)
    mutable uint32_t valid_mask;

    FastRegisterSnapshot()
        : eax(0), ebx(0), ecx(0), edx(0), esi(0), edi(0), ebp(0), esp(0),
          eip(0), cs(0), ds(0), es(0), fs(0), gs(0), ss(0), eflags(0),
          valid_mask(0xFFFFFFFF) {}  // All valid by default

    // Convert to legacy map format (for compatibility)
    std::map<std::string, uint32_t> toLegacyMap() const {
        std::map<std::string, uint32_t> result;
        result["EAX"] = eax; result["EBX"] = ebx; result["ECX"] = ecx; result["EDX"] = edx;
        result["ESI"] = esi; result["EDI"] = edi; result["ESP"] = esp; result["EBP"] = ebp;
        result["EIP"] = eip; result["EFLAGS"] = eflags;
        result["CS"] = cs; result["DS"] = ds; result["ES"] = es;
        result["FS"] = fs; result["GS"] = gs; result["SS"] = ss;
        return result;
    }
};

// Enhanced trace entry structure
struct TraceEntry {
    uint32_t address;
    uint16_t cs;
    uint16_t ip;
    std::string disassembly;

    // OPTIMIZATION: Use fixed struct instead of std::map (50-100x faster!)
    FastRegisterSnapshot fast_registers;

    // Legacy map for backward compatibility (populated on-demand)
    mutable std::map<std::string, uint32_t> registers;
    mutable bool registers_map_valid;

    std::chrono::steady_clock::time_point timestamp;
    uint32_t frame_number;
    uint32_t cycle_count;
    
    // Event type and additional data
    TraceEventType event_type;
    MemoryAccess memory_access;
    
    // Enhanced instruction information
    std::vector<uint8_t> instruction_bytes;
    std::string instruction_mnemonic;
    std::string instruction_operands;
    size_t instruction_length;
    
    // Japanese text support
    std::string string_data_sjis;
    std::string string_data_utf8;
    uint16_t text_codepage;
    
    // Performance tracking
    uint64_t execution_time_cycles;
    
    TraceEntry(uint32_t addr, uint16_t cs_val, uint16_t ip_val, const std::string& disasm)
        : address(addr), cs(cs_val), ip(ip_val), disassembly(disasm),
          registers_map_valid(false),
          timestamp(std::chrono::steady_clock::now()), frame_number(0), cycle_count(0),
          event_type(TraceEventType::INSTRUCTION), instruction_length(0),
          text_codepage(932), execution_time_cycles(0) {}

    // Cached formatted strings for UI performance
    mutable std::string cached_address_str;
    mutable std::string cached_timestamp_str;
    mutable std::string cached_register_str;
    mutable std::string cached_formatted_line;

    // Helper methods
    std::string getAddressString() const;
    std::string getTimestampString() const;
    std::string getFormattedLine() const;
    std::string getRegisterString() const;
    std::string getJapaneseTextString() const;
    std::string getEventTypeString() const;

    // OPTIMIZATION: Lazy population of legacy registers map
    const std::map<std::string, uint32_t>& getLegacyRegisters() const {
        if (!registers_map_valid) {
            registers = fast_registers.toLegacyMap();
            registers_map_valid = true;
        }
        return registers;
    }
};

// Enhanced trace filter options
struct TraceFilter {
    bool enabled = false;
    
    // Address filtering
    uint32_t start_address = 0;
    uint32_t end_address = 0xFFFFFFFF;
    std::vector<std::pair<uint32_t, uint32_t>> address_ranges;
    bool address_range_exclusive = false;
    
    // Event type filtering
    std::set<TraceEventType> enabled_event_types;
    
    // Instruction filtering
    std::string mnemonic_filter;
    std::set<std::string> instruction_patterns;
    bool filter_calls = false;
    bool filter_jumps = false;
    bool filter_rets = false;
    bool filter_interrupts = false;

    // Memory domain filtering
    std::set<std::string> memory_domains;
    bool memory_domain_exclusive = false;
    
    // Register filtering
    std::set<std::string> register_names;
    bool register_exclusive = false;
    
    // Japanese text filtering
    bool filter_sjis_strings = false;
    bool filter_ascii_strings = false;
    uint16_t codepage_filter = 932;
    
    // Performance filtering
    size_t max_events_per_second = 10000;
    bool enable_rate_limiting = false;

    // Range-driven control
    bool autostart_in_range = false; // Begin tracing automatically when entering range
    bool autostop_at_end = false;    // Stop tracing once execution passes end_address
    
    TraceFilter() {
        // Enable all event types by default
        enabled_event_types.insert(TraceEventType::INSTRUCTION);
        enabled_event_types.insert(TraceEventType::MEMORY_READ);
        enabled_event_types.insert(TraceEventType::MEMORY_WRITE);
        enabled_event_types.insert(TraceEventType::INTERRUPT);
        enabled_event_types.insert(TraceEventType::FUNCTION_CALL);
        enabled_event_types.insert(TraceEventType::FUNCTION_RETURN);
    }
    
    bool shouldInclude(const TraceEntry& entry) const;
};

// Enhanced trace statistics
struct TraceStatistics {
    uint32_t total_instructions = 0;
    uint32_t unique_addresses = 0;
    uint32_t call_count = 0;
    uint32_t jump_count = 0;
    uint32_t ret_count = 0;
    uint32_t interrupt_count = 0;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    // Enhanced statistics
    uint64_t total_events = 0;
    uint64_t events_by_type[static_cast<int>(TraceEventType::CUSTOM) + 1] = {0};
    uint64_t total_memory_reads = 0;
    uint64_t total_memory_writes = 0;
    uint64_t total_io_operations = 0;
    uint64_t total_execution_cycles = 0;
    
    // Most frequent instructions and addresses
    std::map<std::string, uint32_t> instruction_frequency;
    std::map<uint32_t, uint32_t> address_frequency;
    std::unordered_map<uint32_t, uint64_t> hotspots;  // address -> hit count
    
    // Japanese text statistics
    uint32_t sjis_string_accesses = 0;
    uint32_t ascii_string_accesses = 0;
    std::map<uint16_t, uint32_t> codepage_usage;
    
    // Performance metrics
    double events_per_second = 0.0;
    size_t buffer_overflow_count = 0;
    
    void reset();
    std::string getFormattedStats() const;
    std::chrono::milliseconds getDuration() const;
    void updateEventStats(const TraceEntry& entry);
};

// Main trace logger class
class TraceLogger {
private:
    // Core components
    MemoryDomainManager* memory_manager_;
    LuaEngineDebug::CoreDebugInterface* debug_interface_;
    LuaEngineSymbols::SymbolManager* symbol_manager_;
    
    // Event buffer (thread-safe)
    std::deque<TraceEntry> trace_log_;
    mutable std::mutex trace_mutex_;
    size_t max_entries_;
    std::atomic<bool> enabled_;
    std::atomic<bool> paused_;
    
    // Filtering and statistics
    TraceFilter filter_;
    TraceStatistics stats_;
    
    // Frame and cycle tracking
    uint32_t current_frame_;
    uint32_t current_cycle_;
    
    // Enhanced performance settings
    uint32_t trace_interval_;          // Trace every N instructions
    uint32_t trace_counter_;           // Current instruction counter
    bool include_registers_;           // Include register state (expensive)
    bool include_instruction_bytes_;   // Include raw instruction bytes
    bool include_memory_data_;         // Include memory access data
    bool decode_japanese_text_;        // Decode SJIS/Japanese text
    bool log_memory_access_;           // Log memory read/write events
    bool log_on_step_;                 // Log instructions executed via step into/over
    
    // Async file output
    std::unique_ptr<std::ofstream> output_file_;
    std::thread writer_thread_;
    std::atomic<bool> writer_should_stop_;
    std::deque<TraceEntry> write_queue_;
    std::mutex write_mutex_;

    // Rate limiting (mutable for thread-safe access in const methods)
    mutable std::chrono::steady_clock::time_point last_event_time_;
    mutable size_t events_this_second_;

    // Call stack tracking
    std::vector<uint32_t> call_stack_;
    mutable std::recursive_mutex call_stack_mutex_;
    bool track_call_stack_ = true;

    // Export settings
    std::string export_format_;        // Format for export (csv, txt, xml, json)

    // Range tracing state
    bool range_active_{false};
    
public:
    TraceLogger();
    ~TraceLogger();
    
    // Initialization
    bool initialize(MemoryDomainManager* memory_mgr,
                   LuaEngineDebug::CoreDebugInterface* debug_interface);
    bool initialize(LuaEngineDebug::CoreDebugInterface* debug_interface);
    bool initialize(LuaEngineDebug::CoreDebugInterface* debug_interface,
                  LuaEngineSymbols::SymbolManager* symbol_manager);
    void shutdown();
    
    // Core functionality
    void addEntry(uint32_t address, uint16_t cs, uint16_t ip, const std::string& disassembly);
    void addEntry(const TraceEntry& entry);
    void clear();
    void pause();
    void resume();
    void step();  // Trace one instruction
    
    // Enhanced event logging with SJIS support
    void logInstruction(uint32_t address, const std::string& disassembly, 
                       const std::vector<uint8_t>& bytes = {});
    void logMemoryRead(uint32_t address, uint32_t value, size_t size, 
                      const std::string& domain = "");
    void logMemoryWrite(uint32_t address, uint32_t value, size_t size, 
                       const std::string& domain = "");
    void logRegisterChange(const std::string& register_name, 
                          uint32_t old_value, uint32_t new_value);
    void logInterrupt(uint8_t interrupt_number);
    void logFunctionCall(uint32_t address, const std::string& function_name = "");
    void logFunctionReturn(uint32_t return_address);
    void logBranch(uint32_t from_address, uint32_t to_address, bool taken);
    void logIOAccess(uint16_t port, uint32_t value, size_t size, bool is_write);
    void logStringAccess(uint32_t address, const std::string& text, uint16_t codepage = 932);
    void logCustomEvent(const std::string& description);
    
    // Enhanced settings
    void setEnabled(bool enabled) { enabled_ = enabled; if (!enabled) range_active_ = false; }
    void setMaxEntries(size_t max_entries);
    void setTraceInterval(uint32_t interval) { trace_interval_ = interval; }
    void setIncludeRegisters(bool include) { include_registers_ = include; }
    void setIncludeInstructionBytes(bool include) { include_instruction_bytes_ = include; }
    void setIncludeMemoryData(bool include) { include_memory_data_ = include; }
    void setDecodeJapaneseText(bool decode) { decode_japanese_text_ = decode; }
    void setLogMemoryAccess(bool enabled) { log_memory_access_ = enabled; }
    void setLogOnStep(bool enabled) { log_on_step_ = enabled; }

    bool getLogMemoryAccess() const { return log_memory_access_; }
    bool getLogOnStep() const { return log_on_step_; }

    // Call stack tracking
    void enableCallStackTracking(bool enable) { track_call_stack_ = enable; }
    bool isCallStackTrackingEnabled() const { return track_call_stack_; }
    std::vector<uint32_t> getCallStack() const;
    void clearCallStack();

    // Filtering
    void setFilter(const TraceFilter& filter) { filter_ = filter; }
    TraceFilter& getFilter() { return filter_; }
    const TraceFilter& getFilter() const { return filter_; }
    
    // Thread-safe accessors
    bool isEnabled() const { return enabled_.load(); }
    bool isPaused() const { return paused_.load(); }
    size_t getEntryCount() const;
    size_t getMaxEntries() const { return max_entries_; }
    
    std::vector<TraceEntry> getEntries() const;
    const TraceEntry* getEntry(size_t index) const;
    std::vector<TraceEntry> getRecentEntries(size_t count) const;
    
    // Enhanced statistics
    TraceStatistics getStatistics() const;
    void resetStatistics();
    void updateStatistics(const TraceEntry& entry);
    
    // Advanced search functionality with SJIS support
    std::vector<size_t> findEntriesByAddress(uint32_t address) const;
    std::vector<size_t> findEntriesByMnemonic(const std::string& mnemonic) const;
    std::vector<size_t> findEntriesByRegister(const std::string& register_name, uint32_t value) const;
    std::vector<size_t> findEntriesByEventType(TraceEventType type) const;
    std::vector<size_t> findEntriesByMemoryAccess(uint32_t address, bool write_only = false) const;
    std::vector<size_t> findEntriesByJapaneseText(const std::string& text, uint16_t codepage = 932) const;
    std::vector<size_t> findEntriesInTimeRange(
        const std::chrono::steady_clock::time_point& start,
        const std::chrono::steady_clock::time_point& end) const;
    
    // Enhanced export functionality
    bool exportToFile(const std::string& filename, const std::string& format = "txt") const;
    bool exportToCSV(const std::string& filename) const;
    bool exportToXML(const std::string& filename) const;
    void exportToJSON(const std::string& filename) const;
    void exportToBinary(const std::string& filename) const;
    std::string exportToString() const;
    
    // Import functionality
    bool importFromFile(const std::string& filename);
    bool importFromBinary(const std::string& filename);
    
    // Advanced analysis functions
    std::vector<uint32_t> getUniqueAddresses() const;
    std::map<uint32_t, uint32_t> getAddressFrequency() const;
    std::map<std::string, uint32_t> getInstructionFrequency() const;
    std::map<uint32_t, uint64_t> getHotspots(size_t top_count = 10) const;
    std::map<std::string, uint32_t> getJapaneseTextFrequency() const;
    std::map<uint16_t, uint32_t> getCodepageUsage() const;
    
    // Performance analysis
    double getExecutionCoverage(uint32_t start_address, uint32_t end_address) const;
    std::vector<uint32_t> findUnexecutedCode(uint32_t start_address, uint32_t end_address) const;
    std::vector<std::vector<TraceEntry>> findRepeatingPatterns(size_t min_length = 3) const;
    
    // Frame and cycle management
    void onFrameStart();
    void onFrameEnd();
    void setCycleCount(uint32_t cycles) { current_cycle_ = cycles; }
    
    // File output control
    bool startFileOutput(const std::string& filename, const std::string& format = "txt");
    void stopFileOutput();
    bool isFileOutputActive() const;
    
    // Integration callbacks
    std::function<void(const TraceEntry&)> onEntryAdded;
    std::function<void()> onTraceCleared;
    std::function<void(bool)> onTracingStateChanged;
    std::function<void(const std::string&)> onError;
    
private:
    // Internal helper methods
    void trimToMaxEntries();
    bool handleRangeAutoControl(uint32_t address);
    bool passesFilter(const TraceEntry& entry) const;
    bool passesRateLimit() const;
    void addEntryInternal(const TraceEntry& entry);
    
    // Formatting methods
    std::string formatEntryAsCSV(const TraceEntry& entry) const;
    std::string formatEntryAsXML(const TraceEntry& entry) const;
    std::string formatEntryAsJSON(const TraceEntry& entry) const;
    std::string formatEntryAsText(const TraceEntry& entry) const;
    
    // Analysis helpers
    void analyzeInstruction(const std::string& disassembly, TraceEntry& entry);
    void analyzeMemoryAccess(TraceEntry& entry);
    void updateCallStack(const TraceEntry& entry);
    
    // Japanese text processing
    std::string decodeJapaneseText(uint32_t address, size_t max_length = 256, uint16_t codepage = 932) const;
    bool isValidSJISSequence(const std::vector<uint8_t>& data) const;
    std::string convertSJISToUTF8(const std::vector<uint8_t>& sjis_data) const;
    
    // Instruction analysis helpers
    bool isCallInstruction(const std::string& mnemonic) const;
    bool isJumpInstruction(const std::string& mnemonic) const;
    bool isRetInstruction(const std::string& mnemonic) const;
    bool isInterruptInstruction(const std::string& mnemonic) const;
    bool isBranchInstruction(const std::string& mnemonic) const;
    bool isStringInstruction(const std::string& mnemonic) const;
    
    // File output helpers
    void writerThreadFunc();
    void writeEntryToFile(const TraceEntry& entry);
    void flushWriteQueue();
    
    // Statistics helpers
    void updateHotspots(const TraceEntry& entry);
    void updateInstructionFrequency(const TraceEntry& entry);
    void updateAddressFrequency(const TraceEntry& entry);
    
    // Error handling
    void handleError(const std::string& error_message);
};

// Utility functions for trace analysis with SJIS support
namespace TraceUtils {
    std::string eventTypeToString(TraceEventType type);
    TraceEventType stringToEventType(const std::string& str);
    std::string formatTimestamp(const std::chrono::steady_clock::time_point& time);
    std::string formatAddress(uint32_t address);
    std::string formatValue(uint32_t value, size_t size);
    
    // Japanese text utilities
    bool isValidSJIS(const std::vector<uint8_t>& data);
    std::string sjisToUTF8(const std::vector<uint8_t>& sjis_data);
    std::vector<uint8_t> utf8ToSJIS(const std::string& utf8_text);
    
    // Statistical analysis
    double calculateExecutionCoverage(const std::vector<TraceEntry>& entries, 
                                    uint32_t start_address, uint32_t end_address);
    std::vector<uint32_t> findUnexecutedCode(const std::vector<TraceEntry>& entries,
                                           uint32_t start_address, uint32_t end_address);
    std::map<std::string, double> calculateInstructionDistribution(const std::vector<TraceEntry>& entries);
    
    // Pattern recognition
    std::vector<std::vector<TraceEntry>> findRepeatingPatterns(const std::vector<TraceEntry>& entries,
                                                              size_t min_pattern_length = 3);
    std::vector<TraceEntry> findAnomalies(const std::vector<TraceEntry>& entries);
};

} // namespace LuaEngineTraceLogger

#endif // TRACE_LOGGER_H
