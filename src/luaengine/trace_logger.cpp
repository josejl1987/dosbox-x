#include "trace_logger.h"
#include "debug_utils.h"
#include "logging.h"
#include "symbol_manager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <cstring>
#include <cstdio>

    using LuaEngineDebugUtils::formatAddress;

    namespace LuaEngineTraceLogger {

        //=============================================================================
        // TraceEntry Implementation
        //=============================================================================

        std::string TraceEntry::getAddressString() const {
            // FIX: Use stored CS:IP directly to match Disassembly/CPU state exactly.
            // Previous logic recalculated from linear address, causing mismatched segment/offset pairs.
            return LuaEngineDebugUtils::formatHexWord(cs) + ":" + LuaEngineDebugUtils::formatHexWord(ip);
        }

        std::string TraceEntry::getTimestampString() const {
            auto duration = timestamp.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            return std::to_string(millis);
        }

        std::string TraceEntry::getFormattedLine() const {
            std::stringstream ss;
            ss << getAddressString() << " " << disassembly;

            if(!registers.empty()) {
                ss << " ; " << getRegisterString();
            }

            return ss.str();
        }

        std::string TraceEntry::getRegisterString() const {
            // OPTIMIZATION: Use fast_registers and avoid stringstream
            // Pre-allocate buffer for maximum efficiency
            char buffer[256];
            char* ptr = buffer;

            // Format key registers directly (much faster than stringstream)
            ptr += sprintf(ptr, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ",
                fast_registers.eax, fast_registers.ebx, fast_registers.ecx, fast_registers.edx);
            ptr += sprintf(ptr, "ESI=%08X EDI=%08X ESP=%08X EBP=%08X",
                fast_registers.esi, fast_registers.edi, fast_registers.esp, fast_registers.ebp);

            return std::string(buffer, ptr - buffer);
        }

        std::string TraceEntry::getJapaneseTextString() const {
            if(!string_data_sjis.empty()) {
                return "SJIS: \"" + string_data_sjis + "\"";
            }
            else if(!string_data_utf8.empty()) {
                return "UTF8: \"" + string_data_utf8 + "\"";
            }
            return "";
        }

        std::string TraceEntry::getEventTypeString() const {
            switch(event_type) {
            case TraceEventType::INSTRUCTION: return "INSTRUCTION";
            case TraceEventType::MEMORY_READ: return "MEMORY_READ";
            case TraceEventType::MEMORY_WRITE: return "MEMORY_WRITE";
            case TraceEventType::REGISTER_CHANGE: return "REGISTER_CHANGE";
            case TraceEventType::INTERRUPT: return "INTERRUPT";
            case TraceEventType::FUNCTION_CALL: return "FUNCTION_CALL";
            case TraceEventType::FUNCTION_RETURN: return "FUNCTION_RETURN";
            case TraceEventType::BRANCH_TAKEN: return "BRANCH_TAKEN";
            case TraceEventType::BRANCH_NOT_TAKEN: return "BRANCH_NOT_TAKEN";
            case TraceEventType::EXCEPTION: return "EXCEPTION";
            case TraceEventType::IO_READ: return "IO_READ";
            case TraceEventType::IO_WRITE: return "IO_WRITE";
            case TraceEventType::STRING_ACCESS: return "STRING_ACCESS";
            case TraceEventType::CUSTOM: return "CUSTOM";
            default: return "UNKNOWN";
            }
        }

        //=============================================================================
        // TraceFilter Implementation
        //=============================================================================

        bool TraceFilter::shouldInclude(const TraceEntry& entry) const {
            if(!enabled) return true;

            // Address range filter
            // FIX: If using triggers (auto-start/stop), DO NOT filter by address range here.
            // The enabled/disabled state of the logger handles the range inclusion.
            if(!autostart_in_range && !autostop_at_end) {
                if(entry.address < start_address || entry.address > end_address) {
                    return false;
                }
            }

            // Mnemonic filter
            if(!mnemonic_filter.empty()) {
                if(entry.instruction_mnemonic.find(mnemonic_filter) == std::string::npos) {
                    return false;
                }
            }

            // Instruction type filters
            if(filter_calls && entry.instruction_mnemonic.find("CALL") != std::string::npos) {
                return false;
            }

            if(filter_jumps && (entry.instruction_mnemonic.find("JMP") != std::string::npos ||
                entry.instruction_mnemonic.find("J") == 0)) {
                return false;
            }

            if(filter_rets && entry.instruction_mnemonic.find("RET") != std::string::npos) {
                return false;
            }

            if(filter_interrupts && entry.instruction_mnemonic.find("INT") != std::string::npos) {
                return false;
            }

            return true;
        }

        //=============================================================================
        // TraceStatistics Implementation
        //=============================================================================

        void TraceStatistics::reset() {
            total_instructions = 0;
            unique_addresses = 0;
            call_count = 0;
            jump_count = 0;
            ret_count = 0;
            interrupt_count = 0;
            instruction_frequency.clear();
            address_frequency.clear();
            start_time = std::chrono::steady_clock::now();
        }

        std::string TraceStatistics::getFormattedStats() const {
            auto duration = end_time - start_time;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

            std::stringstream ss;
            ss << "=== Trace Statistics ===\n";
            ss << "Total Instructions: " << total_instructions << "\n";
            ss << "Unique Addresses: " << unique_addresses << "\n";
            ss << "Duration: " << seconds << " seconds\n";
            ss << "Instructions/Second: " << (seconds > 0 ? total_instructions / seconds : 0) << "\n";
            ss << "Calls: " << call_count << "\n";
            ss << "Jumps: " << jump_count << "\n";
            ss << "Returns: " << ret_count << "\n";
            ss << "Interrupts: " << interrupt_count << "\n";

            ss << "\nTop 10 Instructions:\n";

            // Sort instruction frequency
            std::vector<std::pair<std::string, uint32_t>> sorted_instructions(
                instruction_frequency.begin(), instruction_frequency.end());
            std::sort(sorted_instructions.begin(), sorted_instructions.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });

            for(size_t i = 0; i < std::min(size_t(10), sorted_instructions.size()); ++i) {
                ss << "  " << sorted_instructions[i].first << ": " << sorted_instructions[i].second << "\n";
            }

            return ss.str();
        }

        //=============================================================================
        // TraceLogger Implementation
        //=============================================================================

        TraceLogger::TraceLogger()
            : memory_manager_(nullptr), debug_interface_(nullptr), symbol_manager_(nullptr), max_entries_(100000),
            enabled_(false), paused_(false), current_frame_(0), current_cycle_(0),
            trace_interval_(1), trace_counter_(0), include_registers_(true),
            include_instruction_bytes_(false), include_memory_data_(true),
            decode_japanese_text_(true), log_memory_access_(false), log_on_step_(true),
            writer_should_stop_(false), events_this_second_(0), export_format_("txt"),
            ring_buf_(std::make_unique<TraceRingBuffer>(100000)),
            decode_cache_(std::make_unique<DecodeCache>(200)),
            snapshot_store_(std::make_unique<SnapshotStore>()),
            current_snapshot_index_(0) {

            stats_.reset();
            last_event_time_ = std::chrono::steady_clock::now();
            trace_start_time_ = std::chrono::steady_clock::now();
        }

        TraceLogger::~TraceLogger() {
            shutdown();
        }

        bool TraceLogger::initialize(MemoryDomainManager* memory_mgr,
            LuaEngineDebug::CoreDebugInterface* debug_interface) {
            if(!memory_mgr || !debug_interface) return false;

            memory_manager_ = memory_mgr;
            debug_interface_ = debug_interface;

            stats_.start_time = std::chrono::steady_clock::now();

            return true;
        }

        bool TraceLogger::initialize(LuaEngineDebug::CoreDebugInterface* debug_interface) {
            if(!debug_interface) return false;

            debug_interface_ = debug_interface;
            memory_manager_ = nullptr; // Will need to be set separately if needed
            symbol_manager_ = nullptr; // Will need to be set separately if needed

            stats_.start_time = std::chrono::steady_clock::now();

            return true;
        }

        bool TraceLogger::initialize(LuaEngineDebug::CoreDebugInterface* debug_interface,
                                  LuaEngineSymbols::SymbolManager* symbol_manager) {
            if(!debug_interface) return false;

            debug_interface_ = debug_interface;
            memory_manager_ = nullptr; // Will need to be set separately if needed
            symbol_manager_ = symbol_manager ? symbol_manager : LuaEngineSymbols::g_symbol_manager;

            stats_.start_time = std::chrono::steady_clock::now();

            return true;
        }

        void TraceLogger::shutdown() {
            stopFileOutput();

            std::lock_guard<std::mutex> lock(trace_mutex_);
            trace_log_.clear();
            stats_.reset();

            // PR4: Clear ring buffer and caches
            if(ring_buf_) ring_buf_->clear();
            if(decode_cache_) decode_cache_->clear();
            if(snapshot_store_) snapshot_store_->clear();
        }

        void TraceLogger::addEntry(uint32_t address, uint16_t cs, uint16_t ip, const std::string& disassembly) {
            try {
                if(!handleRangeAutoControl(address)) {
                    return;
                }

                if(!enabled_ || paused_) return;

                // Check trace interval
                if(trace_interval_ > 1) {
                    trace_counter_++;
                    if(trace_counter_ % trace_interval_ != 0) {
                        return;
                    }
                }

                TraceEntry entry(address, cs, ip, disassembly);
                entry.frame_number = current_frame_;
                entry.cycle_count = current_cycle_;

                // Add register state if enabled
                // OPTIMIZATION: Use fast register snapshot (zero allocations!)
                if(include_registers_ && debug_interface_) {
                    debug_interface_->getCpuRegistersFast(entry.fast_registers);
                    entry.registers_map_valid = false;  // Map will be populated on-demand if needed
                }

                // Add instruction bytes if enabled
                if(include_instruction_bytes_ && debug_interface_) {
                    // Get actual instruction bytes from memory at the current address
                    auto instruction_data = debug_interface_->getInstructionBytesVector(address);
                    if(!instruction_data.empty()) {
                        entry.instruction_bytes = instruction_data;
                        entry.instruction_length = instruction_data.size();
                    }
                    else {
                        // Fallback - read from memory directly if debug interface fails
                        std::vector<uint8_t> bytes;
                        for(size_t i = 0; i < 15 && i < 8; ++i) { // Max x86 instruction is 15 bytes, but limit to 8 for safety
                            if(memory_manager_) {
                                uint8_t byte = memory_manager_->readByteAt(address + i);
                                bytes.push_back(byte);
                                // Simple instruction length detection (very basic)
                                if(i > 0 && (byte == 0xC3 || byte == 0xCB || byte == 0xCC)) break; // RET, RETF, INT3
                            }
                            else {
                                break;
                            }
                        }
                        entry.instruction_bytes = bytes;
                        entry.instruction_length = bytes.size();
                    }
                }

                // Analyze instruction
                analyzeInstruction(disassembly, entry);

                // Update internal call stack tracking automatically (fail-safe)
                try {
                    updateCallStack(entry);
                }
                catch(const std::system_error& e) {
                    // TraceLogger call stack tracking disabled due to mutex error: %s", e.what());
                    track_call_stack_ = false;
                }

                // Check filter
                if(!passesFilter(entry)) {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(trace_mutex_);
                    trace_log_.push_back(entry);

                    // Update statistics (already thread-safe)
                    updateStatistics(entry);

                    // Trim if necessary
                    trimToMaxEntries();
                }

                // Add to write queue if file output is active
                if(output_file_) {
                    std::lock_guard<std::mutex> write_lock(write_mutex_);
                    write_queue_.push_back(entry);
                }

                // Fire callback outside lock to avoid deadlocks
                if(onEntryAdded) {
                    onEntryAdded(entry);
                }
            }
            catch(const std::system_error& e) {
                LOG(LOG_MISC, LOG_ERROR)("TraceLogger encountered system_error and will disable tracing: %s", e.what());
                enabled_ = false;
            }
            catch(const std::exception& e) {
                LOG(LOG_MISC, LOG_ERROR)("TraceLogger encountered exception and will disable tracing: %s", e.what());
                enabled_ = false;
            }
        }

        void TraceLogger::addEntry(const TraceEntry& entry) {
            try {
                if(!handleRangeAutoControl(entry.address)) {
                    return;
                }

                if(!enabled_ || paused_) return;

                if(!passesFilter(entry)) {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(trace_mutex_);
                    trace_log_.push_back(entry);
                    updateStatistics(entry);
                    trimToMaxEntries();
                }

                // Add to write queue if file output is active
                if(output_file_) {
                    std::lock_guard<std::mutex> write_lock(write_mutex_);
                    write_queue_.push_back(entry);
                }

                if(onEntryAdded) {
                    onEntryAdded(entry);
                }
            }
            catch(const std::system_error& e) {
                LOG(LOG_MISC, LOG_ERROR)("TraceLogger encountered system_error and will disable tracing: %s", e.what());
                enabled_ = false;
            }
            catch(const std::exception& e) {
                LOG(LOG_MISC, LOG_ERROR)("TraceLogger encountered exception and will disable tracing: %s", e.what());
                enabled_ = false;
            }
        }

        void TraceLogger::clear() {
            {
                std::lock_guard<std::mutex> lock(trace_mutex_);
                trace_log_.clear();
                stats_.reset();
            }
            {
                std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);
                call_stack_.clear();  // Clear call stack when clearing trace log
            }

            // PR4: Clear ring buffer and decode cache
            if(ring_buf_) ring_buf_->clear();
            if(decode_cache_) decode_cache_->clear();
            if(snapshot_store_) snapshot_store_->clear();
            trace_start_time_ = std::chrono::steady_clock::now();

            if(onTraceCleared) {
                onTraceCleared();
            }
        }

        void TraceLogger::pause() {
            paused_ = true;

            if(onTracingStateChanged) {
                onTracingStateChanged(false);
            }
        }

        void TraceLogger::resume() {
            paused_ = false;

            if(onTracingStateChanged) {
                onTracingStateChanged(true);
            }
        }

        void TraceLogger::step() {
            if(!enabled_) return;

            bool was_paused = paused_;
            paused_ = false;

            // The next instruction will be traced
            // Then we'll pause again

            if(was_paused && onTracingStateChanged) {
                onTracingStateChanged(true);
            }
        }

        //=============================================================================
        // Enhanced Event Logging with SJIS Support
        //=============================================================================

        void TraceLogger::logInstruction(uint32_t address, const std::string& disassembly,
            const std::vector<uint8_t>& bytes) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(address, 0, 0, disassembly);
            entry.event_type = TraceEventType::INSTRUCTION;
            entry.instruction_bytes = bytes;
            entry.instruction_length = bytes.size();

            analyzeInstruction(disassembly, entry);
            addEntryInternal(entry);
        }

        void TraceLogger::logMemoryRead(uint32_t address, uint32_t value, size_t size,
            const std::string& domain) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(address, 0, 0, "MEMORY_READ");
            entry.event_type = TraceEventType::MEMORY_READ;
            entry.memory_access.address = address;
            entry.memory_access.value = value;
            entry.memory_access.size = size;
            entry.memory_access.domain = domain;
            entry.memory_access.is_write = false;

            // Decode Japanese text if enabled
            if(decode_japanese_text_ && size > 1) {
                entry.string_data_sjis = decodeJapaneseText(address, size, 932);
                if(!entry.string_data_sjis.empty()) {
                    entry.memory_access.is_text_data = true;
                    entry.memory_access.decoded_text_sjis = entry.string_data_sjis;
                }
            }

            addEntryInternal(entry);
        }

        void TraceLogger::logMemoryWrite(uint32_t address, uint32_t value, size_t size,
            const std::string& domain) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(address, 0, 0, "MEMORY_WRITE");
            entry.event_type = TraceEventType::MEMORY_WRITE;
            entry.memory_access.address = address;
            entry.memory_access.value = value;
            entry.memory_access.size = size;
            entry.memory_access.domain = domain;
            entry.memory_access.is_write = true;

            // Decode Japanese text if enabled
            if(decode_japanese_text_ && size > 1) {
                entry.string_data_sjis = decodeJapaneseText(address, size, 932);
                if(!entry.string_data_sjis.empty()) {
                    entry.memory_access.is_text_data = true;
                    entry.memory_access.decoded_text_sjis = entry.string_data_sjis;
                }
            }

            addEntryInternal(entry);
        }

        void TraceLogger::logRegisterChange(const std::string& register_name,
            uint32_t old_value, uint32_t new_value) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(0, 0, 0, "REG_CHANGE: " + register_name);
            entry.event_type = TraceEventType::REGISTER_CHANGE;
            entry.registers[register_name + "_OLD"] = old_value;
            entry.registers[register_name + "_NEW"] = new_value;

            addEntryInternal(entry);
        }

        void TraceLogger::logInterrupt(uint8_t interrupt_number) {
            if(!enabled_.load() || paused_.load()) return;

            std::stringstream ss;
            ss << "INT " << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(interrupt_number);

            TraceEntry entry(0, 0, 0, ss.str());
            entry.event_type = TraceEventType::INTERRUPT;
            entry.registers["INT_NUM"] = interrupt_number;

            addEntryInternal(entry);
        }

        void TraceLogger::logFunctionCall(uint32_t address, const std::string& function_name) {
            if(!enabled_.load() || paused_.load()) return;

            std::string disasm = "CALL " + (function_name.empty() ?
                std::to_string(address) : function_name);

            TraceEntry entry(address, 0, 0, disasm);
            entry.event_type = TraceEventType::FUNCTION_CALL;

            // Update call stack
            if(track_call_stack_) {
                std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);
                call_stack_.push_back(address);
            }

            addEntryInternal(entry);
        }

        void TraceLogger::logFunctionReturn(uint32_t return_address) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(return_address, 0, 0, "RET");
            entry.event_type = TraceEventType::FUNCTION_RETURN;

            // Update call stack
            if(track_call_stack_) {
                std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);
                if(!call_stack_.empty()) {
                    call_stack_.pop_back();
                }
            }

            addEntryInternal(entry);
        }

        void TraceLogger::logBranch(uint32_t from_address, uint32_t to_address, bool taken) {
            if(!enabled_.load() || paused_.load()) return;

            std::stringstream ss;
            ss << "BRANCH " << std::hex << from_address << " -> " << to_address;

            TraceEntry entry(from_address, 0, 0, ss.str());
            entry.event_type = taken ? TraceEventType::BRANCH_TAKEN : TraceEventType::BRANCH_NOT_TAKEN;
            entry.registers["BRANCH_TARGET"] = to_address;

            addEntryInternal(entry);
        }

        void TraceLogger::logIOAccess(uint16_t port, uint32_t value, size_t size, bool is_write) {
            if(!enabled_.load() || paused_.load()) return;

            std::stringstream ss;
            ss << (is_write ? "OUT " : "IN ") << std::hex << port << ", " << value;

            TraceEntry entry(0, 0, 0, ss.str());
            entry.event_type = is_write ? TraceEventType::IO_WRITE : TraceEventType::IO_READ;
            entry.registers["IO_PORT"] = port;
            entry.registers["IO_VALUE"] = value;
            entry.registers["IO_SIZE"] = size;

            addEntryInternal(entry);
        }

        void TraceLogger::logStringAccess(uint32_t address, const std::string& text, uint16_t codepage) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(address, 0, 0, "STRING_ACCESS");
            entry.event_type = TraceEventType::STRING_ACCESS;
            entry.text_codepage = codepage;

            if(codepage == 932) {
                entry.string_data_sjis = text;
            }
            else {
                entry.string_data_utf8 = text;
            }

            addEntryInternal(entry);
        }

        void TraceLogger::logCustomEvent(const std::string& description) {
            if(!enabled_.load() || paused_.load()) return;

            TraceEntry entry(0, 0, 0, description);
            entry.event_type = TraceEventType::CUSTOM;

            addEntryInternal(entry);
        }

        void TraceLogger::setMaxEntries(size_t max_entries) {
            max_entries_ = max_entries;
            trimToMaxEntries();
            // PR4: Resize ring buffer to match
            if(ring_buf_) {
                ring_buf_ = std::make_unique<TraceRingBuffer>(max_entries);
            }
        }

        const TraceEntry* TraceLogger::getEntry(size_t index) const {
            if(index < trace_log_.size()) {
                return &trace_log_[index];
            }
            return nullptr;
        }

        void TraceLogger::updateStatistics(const TraceEntry& entry) {
            stats_.total_instructions++;
            stats_.end_time = entry.timestamp;

            // Update address frequency
            stats_.address_frequency[entry.address]++;

            // Update instruction frequency
            if(!entry.instruction_mnemonic.empty()) {
                stats_.instruction_frequency[entry.instruction_mnemonic]++;
            }

            // Count instruction types
            if(isCallInstruction(entry.instruction_mnemonic)) {
                stats_.call_count++;
            }
            else if(isJumpInstruction(entry.instruction_mnemonic)) {
                stats_.jump_count++;
            }
            else if(isRetInstruction(entry.instruction_mnemonic)) {
                stats_.ret_count++;
            }
            else if(isInterruptInstruction(entry.instruction_mnemonic)) {
                stats_.interrupt_count++;
            }

            // Update unique addresses count
            stats_.unique_addresses = stats_.address_frequency.size();
        }

        TraceStatistics TraceLogger::getStatistics() const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            return stats_;
        }

        void TraceLogger::resetStatistics() {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            stats_.reset();
        }

        //=============================================================================
        // Search Functions
        //=============================================================================

        std::vector<size_t> TraceLogger::findEntriesByAddress(uint32_t address) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            std::vector<size_t> results;

            for(size_t i = 0; i < trace_log_.size(); ++i) {
                if(trace_log_[i].address == address) {
                    results.push_back(i);
                }
            }

            return results;
        }

        std::vector<size_t> TraceLogger::findEntriesByMnemonic(const std::string& mnemonic) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            std::vector<size_t> results;

            for(size_t i = 0; i < trace_log_.size(); ++i) {
                if(trace_log_[i].instruction_mnemonic == mnemonic) {
                    results.push_back(i);
                }
            }

            return results;
        }

        std::vector<size_t> TraceLogger::findEntriesByRegister(const std::string& register_name, uint32_t value) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            std::vector<size_t> results;

            for(size_t i = 0; i < trace_log_.size(); ++i) {
                // Use lazy-loaded legacy map for dynamic register lookups
                const auto& regs = trace_log_[i].getLegacyRegisters();
                auto it = regs.find(register_name);
                if(it != regs.end() && it->second == value) {
                    results.push_back(i);
                }
            }

            return results;
        }

        //=============================================================================
        // Export Functions
        //=============================================================================

        bool TraceLogger::exportToFile(const std::string& filename, const std::string& format) const {
            if(format == "csv") {
                return exportToCSV(filename);
            }
            else if(format == "xml") {
                return exportToXML(filename);
            }
            else {
                // Default to text format
                std::ofstream file(filename, std::ios::out | std::ios::trunc);
                if(!file.is_open()) {
                    return false;  // Failed to open file
                }
                file << exportToString();
                file.flush();
                file.close();
                return true;  // Success
            }
        }

        // Helper function to escape CSV values
        static std::string escapeCSV(const std::string& str) {
            std::string result;
            result.reserve(str.size() * 1.1);
            for(char c : str) {
                if(c == '"') {
                    result += "\"\"";  // Double quotes for CSV escaping
                }
                else {
                    result += c;
                }
            }
            return result;
        }

        // Helper function to escape XML values
        static std::string escapeXML(const std::string& str) {
            std::string result;
            result.reserve(str.size() * 1.2);
            for(char c : str) {
                switch(c) {
                case '&':  result += "&amp;"; break;
                case '<':  result += "&lt;"; break;
                case '>':  result += "&gt;"; break;
                case '"':  result += "&quot;"; break;
                case '\'': result += "&apos;"; break;
                default:   result += c; break;
                }
            }
            return result;
        }

        bool TraceLogger::exportToCSV(const std::string& filename) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            std::ofstream file(filename, std::ios::out | std::ios::trunc);
            if(!file.is_open()) return false;

            // Header
            file << "Address,CS,IP,Disassembly,Mnemonic,Operands,Frame,Cycle,Timestamp,Label,SourceLine";
            if(include_registers_) {
                file << ",RegisterChanges";
            }
            file << "\n";

            // Data - track register changes
            FastRegisterSnapshot prev_regs{};
            bool have_prev = false;

            for(const auto& entry : trace_log_) {
                // Get symbol information
                std::string label;
                std::string source_line;
                
                if(symbol_manager_) {
                    if(auto* sym = symbol_manager_->findSymbol(entry.address)) {
                        label = sym->name;
                        source_line = sym->sourceline;
                    }
                    else {
                        label = symbol_manager_->getSymbolName(entry.address, true);
                    }
                }

                file << std::hex << entry.address << "," << entry.cs << "," << entry.ip << ","
                    << "\"" << escapeCSV(entry.disassembly) << "\",\"" << escapeCSV(entry.instruction_mnemonic) << "\",\""
                    << escapeCSV(entry.instruction_operands) << "\"," << std::dec << entry.frame_number << ","
                    << entry.cycle_count << "," << entry.getTimestampString() << ","
                    << "\"" << escapeCSV(label) << "\",\"" << escapeCSV(source_line) << "\"";

                if(include_registers_) {
                    file << ",\"";
                    if(!have_prev) {
                        // First entry - show all registers
                        file << std::hex << std::uppercase
                            << "EAX=" << entry.fast_registers.eax
                            << " EBX=" << entry.fast_registers.ebx
                            << " ECX=" << entry.fast_registers.ecx
                            << " EDX=" << entry.fast_registers.edx
                            << " ESI=" << entry.fast_registers.esi
                            << " EDI=" << entry.fast_registers.edi
                            << " ESP=" << entry.fast_registers.esp
                            << " EBP=" << entry.fast_registers.ebp
                            << " EFL=" << entry.fast_registers.eflags;
                    }
                    else {
                        // Show only changed registers
                        bool first = true;
                        auto emit_change = [&](const char* name, uint32_t old_val, uint32_t new_val) {
                            if(old_val != new_val) {
                                if(!first) file << " ";
                                file << std::hex << std::uppercase << name << ":" << old_val << "->" << new_val;
                                first = false;
                            }
                            };

                        emit_change("EAX", prev_regs.eax, entry.fast_registers.eax);
                        emit_change("EBX", prev_regs.ebx, entry.fast_registers.ebx);
                        emit_change("ECX", prev_regs.ecx, entry.fast_registers.ecx);
                        emit_change("EDX", prev_regs.edx, entry.fast_registers.edx);
                        emit_change("ESI", prev_regs.esi, entry.fast_registers.esi);
                        emit_change("EDI", prev_regs.edi, entry.fast_registers.edi);
                        emit_change("ESP", prev_regs.esp, entry.fast_registers.esp);
                        emit_change("EBP", prev_regs.ebp, entry.fast_registers.ebp);
                        emit_change("EFL", prev_regs.eflags, entry.fast_registers.eflags);

                        if(first) {
                            file << "-";  // No changes
                        }
                    }
                    file << "\"";
                    prev_regs = entry.fast_registers;
                    have_prev = true;
                }

                file << "\n";
            }

            file.flush();
            file.close();
            return true;  // Success
        }

        bool TraceLogger::exportToXML(const std::string& filename) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            std::ofstream file(filename, std::ios::out | std::ios::trunc);
            if(!file.is_open()) return false;

            file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            file << "<trace>\n";
            file << "  <statistics>\n";
            file << "    <total_instructions>" << stats_.total_instructions << "</total_instructions>\n";
            file << "    <unique_addresses>" << stats_.unique_addresses << "</unique_addresses>\n";
            file << "  </statistics>\n";
            file << "  <entries>\n";

            // Track register changes
            FastRegisterSnapshot prev_regs{};
            bool have_prev = false;

            for(const auto& entry : trace_log_) {
                // Get symbol information
                std::string label;
                std::string source_line;
                
                if(symbol_manager_) {
                    if(auto* sym = symbol_manager_->findSymbol(entry.address)) {
                        label = sym->name;
                        source_line = sym->sourceline;
                    }
                    else {
                        label = symbol_manager_->getSymbolName(entry.address, true);
                    }
                }

                file << "    <entry>\n";
                file << "      <address>" << std::hex << entry.address << "</address>\n";
                file << "      <cs>" << entry.cs << "</cs>\n";
                file << "      <ip>" << entry.ip << "</ip>\n";
                file << "      <disassembly>" << escapeXML(entry.disassembly) << "</disassembly>\n";
                file << "      <mnemonic>" << escapeXML(entry.instruction_mnemonic) << "</mnemonic>\n";
                file << "      <operands>" << escapeXML(entry.instruction_operands) << "</operands>\n";
                file << "      <frame>" << std::dec << entry.frame_number << "</frame>\n";
                file << "      <cycle>" << entry.cycle_count << "</cycle>\n";
                file << "      <label>" << escapeXML(label) << "</label>\n";
                file << "      <source_line>" << escapeXML(source_line) << "</source_line>\n";

                if(include_registers_) {
                    file << "      <register_changes>\n";

                    if(!have_prev) {
                        // First entry - show all registers
                        file << "        <EAX>" << std::hex << std::uppercase << entry.fast_registers.eax << "</EAX>\n";
                        file << "        <EBX>" << entry.fast_registers.ebx << "</EBX>\n";
                        file << "        <ECX>" << std::hex << entry.fast_registers.ecx << "</ECX>\n";
                        file << "        <EDX>" << std::hex << entry.fast_registers.edx << "</EDX>\n";
                        file << "        <ESI>" << std::hex << entry.fast_registers.esi << "</ESI>\n";
                        file << "        <EDI>" << std::hex << entry.fast_registers.edi << "</EDI>\n";
                        file << "        <ESP>" << std::hex << entry.fast_registers.esp << "</ESP>\n";
                        file << "        <EBP>" << std::hex << entry.fast_registers.ebp << "</EBP>\n";
                        file << "        <EFL>" << entry.fast_registers.eflags << "</EFL>\n";
                    }
                    else {
                        // Show only changed registers
                        auto emit_change = [&](const char* name, uint32_t old_val, uint32_t new_val) {
                            if(old_val != new_val) {
                                file << "        <" << name << " old=\"" << std::hex << std::uppercase
                                    << old_val << "\" new=\"" << new_val << "\"/>\n";
                            }
                            };

                        emit_change("EAX", prev_regs.eax, entry.fast_registers.eax);
                        emit_change("EBX", prev_regs.ebx, entry.fast_registers.ebx);
                        emit_change("ECX", prev_regs.ecx, entry.fast_registers.ecx);
                        emit_change("EDX", prev_regs.edx, entry.fast_registers.edx);
                        emit_change("ESI", prev_regs.esi, entry.fast_registers.esi);
                        emit_change("EDI", prev_regs.edi, entry.fast_registers.edi);
                        emit_change("ESP", prev_regs.esp, entry.fast_registers.esp);
                        emit_change("EBP", prev_regs.ebp, entry.fast_registers.ebp);
                        emit_change("EFL", prev_regs.eflags, entry.fast_registers.eflags);
                    }

                    file << "      </register_changes>\n";
                    prev_regs = entry.fast_registers;
                    have_prev = true;
                }

                file << "    </entry>\n";
            }

            file << "  </entries>\n";
            file << "</trace>\n";

            file.flush();
            file.close();
            return true;  // Success
        }

        std::string TraceLogger::exportToString() const {
            std::stringstream ss;

            // Header
            ss << "DOSBox-X Trace Log\n";
            ss << "==================\n\n";

            // Statistics
            ss << stats_.getFormattedStats() << "\n\n";

            // Entries
            ss << "Trace Entries:\n";
            ss << "--------------\n";

            // Track register changes
            FastRegisterSnapshot prev_regs{};
            bool have_prev = false;

            for(const auto& entry : trace_log_) {
                // Use formatEntryAsText which now includes symbol resolution
                ss << formatEntryAsText(entry);

                if(include_registers_) {
                    if(!have_prev) {
                        // First entry - show all registers
                        ss << " ; EAX=" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
                            << entry.fast_registers.eax
                            << " EBX=" << std::setw(8) << entry.fast_registers.ebx
                            << " ECX=" << std::setw(8) << entry.fast_registers.ecx
                            << " EDX=" << std::setw(8) << entry.fast_registers.edx
                            << " ESI=" << std::setw(8) << entry.fast_registers.esi
                            << " EDI=" << std::setw(8) << entry.fast_registers.edi
                            << " ESP=" << std::setw(8) << entry.fast_registers.esp
                            << " EBP=" << std::setw(8) << entry.fast_registers.ebp
                            << " EFL=" << std::setw(8) << entry.fast_registers.eflags;
                    }
                    else {
                        // Show only changed registers
                        bool any_changes = false;
                        std::stringstream changes;

                        auto emit_change = [&](const char* name, uint32_t old_val, uint32_t new_val) {
                            if(old_val != new_val) {
                                if(any_changes) changes << " ";
                                changes << name << ":" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
                                    << old_val << "->" << std::setw(8) << new_val;
                                any_changes = true;
                            }
                            };

                        emit_change("EAX", prev_regs.eax, entry.fast_registers.eax);
                        emit_change("EBX", prev_regs.ebx, entry.fast_registers.ebx);
                        emit_change("ECX", prev_regs.ecx, entry.fast_registers.ecx);
                        emit_change("EDX", prev_regs.edx, entry.fast_registers.edx);
                        emit_change("ESI", prev_regs.esi, entry.fast_registers.esi);
                        emit_change("EDI", prev_regs.edi, entry.fast_registers.edi);
                        emit_change("ESP", prev_regs.esp, entry.fast_registers.esp);
                        emit_change("EBP", prev_regs.ebp, entry.fast_registers.ebp);
                        emit_change("EFL", prev_regs.eflags, entry.fast_registers.eflags);

                        if(any_changes) {
                            ss << " ; " << changes.str();
                        }
                    }

                    prev_regs = entry.fast_registers;
                    have_prev = true;
                }

                ss << "\n";
            }

            return ss.str();
        }

        //=============================================================================
        // Analysis Functions
        //=============================================================================

        // Duplicate implementations removed - these are already defined earlier in the file

        std::map<uint32_t, uint64_t> TraceLogger::getHotspots(size_t top_count) const {
            auto frequency = getAddressFrequency();
            std::map<uint32_t, uint64_t> hotspots;

            // Convert to vector for sorting
            std::vector<std::pair<uint32_t, uint32_t>> freq_vec(frequency.begin(), frequency.end());
            std::sort(freq_vec.begin(), freq_vec.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });

            // Take top N
            size_t count = std::min(top_count, freq_vec.size());
            for(size_t i = 0; i < count; ++i) {
                hotspots[freq_vec[i].first] = freq_vec[i].second;
            }

            return hotspots;
        }


        std::vector<uint32_t> TraceLogger::getUniqueAddresses() const {
            std::set<uint32_t> unique_set;

            for(const auto& entry : trace_log_) {
                unique_set.insert(entry.address);
            }

            return std::vector<uint32_t>(unique_set.begin(), unique_set.end());
        }

        std::map<uint32_t, uint32_t> TraceLogger::getAddressFrequency() const {
            return stats_.address_frequency;
        }

        std::map<std::string, uint32_t> TraceLogger::getInstructionFrequency() const {
            return stats_.instruction_frequency;
        }

        void TraceLogger::onFrameStart() {
            current_frame_++;
        }

        void TraceLogger::onFrameEnd() {
            // Frame ended - could be used for frame-based analysis
        }

        //=============================================================================
        // Helper Functions
        //=============================================================================

        void TraceLogger::trimToMaxEntries() {
            while(trace_log_.size() > max_entries_) {
                trace_log_.pop_front();
            }
        }

        bool TraceLogger::handleRangeAutoControl(uint32_t address) {
            if(!filter_.enabled || (!filter_.autostart_in_range && !filter_.autostop_at_end)) {
                return true; // Continue normal processing (check enabled flag later)
            }

            // Start Trigger
            if(filter_.autostart_in_range && !enabled_.load(std::memory_order_relaxed)) {
                if(address == filter_.start_address) {
                    enabled_.store(true, std::memory_order_relaxed);
                    range_active_ = true;
                    if(onTracingStateChanged) onTracingStateChanged(true);
                }
            }

            // Stop Trigger
            if(filter_.autostop_at_end && enabled_.load(std::memory_order_relaxed)) {
                if(address == filter_.end_address) {
                    // Stop tracing
                    enabled_.store(false, std::memory_order_relaxed);
                    range_active_ = false;
                    if(onTracingStateChanged) onTracingStateChanged(false);

                    // Note: The instruction at end_address will be skipped by addEntry
                    // because enabled_ is now false. This is typical "stop at" breakpoint behavior.
                }
            }

            return true;
        }

        bool TraceLogger::passesFilter(const TraceEntry& entry) const {
            return filter_.shouldInclude(entry);
        }

        std::string TraceLogger::formatEntryAsCSV(const TraceEntry& entry) const {
            std::stringstream ss;
            ss << std::hex << entry.address << "," << entry.cs << "," << entry.ip << ","
                << "\"" << entry.disassembly << "\",\"" << entry.instruction_mnemonic << "\",\""
                << entry.instruction_operands << "\"," << entry.frame_number << ","
                << entry.cycle_count << "," << entry.getTimestampString();

            if(include_registers_) {
                // OPTIMIZATION: Use fast_registers directly
                ss << "," << std::hex << entry.fast_registers.eax;
                ss << "," << std::hex << entry.fast_registers.ebx;
                ss << "," << std::hex << entry.fast_registers.ecx;
                ss << "," << std::hex << entry.fast_registers.edx;
                ss << "," << std::hex << entry.fast_registers.esi;
                ss << "," << std::hex << entry.fast_registers.edi;
                ss << "," << std::hex << entry.fast_registers.esp;
                ss << "," << std::hex << entry.fast_registers.ebp;
            }

            return ss.str();
        }

        std::string TraceLogger::formatEntryAsXML(const TraceEntry& entry) const {
            std::stringstream ss;
            ss << "    <entry>\n";
            ss << "      <address>" << std::hex << entry.address << "</address>\n";
            ss << "      <cs>" << entry.cs << "</cs>\n";
            ss << "      <ip>" << entry.ip << "</ip>\n";
            ss << "      <disassembly>" << entry.disassembly << "</disassembly>\n";
            ss << "      <mnemonic>" << entry.instruction_mnemonic << "</mnemonic>\n";
            ss << "      <operands>" << entry.instruction_operands << "</operands>\n";
            ss << "      <frame>" << entry.frame_number << "</frame>\n";
            ss << "      <cycle>" << entry.cycle_count << "</cycle>\n";

            if(include_registers_) {
                // OPTIMIZATION: Use fast_registers directly
                ss << "      <registers>\n";
                ss << "        <EAX>" << std::hex << entry.fast_registers.eax << "</EAX>\n";
                ss << "        <EBX>" << std::hex << entry.fast_registers.ebx << "</EBX>\n";
                ss << "        <ECX>" << std::hex << entry.fast_registers.ecx << "</ECX>\n";
                ss << "        <EDX>" << std::hex << entry.fast_registers.edx << "</EDX>\n";
                ss << "        <ESI>" << std::hex << entry.fast_registers.esi << "</ESI>\n";
                ss << "        <EDI>" << std::hex << entry.fast_registers.edi << "</EDI>\n";
                ss << "        <ESP>" << std::hex << entry.fast_registers.esp << "</ESP>\n";
                ss << "        <EBP>" << std::hex << entry.fast_registers.ebp << "</EBP>\n";
                ss << "      </registers>\n";
            }

            ss << "    </entry>\n";
            return ss.str();
        }

        void TraceLogger::analyzeInstruction(const std::string& disassembly, TraceEntry& entry) {
            // Simple instruction parsing - extract mnemonic and operands
            size_t space_pos = disassembly.find(' ');
            if(space_pos != std::string::npos) {
                entry.instruction_mnemonic = disassembly.substr(0, space_pos);
                entry.instruction_operands = disassembly.substr(space_pos + 1);
            }
            else {
                entry.instruction_mnemonic = disassembly;
                entry.instruction_operands = "";
            }

            // Convert to uppercase for consistency
            std::transform(entry.instruction_mnemonic.begin(), entry.instruction_mnemonic.end(),
                entry.instruction_mnemonic.begin(), ::toupper);
        }

        void TraceLogger::updateCallStack(const TraceEntry& entry) {
            if(!track_call_stack_) return;

            std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);

            if(isCallInstruction(entry.instruction_mnemonic)) {
                // Push return address (instruction after CALL) for accurate stack walking
                uint32_t return_address = entry.address + entry.instruction_length;
                call_stack_.push_back(return_address);

                // Limit depth to prevent memory issues
                const size_t MAX_CALL_DEPTH = 256;
                if(call_stack_.size() > MAX_CALL_DEPTH) {
                    call_stack_.erase(call_stack_.begin());
                }
            }
            else if(isRetInstruction(entry.instruction_mnemonic)) {
                if(!call_stack_.empty()) {
                    // Normal return - pop stack
                    call_stack_.pop_back();
                }
                // If stack is empty, this might be returning from a function
                // that was called before tracing started - that's okay
            }
        }

        bool TraceLogger::isCallInstruction(const std::string& mnemonic) const {
            return mnemonic == "CALL" || mnemonic == "CALLF";
        }

        bool TraceLogger::isJumpInstruction(const std::string& mnemonic) const {
            return mnemonic == "JMP" || mnemonic == "JMPF" ||
                mnemonic.length() >= 2 && mnemonic[0] == 'J' && mnemonic != "JMP";
        }

        bool TraceLogger::isRetInstruction(const std::string& mnemonic) const {
            return mnemonic == "RET" || mnemonic == "RETF" || mnemonic == "RETN";
        }

        bool TraceLogger::isInterruptInstruction(const std::string& mnemonic) const {
            return mnemonic == "INT" || mnemonic == "INTO" || mnemonic == "IRET";
        }

        //=============================================================================
        // Enhanced Helper Methods
        //=============================================================================

        void TraceLogger::addEntryInternal(const TraceEntry& entry) {
            if(!passesFilter(entry) || !passesRateLimit()) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(trace_mutex_);
                trace_log_.push_back(entry);

                // PR1-011: increment rate-limit counter under trace_mutex_ (defect 12)
                ++events_this_second_;

                // Update statistics
                stats_.updateEventStats(entry);

                // Trim if necessary
                trimToMaxEntries();
            }

            // Add to write queue if file output is active
            if(output_file_) {
                std::lock_guard<std::mutex> write_lock(write_mutex_);
                write_queue_.push_back(entry);
            }

            // Fire callback
            if(onEntryAdded) {
                onEntryAdded(entry);
            }
        }

        bool TraceLogger::passesRateLimit() const {
            if(!filter_.enable_rate_limiting) return true;

            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_event_time_);

            if(duration.count() >= 1) {
                // Reset counter for new second (members are now mutable)
                std::lock_guard<std::mutex> lock(trace_mutex_);
                events_this_second_ = 0;
                last_event_time_ = now;
            }

            std::lock_guard<std::mutex> lock(trace_mutex_);
            return events_this_second_ < filter_.max_events_per_second;
        }

        std::string TraceLogger::decodeJapaneseText(uint32_t address, size_t max_length, uint16_t codepage) const {
            if(!memory_manager_) return "";

            // Read memory bytes
            std::vector<uint8_t> bytes;
            bytes.reserve(max_length);

            for(size_t i = 0; i < max_length; ++i) {
                uint8_t byte = memory_manager_->readByteAt(address + i);
                if(byte == 0) break; // Null terminator
                bytes.push_back(byte);
            }

            if(bytes.empty()) return "";

            if(codepage == 932) {
                // SJIS decoding (simplified)
                return convertSJISToUTF8(bytes);
            }
            else {
                // ASCII/other codepage
                std::string result;
                for(uint8_t byte : bytes) {
                    if(byte >= 32 && byte <= 126) {
                        result += static_cast<char>(byte);
                    }
                    else {
                        result += '.';
                    }
                }
                return result;
            }
        }

        std::string TraceLogger::convertSJISToUTF8(const std::vector<uint8_t>& sjis_data) const {
            std::string result;

            for(size_t i = 0; i < sjis_data.size(); ++i) {
                uint8_t byte = sjis_data[i];

                if(byte <= 0x7F) {
                    // ASCII
                    result += static_cast<char>(byte);
                }
                else if(byte >= 0xA1 && byte <= 0xDF) {
                    // Half-width katakana
                    result += static_cast<char>(byte);
                }
                else if((byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC)) {
                    // Double-byte character
                    if(i + 1 < sjis_data.size()) {
                        // For now, just represent as [XX YY] placeholder
                        result += "[" + std::to_string(byte) + " " + std::to_string(sjis_data[i + 1]) + "]";
                        i++; // Skip next byte
                    }
                    else {
                        result += '.';
                    }
                }
                else {
                    result += '.';
                }
            }

            return result;
        }

        bool TraceLogger::isValidSJISSequence(const std::vector<uint8_t>& data) const {
            for(size_t i = 0; i < data.size(); ++i) {
                uint8_t byte = data[i];

                if(byte <= 0x7F) {
                    // ASCII - valid
                    continue;
                }
                else if(byte >= 0xA1 && byte <= 0xDF) {
                    // Half-width katakana - valid
                    continue;
                }
                else if((byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC)) {
                    // First byte of double-byte character
                    if(i + 1 < data.size()) {
                        uint8_t second_byte = data[i + 1];
                        if(second_byte >= 0x40 && second_byte <= 0xFC && second_byte != 0x7F) {
                            i++; // Valid double-byte sequence
                            continue;
                        }
                        else {
                            return false; // Invalid second byte
                        }
                    }
                    else {
                        return false; // Incomplete double-byte sequence
                    }
                }
                else {
                    return false; // Invalid byte
                }
            }

            return true;
        }

        bool TraceLogger::isBranchInstruction(const std::string& mnemonic) const {
            return mnemonic.length() >= 2 && mnemonic[0] == 'J' && mnemonic != "JMP";
        }

        bool TraceLogger::isStringInstruction(const std::string& mnemonic) const {
            return mnemonic == "MOVS" || mnemonic == "MOVSB" || mnemonic == "MOVSW" ||
                mnemonic == "CMPS" || mnemonic == "CMPSB" || mnemonic == "CMPSW" ||
                mnemonic == "SCAS" || mnemonic == "SCASB" || mnemonic == "SCASW" ||
                mnemonic == "LODS" || mnemonic == "LODSB" || mnemonic == "LODSW" ||
                mnemonic == "STOS" || mnemonic == "STOSB" || mnemonic == "STOSW";
        }

        bool TraceLogger::startFileOutput(const std::string& filename, const std::string& format) {
            stopFileOutput(); // Stop any existing output

            output_file_ = std::make_unique<std::ofstream>(filename);
            if(!output_file_->is_open()) {
                return false;
            }

            export_format_ = format;
            writer_should_stop_ = false;

            // Start writer thread
            writer_thread_ = std::thread(&TraceLogger::writerThreadFunc, this);

            return true;
        }

        void TraceLogger::stopFileOutput() {
            if(writer_thread_.joinable()) {
                writer_should_stop_ = true;
                writer_thread_.join();
            }

            if(output_file_) {
                output_file_->close();
                output_file_.reset();
            }
        }

        bool TraceLogger::isFileOutputActive() const {
            return output_file_ && output_file_->is_open();
        }

        void TraceLogger::writerThreadFunc() {
            while(!writer_should_stop_) {
                std::vector<TraceEntry> entries_to_write;

                {
                    std::lock_guard<std::mutex> lock(write_mutex_);
                    if(!write_queue_.empty()) {
                        entries_to_write.assign(write_queue_.begin(), write_queue_.end());
                        write_queue_.clear();
                    }
                }

                if(!entries_to_write.empty() && output_file_) {
                    for(const auto& entry : entries_to_write) {
                        writeEntryToFile(entry);
                    }
                    output_file_->flush();
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Flush remaining entries
            flushWriteQueue();
        }

        void TraceLogger::writeEntryToFile(const TraceEntry& entry) {
            if(!output_file_) return;

            if(export_format_ == "csv") {
                *output_file_ << formatEntryAsCSV(entry) << "\n";
            }
            else if(export_format_ == "xml") {
                *output_file_ << formatEntryAsXML(entry);
            }
            else if(export_format_ == "json") {
                *output_file_ << formatEntryAsJSON(entry) << "\n";
            }
            else {
                *output_file_ << formatEntryAsText(entry) << "\n";
            }
        }

        void TraceLogger::flushWriteQueue() {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if(output_file_) {
                for(const auto& entry : write_queue_) {
                    writeEntryToFile(entry);
                }
                write_queue_.clear();
                output_file_->flush();
            }
        }

        std::string TraceLogger::formatEntryAsJSON(const TraceEntry& entry) const {
            std::stringstream ss;
            ss << "{";
            ss << "\"type\":\"" << entry.getEventTypeString() << "\",";
            ss << "\"address\":\"" << std::hex << entry.address << "\",";
            ss << "\"disassembly\":\"" << entry.disassembly << "\",";
            ss << "\"timestamp\":" << entry.getTimestampString();

            // Add symbol information
            std::string label;
            std::string source_line;
            
            if(symbol_manager_) {
                if(auto* sym = symbol_manager_->findSymbol(entry.address)) {
                    label = sym->name;
                    source_line = sym->sourceline;
                }
                else {
                    label = symbol_manager_->getSymbolName(entry.address, true);
                }
            }
            
            if(!label.empty()) {
                ss << ",\"label\":\"" << label << "\"";
            }
            
            if(!source_line.empty()) {
                ss << ",\"source_line\":\"" << source_line << "\"";
            }

            if(!entry.string_data_sjis.empty()) {
                ss << ",\"sjis_text\":\"" << entry.string_data_sjis << "\"";
            }

            if(!entry.string_data_utf8.empty()) {
                ss << ",\"utf8_text\":\"" << entry.string_data_utf8 << "\"";
            }

            ss << "}";
            return ss.str();
        }

        std::string TraceLogger::formatEntryAsText(const TraceEntry& entry) const {
            std::stringstream ss;
            ss << "[" << entry.getEventTypeString() << "] ";
            ss << entry.getAddressString() << " ";
            
            // Add symbol information if available
            std::string label;
            std::string source_line;
            
            if(symbol_manager_) {
                if(auto* sym = symbol_manager_->findSymbol(entry.address)) {
                    label = sym->name;
                    source_line = sym->sourceline;
                }
                else {
                    label = symbol_manager_->getSymbolName(entry.address, true);
                }
            }
            
            if(!label.empty()) {
                ss << "(" << label << ") ";
            }
            
            if(!source_line.empty()) {
                ss << source_line << " ";
            } else {
                ss << entry.disassembly << " ";
            }

            std::string japanese_text = entry.getJapaneseTextString();
            if(!japanese_text.empty()) {
                ss << " " << japanese_text;
            }

            return ss.str();
        }

        //=============================================================================
        // Enhanced Statistics Implementation
        //=============================================================================

        void TraceStatistics::updateEventStats(const TraceEntry& entry) {
            total_events++;
            events_by_type[static_cast<int>(entry.event_type)]++;

            switch(entry.event_type) {
            case TraceEventType::INSTRUCTION:
                total_instructions++;
                break;
            case TraceEventType::MEMORY_READ:
                total_memory_reads++;
                break;
            case TraceEventType::MEMORY_WRITE:
                total_memory_writes++;
                break;
            case TraceEventType::IO_READ:
            case TraceEventType::IO_WRITE:
                total_io_operations++;
                break;
            case TraceEventType::STRING_ACCESS:
                if(entry.text_codepage == 932) {
                    sjis_string_accesses++;
                }
                else {
                    ascii_string_accesses++;
                }
                codepage_usage[entry.text_codepage]++;
                break;
            default:
                break;
            }

            total_execution_cycles += entry.execution_time_cycles;
            end_time = entry.timestamp;

            // Update performance metrics
            auto duration = getDuration();
            if(duration.count() > 0) {
                events_per_second = (total_events * 1000.0) / duration.count();
            }
        }

        std::chrono::milliseconds TraceStatistics::getDuration() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        }

        size_t TraceLogger::getEntryCount() const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            return trace_log_.size();
        }

        std::vector<uint32_t> TraceLogger::getCallStack() const {
            std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);
            return call_stack_;
        }

        void TraceLogger::clearCallStack() {
            std::lock_guard<std::recursive_mutex> lock(call_stack_mutex_);
            call_stack_.clear();
        }

        std::vector<TraceEntry> TraceLogger::getEntries() const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            return std::vector<TraceEntry>(trace_log_.begin(), trace_log_.end());
        }

        std::vector<TraceEntry> TraceLogger::getRecentEntries(size_t count) const {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            size_t start = (trace_log_.size() > count) ? trace_log_.size() - count : 0;
            return std::vector<TraceEntry>(trace_log_.begin() + static_cast<ptrdiff_t>(start), trace_log_.end());
        }

    //=============================================================================
    // PR4: FlowType Lookup Table
    //=============================================================================

    static const FlowType s_flow_table[256] = {
        /* 00 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 04 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 08 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 0C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 10 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 14 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 18 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 1C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 20 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 24 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 28 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 2C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 30 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 34 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 38 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 3C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 40 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 44 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 48 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 4C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 50 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 54 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 58 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 5C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 60 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 64 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 68 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 6C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 70 */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 72 */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 74 */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 76 */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 78 */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 7A */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 7C */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 7E */ FlowType::CONDITIONAL_BRANCH, FlowType::CONDITIONAL_BRANCH,
        /* 80 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 84 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 88 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 8C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 90 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 94 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 98 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* 9C */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* A0 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* A4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* A8 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* AC */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* B0 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* B4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* B8 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* BC */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* C0 */ FlowType::NONE, FlowType::NONE, FlowType::RET, FlowType::RET,
        /* C4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* C8 */ FlowType::RET, FlowType::RET, FlowType::RET, FlowType::RET,
        /* CC */ FlowType::INTERRUPT, FlowType::INTERRUPT, FlowType::INTERRUPT, FlowType::INTERRUPT,
        /* D0 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* D4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* D8 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* DC */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* E0 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* E4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* E8 */ FlowType::CALL, FlowType::JMP, FlowType::JMP, FlowType::JMP,
        /* EC */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* F0 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* F4 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* F8 */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
        /* FC */ FlowType::NONE, FlowType::NONE, FlowType::NONE, FlowType::NONE,
    };

    FlowType flowTypeFromOpcode(uint8_t opcode) {
        // 0F prefix: second byte determines conditional branch (0F 80-8F)
        // Caller must handle 0F prefix separately; we only classify the first byte.
        if(opcode == 0x0F) return FlowType::CONDITIONAL_BRANCH;
        return s_flow_table[opcode];
    }

    //=============================================================================
    // PR4: TraceRingBuffer Implementation
    //=============================================================================

    static size_t nextPowerOf2(size_t v) {
        if(v == 0) return 1;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    }

    TraceRingBuffer::TraceRingBuffer(size_t capacity)
        : buffer_(nextPowerOf2(capacity)),
          mask_(buffer_.size() - 1) {
    }

    void TraceRingBuffer::push(const RawTraceEntry& entry) {
        uint64_t idx = write_index_.fetch_add(1, std::memory_order_relaxed);
        size_t slot = idx & mask_;
        // Check if we're overwriting an unread entry
        if(idx > buffer_.size()) {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
        }
        buffer_[slot] = entry;
    }

    size_t TraceRingBuffer::getEntriesRange(size_t start, size_t count,
                                             RawTraceEntry* out) const {
        std::lock_guard<std::mutex> lock(read_mutex_);
        uint64_t total = write_index_.load(std::memory_order_relaxed);
        if(start >= total) return 0;

        size_t avail = static_cast<size_t>(total - start);
        size_t n = std::min(count, avail);

        for(size_t i = 0; i < n; ++i) {
            uint64_t idx = start + i;
            size_t slot = idx & mask_;
            out[i] = buffer_[slot];
        }
        return n;
    }

    size_t TraceRingBuffer::size() const {
        return write_index_.load(std::memory_order_relaxed);
    }

    void TraceRingBuffer::clear() {
        write_index_.store(0, std::memory_order_relaxed);
        dropped_count_.store(0, std::memory_order_relaxed);
        sampled_count_.store(0, std::memory_order_relaxed);
    }

    //=============================================================================
    // PR4: DecodeCache Implementation
    //=============================================================================

    DecodeCache::DecodeCache(size_t cap) : cap_(cap) {
    }

    const std::string& DecodeCache::getOrDecode(uint32_t address,
                                                  LuaEngineDebug::CoreDebugInterface* iface) {
        auto it = cache_.find(address);
        if(it != cache_.end()) {
            // Move to back of LRU (most recently used)
            auto lru_it = std::find(lru_order_.begin(), lru_order_.end(), address);
            if(lru_it != lru_order_.end()) {
                lru_order_.erase(lru_it);
            }
            lru_order_.push_back(address);
            return it->second;
        }

        // Cache miss — decode
        std::string disasm;
        if(iface) {
            disasm = iface->disassembleInstruction(address);
        }

        // Evict LRU if at capacity
        while(cache_.size() >= cap_ && !lru_order_.empty()) {
            uint32_t evict_addr = lru_order_.front();
            lru_order_.erase(lru_order_.begin());
            cache_.erase(evict_addr);
        }

        auto result = cache_.emplace(address, std::move(disasm));
        lru_order_.push_back(address);
        return result.first->second;
    }

    void DecodeCache::clear() {
        cache_.clear();
        lru_order_.clear();
    }

    //=============================================================================
    // PR4: SnapshotStore Implementation
    //=============================================================================

    uint32_t SnapshotStore::push(const DebuggerUiSnapshot& snapshot) {
        uint32_t idx = write_index_.fetch_add(1, std::memory_order_relaxed);
        size_t slot = idx % buffer_.size();
        buffer_[slot] = snapshot;
        return idx;
    }

    const DebuggerUiSnapshot& SnapshotStore::get(uint32_t index) const {
        static const DebuggerUiSnapshot s_empty{};
        if(buffer_.empty()) return s_empty;
        size_t slot = index % buffer_.size();
        return buffer_[slot];
    }

    //=============================================================================
    // PR4: TraceLogger Hot-Path addRawEntry
    //=============================================================================

    void TraceLogger::addRawEntry(uint32_t address, uint16_t cs, uint16_t ip, uint8_t opcode_byte) {
        if(!handleRangeAutoControl(address)) return;
        if(!enabled_ || paused_) return;

        // Check trace interval
        if(trace_interval_ > 1) {
            trace_counter_++;
            if(trace_counter_ % trace_interval_ != 0) return;
        }

        // Classify flow from opcode byte — zero string operations
        // ponytail: 0F prefix classified as CONDITIONAL_BRANCH (slight over-classification
        // for SETcc/MOVZX, but correct for Jcc which is the common case)
        FlowType ft = flowTypeFromOpcode(opcode_byte);

        // Capture register snapshot for this UI frame
        uint32_t snap_idx = current_snapshot_index_;
        if(debug_interface_ && include_registers_) {
            DebuggerUiSnapshot snap{};
            debug_interface_->getCpuRegistersFast(snap.registers);
            snap.frame_number = current_frame_;
            snap_idx = snapshot_store_->push(snap);
        }

        // Pack timestamp as relative ms from trace start
        auto now = std::chrono::steady_clock::now();
        auto rel_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - trace_start_time_).count();
        uint32_t ts_packed = static_cast<uint32_t>(rel_ms & 0xFFFFFFFF);

        RawTraceEntry entry{};
        entry.address = address;
        entry.cs = cs;
        entry.ip = ip;
        entry.opcode_byte = opcode_byte;
        entry.flow_type = ft;
        entry.event_type = static_cast<uint8_t>(TraceEventType::INSTRUCTION);
        entry._pad = 0;
        entry.snapshot_index = snap_idx;
        entry.timestamp_packed = ts_packed;
        entry.frame = current_frame_;
        entry.cycle = current_cycle_;

        // Write to ring buffer — O(1), zero heap allocs on the hot path
        ring_buf_->push(entry);

        // ponytail: PR6 — push register snapshot to reverse-step ring if enabled
        if(reverse_step_enabled_ && debug_interface_) {
            FastRegisterSnapshot snap;
            debug_interface_->getCpuRegistersFast(snap);
            std::lock_guard<std::mutex> rlock(reverse_ring_mutex_);
            reverse_ring_.push_back(snap);
            while(reverse_ring_.size() > reverse_ring_size_) {
                reverse_ring_.pop_front();
            }
        }

        // Update lightweight stats (no string operations)
        // ponytail: stats_ updates are not under trace_mutex_ here; 
        // integer counters are fine for best-effort display, address_frequency map
        // may have rare races. Upgrade to atomic counters if accuracy matters.
        stats_.total_instructions++;
        if(ft == FlowType::CALL) stats_.call_count++;
        else if(ft == FlowType::JMP || ft == FlowType::CONDITIONAL_BRANCH) stats_.jump_count++;
        else if(ft == FlowType::RET) stats_.ret_count++;
        else if(ft == FlowType::INTERRUPT) stats_.interrupt_count++;
        // ponytail: skip address_frequency update on hot path to avoid map allocation;
        // unique_addresses count will be slightly low, acceptable for display

        // Fire callback — only used for auto-scroll notification, entry data not consumed
        if(onEntryAdded) {
            // ponytail: create minimal TraceEntry for callback compat; 
            // empty string uses SSO (no heap alloc), and callback only triggers auto-scroll
            TraceEntry placeholder(address, cs, ip, "");
            placeholder.frame_number = current_frame_;
            placeholder.cycle_count = current_cycle_;
            onEntryAdded(placeholder);
        }
    }

    //=============================================================================
    // PR4: Range-based trace retrieval
    //=============================================================================

    std::vector<RawTraceEntry> TraceLogger::getEntriesRange(size_t start, size_t count) const {
        std::vector<RawTraceEntry> result(count);
        size_t n = ring_buf_->getEntriesRange(start, count, result.data());
        result.resize(n);
        return result;
    }

    size_t TraceLogger::getRingEntryCount() const {
        return ring_buf_->size();
    }

    uint64_t TraceLogger::getDroppedCount() const {
        return ring_buf_->dropped_count();
    }

    void TraceLogger::onUiFrameTick() {
        if(debug_interface_ && include_registers_) {
            DebuggerUiSnapshot snap{};
            debug_interface_->getCpuRegistersFast(snap.registers);
            snap.frame_number = current_frame_;
            current_snapshot_index_ = snapshot_store_->push(snap);
        }
    }

    //=============================================================================
    // PR6: Register-only reverse-step
    //=============================================================================

    bool TraceLogger::reverseStep() {
        std::lock_guard<std::mutex> lock(reverse_ring_mutex_);
        if(reverse_ring_.empty()) return false;
        FastRegisterSnapshot snap = reverse_ring_.back();
        reverse_ring_.pop_back();

        // ponytail: PR6 — restore CPU registers via debug interface
        if(debug_interface_) {
            debug_interface_->setCpuRegistersFast(snap);
        }
        return true;
    }

    bool TraceLogger::isReverseStepAvailable() const {
        std::lock_guard<std::mutex> lock(reverse_ring_mutex_);
        return !reverse_ring_.empty();
    }

    } // namespace LuaEngineTraceLogger

