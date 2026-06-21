#ifndef INSTRUMENTATION_ROUTER_H
#define INSTRUMENTATION_ROUTER_H

#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

// ponytail: Bitu defined in config.h — must be available before FillFlags decl
#ifndef HAVE_CONFIG_H
typedef uintptr_t Bitu;
#endif

#if C_LUA

// ponytail: forward decl — FillFlags is in lazyflags.h, can't include due to circular dep
extern Bitu FillFlags();

// ============================================================================
// InstrumentationFeature — bit flags for the atomic feature mask
// Checked with memory_order_relaxed on CPU hot path
// ============================================================================
enum InstrumentationFeature : uint32_t {
	INSTR_EXECUTION_BREAK   = 1u << 0,  // Breakpoints on instruction execution
	INSTR_EXECUTION_TRACE   = 1u << 1,  // Instruction tracing
	INSTR_MEMORY_READ_TAP   = 1u << 2,  // Tap on guest memory reads
	INSTR_MEMORY_WRITE_TAP  = 1u << 3,  // Tap on guest memory writes
	INSTR_MEMORY_WATCHPOINT = 1u << 4,  // Exact address watchpoints
	INSTR_FRAME_BOUNDARY    = 1u << 5,  // Frame start/end hooks
	INSTR_CALL_STACK        = 1u << 6,  // Call/return tracking
	INSTR_STEP_CONTROL      = 1u << 7,  // Step-into/over/out
	INSTR_CONDITION_EVAL    = 1u << 8,  // Breakpoint condition evaluation
	INSTR_CDL_EXECUTION     = 1u << 9,  // CDL instruction fetch tracking
	INSTR_CDL_WRITE         = 1u << 10, // CDL guest memory write tracking
	INSTR_CDL_READ          = 1u << 11, // CDL guest memory read tracking
};

// Exact-instruction feature mask — when set, dynamic core must force normal
constexpr uint32_t INSTR_EXACT_FEATURES =
    INSTR_EXECUTION_BREAK | INSTR_EXECUTION_TRACE |
    INSTR_STEP_CONTROL | INSTR_CDL_EXECUTION;

// Exact-memory feature mask — bypasses EventManager throttling
constexpr uint32_t INSTR_EXACT_MEMORY =
    INSTR_MEMORY_WATCHPOINT | INSTR_CDL_WRITE | INSTR_CDL_READ;

// ============================================================================
// DebugAddress — segment:offset, linear, lazy physical
// ============================================================================
struct DebugAddress {
	uint16_t segment  = 0;
	uint32_t offset   = 0;
	uint32_t linear   = 0;
	mutable uint32_t physical = 0;

	static DebugAddress fromCSIP();
	uint32_t getPhysical() const;
};

// ============================================================================
// Condition compiler — stack-machine bytecode for breakpoint conditions
// ============================================================================
enum class ConditionOp : uint8_t {
	LOAD_REG_EAX, LOAD_REG_EBX, LOAD_REG_ECX, LOAD_REG_EDX,
	LOAD_REG_ESI, LOAD_REG_EDI, LOAD_REG_EBP, LOAD_REG_ESP,
	LOAD_REG_CS,  LOAD_REG_DS,  LOAD_REG_ES,  LOAD_REG_FS,
	LOAD_REG_GS,  LOAD_REG_SS,  LOAD_REG_EIP, LOAD_REG_EFLAGS,
	LOAD_IMM,
	CMP_EQ, CMP_NE, CMP_GT, CMP_GE, CMP_LT, CMP_LE,
	AND, OR, NOT,
};

struct ConditionValue {
	ConditionOp op;
	uint32_t imm = 0;  // used with LOAD_IMM
};

// ============================================================================
// RAII Handle types — move-only, auto-deregister on destruction
// ponytail: single-threaded — CPU and Lua share one thread, no mutex needed
// ============================================================================

class InstrumentationRouter;

class BreakpointHandle {
public:
	BreakpointHandle() = default;
	BreakpointHandle(uint32_t id, InstrumentationRouter* router);
	~BreakpointHandle();

	BreakpointHandle(BreakpointHandle&& o) noexcept
		: id_(o.id_), router_(o.router_) {
		o.id_ = 0; o.router_ = nullptr;
	}
	BreakpointHandle& operator=(BreakpointHandle&& o) noexcept {
		if (this != &o) { reset(); id_ = o.id_; router_ = o.router_; o.id_ = 0; o.router_ = nullptr; }
		return *this;
	}
	BreakpointHandle(const BreakpointHandle&) = delete;
	BreakpointHandle& operator=(const BreakpointHandle&) = delete;

	bool valid() const { return id_ != 0 && router_ != nullptr; }
	uint32_t id() const { return id_; }
	void reset();
private:
	uint32_t id_ = 0;
	InstrumentationRouter* router_ = nullptr;
};

class MemoryTapHandle {
public:
	MemoryTapHandle() = default;
	MemoryTapHandle(uint32_t id, InstrumentationRouter* router);
	~MemoryTapHandle();

	MemoryTapHandle(MemoryTapHandle&& o) noexcept
		: id_(o.id_), router_(o.router_) {
		o.id_ = 0; o.router_ = nullptr;
	}
	MemoryTapHandle& operator=(MemoryTapHandle&& o) noexcept {
		if (this != &o) { reset(); id_ = o.id_; router_ = o.router_; o.id_ = 0; o.router_ = nullptr; }
		return *this;
	}
	MemoryTapHandle(const MemoryTapHandle&) = delete;
	MemoryTapHandle& operator=(const MemoryTapHandle&) = delete;

	bool valid() const { return id_ != 0 && router_ != nullptr; }
	uint32_t id() const { return id_; }
	void reset();
private:
	uint32_t id_ = 0;
	InstrumentationRouter* router_ = nullptr;
};

class WatchpointHandle {
public:
	WatchpointHandle() = default;
	WatchpointHandle(uint32_t id, InstrumentationRouter* router);
	~WatchpointHandle();

	WatchpointHandle(WatchpointHandle&& o) noexcept
		: id_(o.id_), router_(o.router_) {
		o.id_ = 0; o.router_ = nullptr;
	}
	WatchpointHandle& operator=(WatchpointHandle&& o) noexcept {
		if (this != &o) { reset(); id_ = o.id_; router_ = o.router_; o.id_ = 0; o.router_ = nullptr; }
		return *this;
	}
	WatchpointHandle(const WatchpointHandle&) = delete;
	WatchpointHandle& operator=(const WatchpointHandle&) = delete;

	bool valid() const { return id_ != 0 && router_ != nullptr; }
	uint32_t id() const { return id_; }
	void reset();
private:
	uint32_t id_ = 0;
	InstrumentationRouter* router_ = nullptr;
};

// ============================================================================
// Callback types
// ============================================================================
using MemoryTapCallback = std::function<void(uint32_t linear_addr, uint32_t value, uint8_t size, bool is_write)>;
using FrameHook = std::function<void(bool is_start)>;
using TraceCallback = std::function<void(const DebugAddress& addr)>;

// ============================================================================
// InstrumentationRouter — singleton dispatcher
// ============================================================================
class InstrumentationRouter {
public:
	InstrumentationRouter();
	~InstrumentationRouter();

	// Hot-path dispatch — CPU core loops
	// Returns true if a breakpoint was hit (caller should return to debugger)
	bool onInstruction(DebugAddress addr, uint32_t feature_mask);

	// Hot-path dispatch — paging.h inline memory writes
	void onMemoryWrite(uint32_t linear_addr, uint32_t value, uint8_t size, uint32_t feature_mask);
	// Hot-path dispatch — paging.h inline memory reads (PR3-005 read tap)
	void onMemoryRead(uint32_t linear_addr, uint32_t value, uint8_t size, uint32_t feature_mask);

	// Frame boundary dispatch
	void onFrameBoundary(bool is_start);

	// Registration — returns RAII handles
	BreakpointHandle addBreakpoint(uint32_t linear_addr, const std::string& condition = "");
	MemoryTapHandle addMemoryTap(uint32_t start, uint32_t end, bool is_write, MemoryTapCallback cb);
	WatchpointHandle addWatchpoint(uint32_t linear_addr, uint8_t size, MemoryTapCallback cb);

	// Frame hook registration (no RAII — removed by ID)
	uint32_t addFrameHook(FrameHook cb);
	void removeFrameHook(uint32_t id);

	// Trace callback registration
	uint32_t addTraceCallback(TraceCallback cb);
	void removeTraceCallback(uint32_t id);

	// Internal removal — called by RAII handles
	void removeBreakpoint(uint32_t id);
	void removeTap(uint32_t id);
	void removeWatchpoint(uint32_t id);

	// Page bitmap check — called from paging.h hot path
	bool hasTapsOnPage(uint32_t page_index) const;

	// Feature mask management — recalculates from current state
	void updateFeatureMask();

	// Step control
	void enableStepping(int32_t count = 1);
	void disableStepping();

	// Condition compiler — parse expression into bytecode
	static std::vector<ConditionValue> compileCondition(const std::string& expr);

	// Condition evaluator — stack machine, O(1) per opcode
	static bool evaluateCondition(const std::vector<ConditionValue>& bytecode);

private:
	struct BreakpointEntry {
		uint32_t id;
		uint32_t linear_addr;
		std::vector<ConditionValue> condition;
		bool active;
	};

	struct TapEntry {
		uint32_t id;
		uint32_t start_addr;
		uint32_t end_addr;
		bool is_write;
		MemoryTapCallback callback;
		bool active;
	};

	struct WatchpointEntry {
		uint32_t id;
		uint32_t linear_addr;
		uint8_t size;
		MemoryTapCallback callback;
		bool active;
	};

	struct FrameHookEntry {
		uint32_t id;
		FrameHook callback;
	};

	struct TraceEntry {
		uint32_t id;
		TraceCallback callback;
	};

	uint32_t next_id_ = 1;

	// Breakpoint storage
	std::vector<BreakpointEntry> breakpoints_;

	// Memory tap storage
	std::vector<TapEntry> write_taps_;
	std::vector<TapEntry> read_taps_;

	// Watchpoint storage
	std::vector<WatchpointEntry> watchpoints_;

	// Frame hooks
	std::vector<FrameHookEntry> frame_hooks_;

	// Trace callbacks
	std::vector<TraceEntry> trace_callbacks_;

	// Page bitmap — lazy-allocated, indexed by addr >> 12
	// ponytail: lazy vector, up to 1M entries for 4GB/4K pages; allocate on first tap
	std::vector<uint8_t> tap_pages_;

	// Step control state
	bool stepping_ = false;
	int32_t step_count_ = 0;

	// Recompute page bitmap from tap entries
	void rebuildPageBitmap();
};

// ============================================================================
// Global singleton pointer — matches project convention (g_core_debugger, etc.)
// ============================================================================
extern InstrumentationRouter* g_instrumentation;

// Atomic feature mask — checked with memory_order_relaxed on CPU hot path
extern std::atomic<uint32_t> g_instrumentation_features;

// ============================================================================
// INSTRUMENT_CHECK — replaces DEBUG_HeavyIsBreakpoint() in CPU core loops
// Only active when C_LUA is defined
// ============================================================================
// PR3-003 fix: guard by C_LUA, not C_HEAVY_DEBUG — the router must be active
// in any --enable-lua build, regardless of heavy-debug mode
#if C_LUA
#define INSTRUMENT_CHECK() do { \
	auto _instr_f_ = g_instrumentation_features.load(std::memory_order_relaxed); \
	if (_instr_f_ && g_instrumentation && \
		g_instrumentation->onInstruction(DebugAddress::fromCSIP(), _instr_f_)) { \
		FillFlags(); \
		return (Bits)debugCallback; \
	} \
} while(0)
#else
#define INSTRUMENT_CHECK() ((void)0)
#endif

#endif // C_LUA

#endif // INSTRUMENTATION_ROUTER_H
