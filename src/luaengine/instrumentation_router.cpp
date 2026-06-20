/*
 *  Copyright (C) 2026  The DOSBox-X Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#if C_LUA

#include "instrumentation_router.h"

#include "dosbox.h"
#include "cpu.h"
#include "regs.h"
#include "paging.h"
#include "debug.h"
#include "../debug/debug_inc.h"
#include "pc98_cdl.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

// ============================================================================
// Global definitions
// ============================================================================
InstrumentationRouter* g_instrumentation = nullptr;
std::atomic<uint32_t> g_instrumentation_features{0};

// ============================================================================
// DebugAddress implementation
// ============================================================================

DebugAddress DebugAddress::fromCSIP() {
	uint16_t cs = SegValue(cs);
	uint32_t ip = reg_eip;
	return {cs, ip, (uint32_t(cs) << 4) + ip, 0};
}

uint32_t DebugAddress::getPhysical() const {
	if (physical == 0 && linear != 0) {
		physical = PAGING_GetPhysicalAddress(linear);
	}
	return physical;
}

// ============================================================================
// RAII Handle implementations
// ============================================================================

BreakpointHandle::BreakpointHandle(uint32_t id, InstrumentationRouter* router)
	: id_(id), router_(router) {}

BreakpointHandle::~BreakpointHandle() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeBreakpoint(id_);
	}
}

void BreakpointHandle::reset() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeBreakpoint(id_);
	}
	id_ = 0;
	router_ = nullptr;
}

MemoryTapHandle::MemoryTapHandle(uint32_t id, InstrumentationRouter* router)
	: id_(id), router_(router) {}

MemoryTapHandle::~MemoryTapHandle() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeTap(id_);
	}
}

void MemoryTapHandle::reset() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeTap(id_);
	}
	id_ = 0;
	router_ = nullptr;
}

WatchpointHandle::WatchpointHandle(uint32_t id, InstrumentationRouter* router)
	: id_(id), router_(router) {}

WatchpointHandle::~WatchpointHandle() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeWatchpoint(id_);
	}
}

void WatchpointHandle::reset() {
	if (id_ != 0 && router_ != nullptr) {
		router_->removeWatchpoint(id_);
	}
	id_ = 0;
	router_ = nullptr;
}

// ============================================================================
// InstrumentationRouter — constructor / destructor
// ============================================================================

InstrumentationRouter::InstrumentationRouter() {
	g_instrumentation = this;
	g_instrumentation_features.store(0, std::memory_order_relaxed);
}

InstrumentationRouter::~InstrumentationRouter() {
	g_instrumentation_features.store(0, std::memory_order_relaxed);
	g_instrumentation = nullptr;
}

// ============================================================================
// onInstruction — hot-path dispatch from CPU core loops
// Returns true if a breakpoint was hit (caller should return to debugger)
// ============================================================================

bool InstrumentationRouter::onInstruction(DebugAddress addr, uint32_t feature_mask) {
	bool should_break = false;

	// Step control — decrement counter, break when done
	if (feature_mask & INSTR_STEP_CONTROL) {
		if (stepping_ && step_count_ > 0) {
			step_count_--;
			if (step_count_ <= 0) {
				stepping_ = false;
				should_break = true;
			}
		}
	}

	// Execution breakpoints — check both CBreakpoint and router's own entries
	if (feature_mask & INSTR_EXECUTION_BREAK) {
		// Legacy CBreakpoint system — backward compatible (returns bool)
		if (!CBreakpoint::BPoints.empty() &&
			CBreakpoint::CheckBreakpoint(addr.segment, addr.offset)) {
			should_break = true;
		}

		// Router's own breakpoint entries with compiled conditions
		if (!should_break) {
			for (const auto& bp : breakpoints_) {
				if (bp.active && bp.linear_addr == addr.linear) {
					// Check compiled condition if present
					if (!bp.condition.empty()) {
						if (!evaluateCondition(bp.condition)) continue;
					}
					should_break = true;
					break;
				}
			}
		}
	}

	// Execution trace — fire callbacks, don't break
	if (feature_mask & INSTR_EXECUTION_TRACE) {
		for (const auto& tc : trace_callbacks_) {
			tc.callback(addr);
		}
	}

	// Call stack tracking — placeholder for future PR
	// if (feature_mask & INSTR_CALL_STACK) { ... }

	return should_break;
}

// ============================================================================
// onMemoryWrite — hot-path dispatch from paging.h inline memory writes
// 3-level filter: feature mask (already checked) → page bitmap → range scan
// ============================================================================

void InstrumentationRouter::onMemoryWrite(uint32_t linear_addr, uint32_t value,
										   uint8_t size, uint32_t feature_mask) {
	// Level 2: page bitmap check
	uint32_t page = linear_addr >> 12;
	if (!hasTapsOnPage(page)) return;

	// Level 3: scan matching tap ranges
	if (feature_mask & INSTR_MEMORY_WRITE_TAP) {
		for (const auto& tap : write_taps_) {
			if (tap.active &&
				linear_addr >= tap.start_addr &&
				linear_addr + size - 1 <= tap.end_addr) {
				tap.callback(linear_addr, value, size, true);
			}
		}
	}

	// ponytail: PR6 — CDL recording on guest memory write
	if (PC98CDL::GetCDL().active()) {
		DebugAddress addr = DebugAddress::fromCSIP();
		uint32_t writer_pc = (static_cast<uint32_t>(addr.segment) << 4) + addr.offset;
		PC98CDL::GetCDL().recordDataWrite(linear_addr, size, writer_pc);
	}

	// Watchpoint check
	if (feature_mask & INSTR_MEMORY_WATCHPOINT) {
		for (const auto& wp : watchpoints_) {
			if (wp.active &&
				linear_addr >= wp.linear_addr &&
				linear_addr < wp.linear_addr + wp.size) {
				wp.callback(linear_addr, value, size, true);
			}
		}
	}
}

// ============================================================================
// onMemoryRead — hot-path dispatch from paging.h inline memory reads
// PR3-005: read tap mirror of onMemoryWrite
// ============================================================================

void InstrumentationRouter::onMemoryRead(uint32_t linear_addr, uint32_t value,
										  uint8_t size, uint32_t feature_mask) {
	// Level 2: page bitmap check
	uint32_t page = linear_addr >> 12;
	if (!hasTapsOnPage(page)) return;

	// Level 3: scan matching read tap ranges
	if (feature_mask & INSTR_MEMORY_READ_TAP) {
		for (const auto& tap : read_taps_) {
			if (tap.active &&
				linear_addr >= tap.start_addr &&
				linear_addr + size - 1 <= tap.end_addr) {
				tap.callback(linear_addr, value, size, false);
			}
		}
	}

	// ponytail: PR6 — CDL recording on guest memory read
	if (PC98CDL::GetCDL().active()) {
		PC98CDL::GetCDL().recordDataRead(linear_addr, size);
	}
}

// ============================================================================
// onFrameBoundary — dispatch frame start/end hooks in registration order
// ============================================================================

void InstrumentationRouter::onFrameBoundary(bool is_start) {
	for (const auto& hook : frame_hooks_) {
		hook.callback(is_start);
	}
}

// ============================================================================
// Registration methods — return RAII handles
// ============================================================================

BreakpointHandle InstrumentationRouter::addBreakpoint(uint32_t linear_addr,
													   const std::string& condition) {
	uint32_t id = next_id_++;
	BreakpointEntry entry;
	entry.id = id;
	entry.linear_addr = linear_addr;
	entry.active = true;

	if (!condition.empty()) {
		entry.condition = compileCondition(condition);
	}

	breakpoints_.push_back(std::move(entry));
	updateFeatureMask();
	return BreakpointHandle(id, this);
}

MemoryTapHandle InstrumentationRouter::addMemoryTap(uint32_t start, uint32_t end,
													 bool is_write, MemoryTapCallback cb) {
	uint32_t id = next_id_++;
	TapEntry entry;
	entry.id = id;
	entry.start_addr = start;
	entry.end_addr = end;
	entry.is_write = is_write;
	entry.callback = std::move(cb);
	entry.active = true;

	auto& taps = is_write ? write_taps_ : read_taps_;
	taps.push_back(std::move(entry));

	// Mark pages in bitmap
	uint32_t start_page = start >> 12;
	uint32_t end_page = end >> 12;
	if (tap_pages_.empty()) {
		// ponytail: lazy allocation — 1M entries covers 4GB with 4K pages
		tap_pages_.resize(1048576, 0);
	}
	for (uint32_t p = start_page; p <= end_page; p++) {
		tap_pages_[p] = 1;
	}

	updateFeatureMask();
	return MemoryTapHandle(id, this);
}

WatchpointHandle InstrumentationRouter::addWatchpoint(uint32_t linear_addr, uint8_t size,
													  MemoryTapCallback cb) {
	uint32_t id = next_id_++;
	WatchpointEntry entry;
	entry.id = id;
	entry.linear_addr = linear_addr;
	entry.size = size;
	entry.callback = std::move(cb);
	entry.active = true;

	watchpoints_.push_back(std::move(entry));

	// Mark page in bitmap
	uint32_t page = linear_addr >> 12;
	if (tap_pages_.empty()) {
		tap_pages_.resize(1048576, 0);
	}
	tap_pages_[page] = 1;

	updateFeatureMask();
	return WatchpointHandle(id, this);
}

uint32_t InstrumentationRouter::addFrameHook(FrameHook cb) {
	uint32_t id = next_id_++;
	frame_hooks_.push_back({id, std::move(cb)});
	updateFeatureMask();
	return id;
}

void InstrumentationRouter::removeFrameHook(uint32_t id) {
	auto it = std::find_if(frame_hooks_.begin(), frame_hooks_.end(),
		[id](const FrameHookEntry& e) { return e.id == id; });
	if (it != frame_hooks_.end()) {
		frame_hooks_.erase(it);
		updateFeatureMask();
	}
}

uint32_t InstrumentationRouter::addTraceCallback(TraceCallback cb) {
	uint32_t id = next_id_++;
	trace_callbacks_.push_back({id, std::move(cb)});
	updateFeatureMask();
	return id;
}

void InstrumentationRouter::removeTraceCallback(uint32_t id) {
	auto it = std::find_if(trace_callbacks_.begin(), trace_callbacks_.end(),
		[id](const TraceEntry& e) { return e.id == id; });
	if (it != trace_callbacks_.end()) {
		trace_callbacks_.erase(it);
		updateFeatureMask();
	}
}

// ============================================================================
// Internal removal — called by RAII handles
// ============================================================================

void InstrumentationRouter::removeBreakpoint(uint32_t id) {
	auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
		[id](const BreakpointEntry& e) { return e.id == id; });
	if (it != breakpoints_.end()) {
		breakpoints_.erase(it);
		updateFeatureMask();
	}
}

void InstrumentationRouter::removeTap(uint32_t id) {
	// Try write taps first
	auto wit = std::find_if(write_taps_.begin(), write_taps_.end(),
		[id](const TapEntry& e) { return e.id == id; });
	if (wit != write_taps_.end()) {
		write_taps_.erase(wit);
		rebuildPageBitmap();
		updateFeatureMask();
		return;
	}

	// Then read taps
	auto rit = std::find_if(read_taps_.begin(), read_taps_.end(),
		[id](const TapEntry& e) { return e.id == id; });
	if (rit != read_taps_.end()) {
		read_taps_.erase(rit);
		rebuildPageBitmap();
		updateFeatureMask();
		return;
	}
}

void InstrumentationRouter::removeWatchpoint(uint32_t id) {
	auto it = std::find_if(watchpoints_.begin(), watchpoints_.end(),
		[id](const WatchpointEntry& e) { return e.id == id; });
	if (it != watchpoints_.end()) {
		watchpoints_.erase(it);
		rebuildPageBitmap();
		updateFeatureMask();
	}
}

// ============================================================================
// Page bitmap management
// ============================================================================

bool InstrumentationRouter::hasTapsOnPage(uint32_t page_index) const {
	if (tap_pages_.empty()) return false;
	if (page_index >= tap_pages_.size()) return false;
	return tap_pages_[page_index] != 0;
}

void InstrumentationRouter::rebuildPageBitmap() {
	// Clear bitmap
	if (tap_pages_.empty()) return;
	std::fill(tap_pages_.begin(), tap_pages_.end(), 0);

	// Rebuild from write taps
	for (const auto& tap : write_taps_) {
		if (!tap.active) continue;
		uint32_t start_page = tap.start_addr >> 12;
		uint32_t end_page = tap.end_addr >> 12;
		for (uint32_t p = start_page; p <= end_page && p < tap_pages_.size(); p++) {
			tap_pages_[p] = 1;
		}
	}

	// Rebuild from read taps
	for (const auto& tap : read_taps_) {
		if (!tap.active) continue;
		uint32_t start_page = tap.start_addr >> 12;
		uint32_t end_page = tap.end_addr >> 12;
		for (uint32_t p = start_page; p <= end_page && p < tap_pages_.size(); p++) {
			tap_pages_[p] = 1;
		}
	}

	// Rebuild from watchpoints
	for (const auto& wp : watchpoints_) {
		if (!wp.active) continue;
		uint32_t page = wp.linear_addr >> 12;
		if (page < tap_pages_.size()) {
			tap_pages_[page] = 1;
		}
	}
}

// ============================================================================
// Feature mask management
// ============================================================================

void InstrumentationRouter::updateFeatureMask() {
	uint32_t mask = 0;

	if (!breakpoints_.empty()) mask |= INSTR_EXECUTION_BREAK;
	if (!write_taps_.empty())  mask |= INSTR_MEMORY_WRITE_TAP;
	if (!read_taps_.empty())   mask |= INSTR_MEMORY_READ_TAP;
	if (!watchpoints_.empty()) mask |= INSTR_MEMORY_WATCHPOINT;
	if (!frame_hooks_.empty()) mask |= INSTR_FRAME_BOUNDARY;
	if (!trace_callbacks_.empty()) mask |= INSTR_EXECUTION_TRACE;
	if (stepping_) mask |= INSTR_STEP_CONTROL;

	// CONDITION_EVAL is set when any breakpoint has a compiled condition
	if (mask & INSTR_EXECUTION_BREAK) {
		for (const auto& bp : breakpoints_) {
			if (!bp.condition.empty()) {
				mask |= INSTR_CONDITION_EVAL;
				break;
			}
		}
	}

	g_instrumentation_features.store(mask, std::memory_order_relaxed);
}

// ============================================================================
// Step control
// ============================================================================

void InstrumentationRouter::enableStepping(int32_t count) {
	stepping_ = true;
	step_count_ = count;
	updateFeatureMask();
}

void InstrumentationRouter::disableStepping() {
	stepping_ = false;
	step_count_ = 0;
	updateFeatureMask();
}

// ============================================================================
// Condition compiler — recursive-descent parser → flat stack-machine bytecode
// Grammar:
//   expr     := or_expr
//   or_expr  := and_expr ('||' and_expr)*
//   and_expr := cmp_expr ('&&' cmp_expr)*
//   cmp_expr := primary (cmp_op primary)?
//   primary  := '!' primary | '(' expr ')' | register | integer
// ============================================================================

class ConditionParser {
public:
	ConditionParser(const std::string& expr) : src_(expr), pos_(0) {}

	std::vector<ConditionValue> parse() {
		std::vector<ConditionValue> result;
		parseOrExpr(result);
		skipWhitespace();
		if (pos_ < src_.size()) {
			// Unexpected trailing chars — still return what we parsed
		}
		return result;
	}

private:
	std::string src_;
	size_t pos_;

	void skipWhitespace() {
		while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
			pos_++;
		}
	}

	char peek() {
		skipWhitespace();
		return pos_ < src_.size() ? src_[pos_] : '\0';
	}

	char advance() {
		return pos_ < src_.size() ? src_[pos_++] : '\0';
	}

	bool match(const char* s) {
		skipWhitespace();
		size_t len = std::strlen(s);
		if (pos_ + len <= src_.size() && src_.compare(pos_, len, s) == 0) {
			pos_ += len;
			return true;
		}
		return false;
	}

	void parseOrExpr(std::vector<ConditionValue>& out) {
		parseAndExpr(out);
		while (match("||")) {
			parseAndExpr(out);
			out.push_back({ConditionOp::OR, 0});
		}
	}

	void parseAndExpr(std::vector<ConditionValue>& out) {
		parseCmpExpr(out);
		while (match("&&")) {
			parseCmpExpr(out);
			out.push_back({ConditionOp::AND, 0});
		}
	}

	void parseCmpExpr(std::vector<ConditionValue>& out) {
		parsePrimary(out);
		skipWhitespace();

		ConditionOp op;
		if      (match("==")) op = ConditionOp::CMP_EQ;
		else if (match("!=")) op = ConditionOp::CMP_NE;
		else if (match(">=")) op = ConditionOp::CMP_GE;
		else if (match("<=")) op = ConditionOp::CMP_LE;
		else if (match(">"))  op = ConditionOp::CMP_GT;
		else if (match("<"))  op = ConditionOp::CMP_LT;
		else return;  // no comparison operator

		parsePrimary(out);
		out.push_back({op, 0});
	}

	void parsePrimary(std::vector<ConditionValue>& out) {
		skipWhitespace();

		// Unary NOT
		if (match("!")) {
			parsePrimary(out);
			out.push_back({ConditionOp::NOT, 0});
			return;
		}

		// Parenthesized expression
		if (match("(")) {
			parseOrExpr(out);
			match(")");  // consume closing paren
			return;
		}

		// Register name or integer
		if (pos_ < src_.size() && (std::isalpha(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
			parseRegister(out);
			return;
		}

		// Integer literal
		parseInteger(out);
	}

	void parseRegister(std::vector<ConditionValue>& out) {
		size_t start = pos_;
		while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
			pos_++;
		}
		std::string name = src_.substr(start, pos_ - start);

		// Case-insensitive match
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return std::tolower(c); });

		ConditionOp op;
		if      (lower == "eax")     op = ConditionOp::LOAD_REG_EAX;
		else if (lower == "ebx")     op = ConditionOp::LOAD_REG_EBX;
		else if (lower == "ecx")     op = ConditionOp::LOAD_REG_ECX;
		else if (lower == "edx")     op = ConditionOp::LOAD_REG_EDX;
		else if (lower == "esi")     op = ConditionOp::LOAD_REG_ESI;
		else if (lower == "edi")     op = ConditionOp::LOAD_REG_EDI;
		else if (lower == "ebp")     op = ConditionOp::LOAD_REG_EBP;
		else if (lower == "esp")     op = ConditionOp::LOAD_REG_ESP;
		else if (lower == "cs")      op = ConditionOp::LOAD_REG_CS;
		else if (lower == "ds")      op = ConditionOp::LOAD_REG_DS;
		else if (lower == "es")      op = ConditionOp::LOAD_REG_ES;
		else if (lower == "fs")      op = ConditionOp::LOAD_REG_FS;
		else if (lower == "gs")      op = ConditionOp::LOAD_REG_GS;
		else if (lower == "ss")      op = ConditionOp::LOAD_REG_SS;
		else if (lower == "eip")     op = ConditionOp::LOAD_REG_EIP;
		else if (lower == "eflags" || lower == "flags") op = ConditionOp::LOAD_REG_EFLAGS;
		else {
			// Unknown register — treat as zero
			op = ConditionOp::LOAD_IMM;
			out.push_back({op, 0});
			return;
		}
		out.push_back({op, 0});
	}

	void parseInteger(std::vector<ConditionValue>& out) {
		skipWhitespace();
		uint32_t val = 0;

		if (pos_ + 1 < src_.size() && src_[pos_] == '0' &&
			(src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
			// Hex literal
			pos_ += 2;
			while (pos_ < src_.size() && std::isxdigit(static_cast<unsigned char>(src_[pos_]))) {
				char c = src_[pos_];
				val = val * 16 + (std::isdigit(static_cast<unsigned char>(c))
					? c - '0'
					: std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
				pos_++;
			}
		} else {
			// Decimal literal
			while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
				val = val * 10 + (src_[pos_] - '0');
				pos_++;
			}
		}

		out.push_back({ConditionOp::LOAD_IMM, val});
	}
};

std::vector<ConditionValue> InstrumentationRouter::compileCondition(const std::string& expr) {
	ConditionParser parser(expr);
	return parser.parse();
}

// ============================================================================
// Condition evaluator — stack machine
// ============================================================================

bool InstrumentationRouter::evaluateCondition(const std::vector<ConditionValue>& bytecode) {
	if (bytecode.empty()) return true;

	uint32_t stack[64];  // ponytail: fixed 64-entry stack, enough for any practical condition
	int sp = 0;

	for (const auto& cv : bytecode) {
		switch (cv.op) {
		// Register loads
		case ConditionOp::LOAD_REG_EAX:    stack[sp++] = reg_eax; break;
		case ConditionOp::LOAD_REG_EBX:    stack[sp++] = reg_ebx; break;
		case ConditionOp::LOAD_REG_ECX:    stack[sp++] = reg_ecx; break;
		case ConditionOp::LOAD_REG_EDX:    stack[sp++] = reg_edx; break;
		case ConditionOp::LOAD_REG_ESI:    stack[sp++] = reg_esi; break;
		case ConditionOp::LOAD_REG_EDI:    stack[sp++] = reg_edi; break;
		case ConditionOp::LOAD_REG_EBP:    stack[sp++] = reg_ebp; break;
		case ConditionOp::LOAD_REG_ESP:    stack[sp++] = reg_esp; break;
		case ConditionOp::LOAD_REG_CS:     stack[sp++] = SegValue(cs); break;
		case ConditionOp::LOAD_REG_DS:     stack[sp++] = SegValue(ds); break;
		case ConditionOp::LOAD_REG_ES:     stack[sp++] = SegValue(es); break;
		case ConditionOp::LOAD_REG_FS:     stack[sp++] = SegValue(fs); break;
		case ConditionOp::LOAD_REG_GS:     stack[sp++] = SegValue(gs); break;
		case ConditionOp::LOAD_REG_SS:     stack[sp++] = SegValue(ss); break;
		case ConditionOp::LOAD_REG_EIP:    stack[sp++] = reg_eip; break;
		case ConditionOp::LOAD_REG_EFLAGS: stack[sp++] = reg_flags; break;

		// Immediate
		case ConditionOp::LOAD_IMM: stack[sp++] = cv.imm; break;

		// Comparisons — pop b, pop a, push (a op b)? 1 : 0
		case ConditionOp::CMP_EQ: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a == b) ? 1u : 0u; } break;
		case ConditionOp::CMP_NE: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a != b) ? 1u : 0u; } break;
		case ConditionOp::CMP_GT: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a > b)  ? 1u : 0u; } break;
		case ConditionOp::CMP_GE: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a >= b) ? 1u : 0u; } break;
		case ConditionOp::CMP_LT: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a < b)  ? 1u : 0u; } break;
		case ConditionOp::CMP_LE: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a <= b) ? 1u : 0u; } break;

		// Logical — pop b, pop a, push result
		case ConditionOp::AND: if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a && b) ? 1u : 0u; } break;
		case ConditionOp::OR:  if (sp >= 2) { auto b = stack[--sp]; auto a = stack[--sp]; stack[sp++] = (a || b) ? 1u : 0u; } break;
		case ConditionOp::NOT: if (sp >= 1) { auto a = stack[--sp]; stack[sp++] = (!a) ? 1u : 0u; } break;
		}

		// Stack overflow guard
		if (sp >= 64) sp = 63;
	}

	return sp > 0 && stack[sp - 1] != 0;
}

#endif // C_LUA
