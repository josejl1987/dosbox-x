#ifndef CORE_DEBUG_INTERFACE_H
#define CORE_DEBUG_INTERFACE_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <functional>
#include <cstdint>
#include <shared_mutex>
#include <set>
#include <mutex>
#include <chrono>

// Forward declare FastRegisterSnapshot from trace_logger.h
namespace LuaEngineTraceLogger {
    struct FastRegisterSnapshot;
}

namespace LuaEngineDebug {

// Forward declaration
class EventManager;

// Breakpoint structure
struct Breakpoint {
    enum Type {
        Simple,     // Unconditional breakpoint - handled only by CBreakpoint
        Conditional // Lua/conditional breakpoint - handled by DosBoxCoreDebugger
    };

    uint32_t address;
    bool enabled = true;
    Type type = Simple;
    std::string condition;  // Lua expression for conditional breakpoints
    uint32_t hit_count = 0;
    std::string description;

    // Default constructor for unordered_map compatibility
    Breakpoint() : address(0) {}
    Breakpoint(uint32_t addr) : address(addr) {}
    Breakpoint(uint32_t addr, const std::string& desc) : address(addr), description(desc) {}

    // Helper to determine if this breakpoint needs Lua processing
    bool isConditional() const { return type == Conditional || !condition.empty(); }
};

// CPU state structure
struct CPUState {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint16_t cs, ds, es, fs, gs, ss;
    uint32_t ip;
    uint32_t flags;
    
    // Additional state for debugging
    uint16_t fpu_control;
    uint16_t fpu_status;
    uint16_t fpu_tag;
    
    // For text encoding support
    uint16_t current_codepage;
    bool japanese_mode;
};

// Disassembly result structure
struct DisassemblyLine {
    uint32_t address;
    std::string bytes;      // Raw bytes as hex string
    std::string mnemonic;   // Instruction mnemonic
    std::string operands;   // Instruction operands
    std::string full_text;  // Complete disassembly line
    uint32_t length;        // Instruction length in bytes
};

// Call stack frame structure
struct CallStackFrame {
    uint32_t address;             // Linear address of return point
    uint32_t frame_pointer;       // BP/EBP value for this frame
    uint16_t code_segment;        // CS value for this frame
    uint32_t instruction_pointer; // IP/EIP value
    std::string function_name;    // Optional: resolved symbol name
    bool is_far_call;             // Was this a far call?

    CallStackFrame() : address(0), frame_pointer(0), code_segment(0), instruction_pointer(0), is_far_call(false) {}
    CallStackFrame(uint32_t addr) : address(addr), frame_pointer(0), code_segment(0), instruction_pointer(0), is_far_call(false) {}
    CallStackFrame(uint32_t addr, uint32_t bp, uint16_t cs, uint32_t ip, bool far_call = false)
        : address(addr), frame_pointer(bp), code_segment(cs), instruction_pointer(ip), is_far_call(far_call) {}
};

// Trace-based call stack entry for more reliable tracking
struct TracedCallEntry {
    uint32_t call_site;         // Address where CALL was executed
    uint32_t target_address;    // Address being called
    uint32_t return_address;    // Address to return to
    uint16_t call_cs;           // CS at time of call
    uint16_t target_cs;         // Target CS (for far calls)
    uint32_t stack_pointer;     // ESP/SP at time of call
    bool is_far_call;
    std::chrono::steady_clock::time_point timestamp;
};

enum class BreakReason {
    Manual,
    Breakpoint,
    Step,
    RunToCursor,
    Unknown
};

class CoreDebuggerListener {
public:
    virtual ~CoreDebuggerListener() = default;
    virtual void onPaused(BreakReason reason, uint32_t address) {}
    virtual void onResumed() {}
    virtual void onStepComplete(uint32_t address) {}
    virtual void onBreakpointHit(uint32_t address, BreakReason reason) {}
    virtual void onBreakpointListChanged() {}
};

// Abstract interface for core debugging functionality
class CoreDebugInterface {
public:
    virtual ~CoreDebugInterface() = default;

    // CPU State Management
    virtual std::map<std::string, uint32_t> getCpuRegisters() = 0;

    // OPTIMIZATION: Fast register snapshot (zero allocations, cache-friendly)
    virtual void getCpuRegistersFast(LuaEngineTraceLogger::FastRegisterSnapshot& snapshot) = 0;

    // ponytail: PR6 — bulk register restore for reverse-step
    virtual void setCpuRegistersFast(const LuaEngineTraceLogger::FastRegisterSnapshot& snapshot) = 0;

    virtual CPUState getCpuState() = 0;
    virtual void setCpuRegister(const std::string& name, uint32_t value) = 0;
    virtual uint32_t getCurrentEIP() = 0;
    virtual uint16_t getCurrentCS() = 0;
    
    // Execution Control
    virtual void pause(BreakReason reason = BreakReason::Manual) = 0;
    virtual void resume() = 0;
    virtual void stepInto() = 0;
    virtual void stepOver() = 0;
    virtual void stepOut() = 0;
    virtual void reset() = 0;
    virtual void runToCursor(uint32_t address) = 0;
    virtual bool isPaused() const = 0;
    
    // Breakpoint Management
    virtual void addBreakpoint(uint32_t address) = 0;
    virtual void addBreakpoint(const Breakpoint& bp) = 0;
    virtual void removeBreakpoint(uint32_t address) = 0;
    virtual void toggleBreakpoint(uint32_t address) = 0;
    virtual void enableBreakpoint(uint32_t address, bool enabled) = 0;
    virtual void clearAllBreakpoints() = 0;
    virtual std::vector<Breakpoint> getBreakpoints() const = 0;
    virtual bool hasBreakpoint(uint32_t address) const = 0;
    
    // Disassembly
    virtual DisassemblyLine disassemble(uint32_t address) = 0;
    virtual std::vector<DisassemblyLine> disassembleRange(uint32_t start_address, int count) = 0;
    virtual std::string disassembleInstruction(uint32_t address) = 0;
    virtual std::string getInstructionBytes(uint32_t address) = 0;
    virtual std::vector<uint8_t> getInstructionBytesVector(uint32_t address) = 0;
    
    // Call Stack
    virtual std::vector<CallStackFrame> getCallStack() = 0;
    
    // Memory Access
    virtual uint8_t readMemoryByte(uint32_t address) = 0;
    virtual uint16_t readMemoryWord(uint32_t address) = 0;
    virtual uint32_t readMemoryDWord(uint32_t address) = 0;
    virtual void writeMemoryByte(uint32_t address, uint8_t value) = 0;
    virtual void writeMemoryWord(uint32_t address, uint16_t value) = 0;
    virtual void writeMemoryDWord(uint32_t address, uint32_t value) = 0;
    virtual uint8_t readByte(uint32_t address) = 0;
    virtual uint16_t readWord(uint32_t address) = 0;
    virtual uint32_t readDWord(uint32_t address) = 0;
    
    // Bulk memory operations for performance
    virtual void readMemoryBlock(uint32_t address, uint8_t* buffer, size_t size) = 0;
    virtual void writeMemoryBlock(uint32_t address, const uint8_t* buffer, size_t size) = 0;
    
    // PR4: Bulk memory read returning vector (convenience wrapper over readMemoryBlock)
    virtual std::vector<uint8_t> readMemoryRange(uint32_t address, size_t length) = 0;
    
    // Text encoding support for Japanese and other languages
    virtual std::string convertToUTF8(const uint8_t* data, size_t length, uint16_t codepage = 932) = 0;
    virtual std::vector<uint8_t> convertFromUTF8(const std::string& utf8_text, uint16_t codepage = 932) = 0;
    virtual std::string readTextFromMemory(uint32_t address, size_t length, uint16_t codepage = 932) = 0;
    virtual void writeTextToMemory(uint32_t address, const std::string& text, uint16_t codepage = 932) = 0;
    virtual uint16_t getCurrentCodepage() = 0;
    virtual void setCurrentCodepage(uint16_t codepage) = 0;
    
    // Event callbacks
    virtual void setBreakpointHitCallback(std::function<void(uint32_t)> callback) = 0;
    virtual void setPauseCallback(std::function<void()> callback) = 0;
    virtual void setResumeCallback(std::function<void()> callback) = 0;
    virtual void addListener(CoreDebuggerListener* listener) = 0;
    virtual void removeListener(CoreDebuggerListener* listener) = 0;
};

// Concrete implementation of the debug interface
class DosBoxCoreDebugger : public CoreDebugInterface {
private:
    // Execution state
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> step_into_requested_{false};
    std::atomic<bool> step_over_requested_{false};
    std::atomic<uint32_t> step_over_target_{0};
    
    // Breakpoint management
    std::vector<Breakpoint> breakpoints_;
    std::unordered_map<uint32_t, size_t> breakpoint_index_; // Fast O(1) lookup
    mutable std::shared_mutex breakpoints_mutex_; // OPTIMIZATION: Reader-writer lock for concurrent reads
    std::atomic<size_t> active_breakpoint_count_{0}; // Optimization: Atomic check

    // PHASE 4 LOCK-FREE HOT PATH: Eliminate shared_lock overhead with thread-local cache
    // This implements double-checked locking: version check (relaxed) → cache hit (zero locks!)
    std::atomic<uint32_t> breakpoint_version_{0};  // Incremented on any breakpoint modification

    // Thread-local cache: Each thread maintains its own copy of enabled breakpoints
    // Updated only when breakpoint_version_ changes (rarely)
    static thread_local uint32_t t_local_version_;
    static thread_local std::unordered_map<uint32_t, Breakpoint> t_local_breakpoints_;

    // OPTIMIZATION 5: Page bitmap fast filter to eliminate most hash lookups
    // 4KB pages (1 << 12) - single byte per page to indicate presence of breakpoints
    static constexpr uint32_t PAGE_SHIFT = 12;
    static constexpr uint32_t PAGE_SIZE = 1u << PAGE_SHIFT;
    std::vector<uint8_t> breakpoint_pages_;  // 0/1 flags per page

    // Event system integration
    EventManager* event_manager_;
    
    // Callbacks
    std::function<void(uint32_t)> breakpoint_hit_callback_;
    std::function<void()> pause_callback_;
    std::function<void()> resume_callback_;
    
    // Text encoding support
    uint16_t current_codepage_ = 932; // Default to Shift JIS
    bool japanese_mode_ = false;
    
    // Address tracking for debugger integration
    uint32_t current_address_ = 0;
    uint32_t follow_address_ = 0;
    uint32_t last_paused_address_ = 0;
    BreakReason last_break_reason_ = BreakReason::Unknown;
    uint32_t ignore_breakpoint_once_ = 0;
    bool has_ignore_breakpoint_once_ = false;
    bool skip_was_used_this_instruction_ = false;  // Tracks if skip was already used for current instruction

    // Listener management
    std::set<CoreDebuggerListener*> listeners_;
    mutable std::recursive_mutex listener_mutex_;
    
    // Internal helper methods
    bool isCallInstruction(uint32_t address);
    bool isRetInstruction(uint32_t address);
    uint32_t getNextInstructionAddress(uint32_t address);
    bool isValidSegmentValue(uint16_t seg, bool is_protected_mode);
    bool isLikelyCodeSegment(uint16_t seg, uint32_t ip, bool is_protected_mode);
    uint32_t getSegmentBase(uint16_t selector);
    std::string resolveSymbolName(uint32_t address) const;

    // Performance optimization: Rebuild page bitmap when breakpoints change
    void rebuildBreakpointPagesLocked();
    
    // Encoding helper methods
    std::string sjisToUTF8(const uint8_t* sjis_data, size_t length);
    std::vector<uint8_t> utf8ToSJIS(const std::string& utf8_text);
    std::string cp932ToUTF8(const uint8_t* cp932_data, size_t length);
    std::vector<uint8_t> utf8ToCP932(const std::string& utf8_text);
    
    // DOS codepage encoding helper methods
    std::string cp437ToUTF8(const uint8_t* cp437_data, size_t length);
    std::vector<uint8_t> utf8ToCP437(const std::string& utf8_text);
    std::string cp850ToUTF8(const uint8_t* cp850_data, size_t length);
    std::vector<uint8_t> utf8ToCP850(const std::string& utf8_text);
    
    // Analysis helper methods
    bool evaluateBreakpointCondition(const std::string& condition, uint32_t address);
    void parseDisassemblyText(const std::string& full_text, std::string& mnemonic, std::string& operands);
    
    // Japanese text encoding helper methods
    uint16_t sjisToUnicode(uint8_t byte1, uint8_t byte2);
    std::pair<uint8_t, uint8_t> unicodeToSJIS(uint16_t unicode);
    bool isValidSJISSequence(uint8_t byte1, uint8_t byte2);
    
    // Debugger state synchronization
    void processStepCommands();
    void checkBreakpoints(uint32_t address);
    void notifyPaused(BreakReason reason, uint32_t address);
    void notifyResumed();
    void notifyStepComplete(uint32_t address);
    void notifyBreakpointHit(uint32_t address, BreakReason reason);
    void notifyBreakpointListChanged();

    // Trace-based call stack tracking
    std::vector<TracedCallEntry> traced_call_stack_;
    mutable std::mutex call_stack_mutex_;
    bool trace_call_stack_enabled_ = false;
    
public:
    DosBoxCoreDebugger();
    virtual ~DosBoxCoreDebugger();
    
    // Initialize with event manager
    void initialize(EventManager* event_mgr);
    
    // Core instruction hook - called by CPU loop
    void onInstructionExecuted(uint32_t address);
    
    // CPU State Management
    std::map<std::string, uint32_t> getCpuRegisters() override;

    // OPTIMIZATION: Fast register snapshot (zero allocations, cache-friendly)
    void getCpuRegistersFast(LuaEngineTraceLogger::FastRegisterSnapshot& snapshot) override;

    // ponytail: PR6 — bulk register restore for reverse-step
    void setCpuRegistersFast(const LuaEngineTraceLogger::FastRegisterSnapshot& snapshot) override;

    CPUState getCpuState() override;
    void setCpuRegister(const std::string& name, uint32_t value) override;
    uint32_t getCurrentEIP() override;
    uint16_t getCurrentCS() override;
    
    // Additional address methods for debugger UI
    uint32_t getCurrentAddress() const;
    uint32_t getFollowAddress() const { return follow_address_; }

    // Address navigation for debugger UI
    void goToAddress(uint32_t address);

    // Public method to sync with DOSBox debugger state
    void syncWithExistingDebugger();
    
    // Execution Control
    void pause(BreakReason reason = BreakReason::Manual) override;
    void resume() override;
    void stepInto() override;
    void stepOver() override;
    void stepOut() override;
    void reset() override;
    void runToCursor(uint32_t address) override;
    bool isPaused() const override { return is_paused_.load(); }
    
    // Breakpoint Management
    void addBreakpoint(uint32_t address) override;
    void addBreakpoint(const Breakpoint& bp) override;
    void removeBreakpoint(uint32_t address) override;
    void toggleBreakpoint(uint32_t address) override;
    void enableBreakpoint(uint32_t address, bool enabled) override;
    void clearAllBreakpoints() override;
    std::vector<Breakpoint> getBreakpoints() const override;
    bool hasBreakpoint(uint32_t address) const override;
    
    // Disassembly
    DisassemblyLine disassemble(uint32_t address) override;
    std::vector<DisassemblyLine> disassembleRange(uint32_t start_address, int count) override;
    std::string disassembleInstruction(uint32_t address) override;
    std::string getInstructionBytes(uint32_t address) override;
    std::vector<uint8_t> getInstructionBytesVector(uint32_t address) override;
    
    // Call Stack
    std::vector<CallStackFrame> getCallStack() override;
    std::vector<CallStackFrame> getCallStackCombined();
    void enableCallStackTracing(bool enable);
    void onCallInstruction(uint32_t call_addr, uint32_t target_addr, bool is_far);
    void onReturnInstruction(uint32_t ret_addr);
    std::vector<CallStackFrame> getTracedCallStack() const;
    void clearTracedCallStack();
    void dumpStackFrameDebugInfo();
    
    // Memory Access
    uint8_t readMemoryByte(uint32_t address) override;
    uint16_t readMemoryWord(uint32_t address) override;
    uint32_t readMemoryDWord(uint32_t address) override;
    void writeMemoryByte(uint32_t address, uint8_t value) override;
    void writeMemoryWord(uint32_t address, uint16_t value) override;
    void writeMemoryDWord(uint32_t address, uint32_t value) override;
    uint8_t readByte(uint32_t address) override;
    uint16_t readWord(uint32_t address) override;
    uint32_t readDWord(uint32_t address) override;
    
    // Bulk memory operations for performance
    void readMemoryBlock(uint32_t address, uint8_t* buffer, size_t size) override;
    void writeMemoryBlock(uint32_t address, const uint8_t* buffer, size_t size) override;
    
    // PR4: Bulk memory read returning vector
    std::vector<uint8_t> readMemoryRange(uint32_t address, size_t length) override;
    
    // Text encoding support for Japanese and other languages
    std::string convertToUTF8(const uint8_t* data, size_t length, uint16_t codepage = 932) override;
    std::vector<uint8_t> convertFromUTF8(const std::string& utf8_text, uint16_t codepage = 932) override;
    std::string readTextFromMemory(uint32_t address, size_t length, uint16_t codepage = 932) override;
    void writeTextToMemory(uint32_t address, const std::string& text, uint16_t codepage = 932) override;
    uint16_t getCurrentCodepage() override;
    void setCurrentCodepage(uint16_t codepage) override;
    
    // Event callbacks
    void setBreakpointHitCallback(std::function<void(uint32_t)> callback) override;
    void setPauseCallback(std::function<void()> callback) override;
    void setResumeCallback(std::function<void()> callback) override;
    void addListener(CoreDebuggerListener* listener) override;
    void removeListener(CoreDebuggerListener* listener) override;
};

// Global instance
extern DosBoxCoreDebugger* g_core_debugger;

} // namespace LuaEngineDebug

// Forward declaration — defined in debugger_session.h
// Must be outside LuaEngineDebug namespace to match global scope in debugger_session.cpp
namespace LuaEngineDebugTools { class DebuggerSession; }
// ponytail: actual definition is unique_ptr in debugger_session.h — raw pointer here caused conflicting declaration
// extern LuaEngineDebugTools::DebuggerSession* g_debugger_session;

#endif // CORE_DEBUG_INTERFACE_H
