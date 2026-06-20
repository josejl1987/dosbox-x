/*
 *  Copyright (C) 2026  DOSBox-X authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  PC-98 Code/Data Logger runtime collector.
 */

#include "pc98_cdl.h"
#include "cpu.h"
#include "mem.h"
#include "debug.h"
#include "../debug/debug_inc.h"

// Performance tuning:
// Stack tracking reads memory and updates evidence for every instruction;
// disable it if CDL capture is too slow. Instruction fetch/code marking
// remains active.
#define PC98CDL_STACK_TRACKING 0

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace PC98CDL {

namespace {

constexpr char kMagic[7] = {'P', '9', '8', 'C', 'D', 'L', '\0'};
constexpr uint16_t kVersion = 1;

template <typename T>
inline void writeRaw(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
inline void readRaw(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
}

void writeString(std::ofstream& f, const std::string& s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    writeRaw(f, len);
    f.write(s.data(), len);
}

std::string readString(std::ifstream& f) {
    uint16_t len = 0;
    readRaw(f, len);
    if (!f || len == 0) return std::string();
    std::string s;
    s.resize(len);
    f.read(&s[0], len);
    return s;
}

} // anonymous namespace

// ------------------------------------------------------------------
// Module
// ------------------------------------------------------------------
Module::Module(const ModuleInfo& info)
    : id(info.id)
    , base(info.base)
    , size(info.size)
    , source_file(info.source_file)
    , source_offset(info.source_offset) {
    evidence.resize(size);
}

int64_t Module::relativeOffset(uint32_t linear) const {
    if (linear < base || linear >= base + size) return -1;
    return static_cast<int64_t>(linear - base);
}

void Module::markAccess(uint32_t offset, uint32_t flag) {
    if (offset >= size) return;
    evidence[offset].access |= flag;
}

void Module::markOrigin(uint32_t offset, uint16_t flag) {
    if (offset >= size) return;
    evidence[offset].origin |= flag;
}

void Module::markAnalysis(uint32_t offset, uint16_t flag) {
    if (offset >= size) return;
    evidence[offset].analysis |= flag;
}

void Module::addEntryPoint(uint32_t module_offset) {
    if (module_offset < size) entry_points.insert(module_offset);
}

Module::Stats Module::computeStats() const {
    Stats s;
    s.total = size;
    s.entries = static_cast<uint32_t>(entry_points.size());
    for (uint32_t i = 0; i < size; ++i) {
        const ByteEvidence& e = evidence[i];
        if (e.access & InsnStart) s.executed++;
        if (e.access & DataRead) s.data_read++;
        if (e.access & DataWrite) s.data_written++;
        if (e.access & StackRead) s.stack_read++;
        if (e.access & StackWrite) s.stack_written++;
        const bool any_access = (e.access & (InsnStart | InsnByte)) != 0;
        const bool any_data = (e.access & (DataRead | DataWrite | StackRead | StackWrite | CodePtrRead | DataPtrRead | StringSource | StringDest | BlockSource | BlockDest)) != 0;
        if (any_access && any_data) s.conflicts++;
        else if (e.access == 0 && e.origin == 0 && e.analysis == 0) s.unknown++;
    }
    return s;
}

// ------------------------------------------------------------------
// CDL
// ------------------------------------------------------------------
CDL::CDL() = default;
CDL::~CDL() = default;

bool CDL::start(const std::string& session_name) {
    session_name_ = session_name.empty() ? "session" : session_name;
    active_ = true;
    return true;
}

void CDL::stop() {
    active_ = false;
}

bool CDL::setActiveModule(const std::string& id) {
    auto it = modules_.find(id);
    if (it == modules_.end()) {
        active_module_ = nullptr;
        return false;
    }
    active_module_ = &it->second;
    return true;
}

Module* CDL::registerModule(const ModuleInfo& info) {
    auto it = modules_.emplace(info.id, Module(info));
    Module* m = &it.first->second;
    for (uint32_t a = m->base; a < m->base + m->size; ++a) {
        modules_by_addr_[a].push_back(m);
    }
    return m;
}

Module* CDL::findModuleForLinear(uint32_t linear) {
    if (active_module_ != nullptr) {
        int64_t rel = active_module_->relativeOffset(linear);
        if (rel >= 0) return active_module_;
    }
    auto it = modules_by_addr_.find(linear);
    if (it == modules_by_addr_.end()) return nullptr;
    // If multiple modules claim the same address, prefer the one with the most-recent write.
    return it->second.empty() ? nullptr : it->second.back();
}

void CDL::recordInsnFetch(uint16_t cs, uint32_t ip) {
    if (!active_) return;
    const uint32_t linear = static_cast<uint32_t>(cs) * 16u + ip;
    char dasm_line[256] = {0};
    int len = 0;
    {
        PhysPt phys = ::GetAddress(cs, ip);
        len = static_cast<int>(DasmI386(dasm_line, phys, ip, cpu.code.big));
    }
    Module* m = findModuleForLinear(linear);
    if (m == nullptr) return;
    const uint32_t start_off = linear - m->base;
    // First byte is the instruction start.
    m->markAccess(start_off, InsnStart);
    // Following bytes are instruction bytes.
    for (int i = 1; i < len; ++i) {
        uint32_t off = start_off + i;
        if (off < m->size) m->markAccess(off, InsnByte);
    }
    m->addEntryPoint(start_off);

#if PC98CDL_STACK_TRACKING
    // Stack access tracking: check raw opcode bytes for PUSH/POP/CALL/RET
    // and record stack reads/writes at SS:SP.
    {
        const PhysPt phys = ::GetAddress(cs, ip);
        const uint8_t op = mem_readb(phys);
        const uint16_t ss_val = SegValue(::ss);
        const uint16_t sp_val = reg_sp;
        const uint32_t stack_linear = static_cast<uint32_t>(ss_val) * 16u + sp_val;

        bool is_push = false, is_pop = false, is_call = false, is_ret = false;

        if (op >= 0x50 && op <= 0x57) is_push = true;      // PUSH r16
        else if (op >= 0x58 && op <= 0x5F) is_pop = true;   // POP r16
        else if (op == 0x68 || op == 0x6A) is_push = true;  // PUSH imm
        else if (op == 0x9C) is_push = true;                 // PUSHF
        else if (op == 0x9D) is_pop = true;                  // POPF
        else if (op == 0x8F) is_pop = true;                  // POP r/m
        else if (op == 0xE8 || op == 0x9A) is_call = true;   // CALL
        else if (op == 0xC3 || op == 0xC2 || op == 0xCB || op == 0xCA) is_ret = true;
        else if (op == 0xFF) {
            const uint8_t modrm = mem_readb(phys + 1);
            const uint8_t reg = (modrm >> 3) & 7;
            if (reg == 2) is_call = true;     // CALL r/m
            else if (reg == 6) is_push = true; // PUSH r/m
        }

        if (is_push || is_call) {
            recordStackWrite(stack_linear - 2, 2);
        } else if (is_pop || is_ret) {
            recordStackRead(stack_linear, 2);
        }
    }
#endif
}

void CDL::recordDataRead(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, DataRead);
}

void CDL::recordDataWrite(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, DataWrite);
}

void CDL::recordStackRead(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, StackRead);
}

void CDL::recordStackWrite(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, StackWrite);
}

void CDL::recordCodePointer(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, CodePtrRead);
}

void CDL::recordDataPointer(uint32_t linear, uint32_t len) {
    if (!active_) return;
    recordAccess(linear, len, DataPtrRead);
}

void CDL::recordAccess(uint32_t linear, uint32_t len, uint32_t flag) {
    Module* m = findModuleForLinear(linear);
    if (m == nullptr) return;
    uint32_t max = static_cast<uint32_t>(m->size);
    uint32_t off = linear - m->base;
    uint32_t end = std::min(off + len, max);
    for (uint32_t i = off; i < end; ++i) {
        m->markAccess(i, flag);
    }
}

void CDL::beginLoad(const LoadSpan& span) {
    current_load = span;
    current_load.active = true;
}

void CDL::endLoad() {
    if (!current_load.active) return;
    if (current_load.size == 0) {
        current_load.active = false;
        return;
    }
    // Mark the destination with DirectFileLoad / Decompressed etc based on origin.
    Module* m = findModuleForLinear(current_load.destination);
    if (m != nullptr) {
        uint32_t dst_rel = current_load.destination - m->base;
        uint32_t max = static_cast<uint32_t>(m->size);
        uint32_t end = std::min(dst_rel + current_load.size, max);
        uint16_t origin = DirectFileLoad;
        // If a source file name suggests compression, we could hint Decompressed.
        for (uint32_t i = dst_rel; i < end; ++i) {
            m->markOrigin(i, origin);
        }
    }
    current_load.active = false;
}

// ------------------------------------------------------------------
// Save / Merge
// ------------------------------------------------------------------
void CDL::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;

    f.write(kMagic, sizeof(kMagic));
    writeRaw(f, kVersion);
    writeString(f, session_name_);

    uint32_t module_count = static_cast<uint32_t>(modules_.size());
    writeRaw(f, module_count);

    for (const auto& kv : modules_) {
        const Module& m = kv.second;
        writeString(f, m.id);
        writeRaw(f, m.base);
        writeRaw(f, m.size);
        writeString(f, m.source_file);
        writeRaw(f, m.source_offset);

        uint32_t entry_count = static_cast<uint32_t>(m.entry_points.size());
        writeRaw(f, entry_count);
        for (uint32_t ep : m.entry_points) writeRaw(f, ep);

        // Count non-zero evidence bytes.
        uint32_t ev_count = 0;
        for (const auto& e : m.evidence) {
            if (e.access || e.origin || e.analysis) ++ev_count;
        }
        writeRaw(f, ev_count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m.evidence.size()); ++i) {
            const ByteEvidence& e = m.evidence[i];
            if (!e.access && !e.origin && !e.analysis) continue;
            writeRaw(f, i);
            writeRaw(f, e.access);
            writeRaw(f, e.origin);
            writeRaw(f, e.analysis);
        }
    }

    uint32_t transform_count = 0; // reserved; none currently stored at run time
    writeRaw(f, transform_count);
    f.flush();
}

void CDL::merge(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;

    char magic[sizeof(kMagic)];
    f.read(magic, sizeof(magic));
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return;

    uint16_t version = 0;
    readRaw(f, version);
    if (version != kVersion) return;

    std::string session = readString(f);
    (void)session;

    uint32_t module_count = 0;
    readRaw(f, module_count);
    for (uint32_t mi = 0; mi < module_count; ++mi) {
        std::string id = readString(f);
        if (id.empty()) continue;
        uint32_t base = 0, size = 0, src_off = 0;
        readRaw(f, base);
        readRaw(f, size);
        std::string src_file = readString(f);
        readRaw(f, src_off);

        Module* m = getModule(id);
        if (m == nullptr) {
            ModuleInfo info;
            info.id = id;
            info.base = base;
            info.size = size;
            info.source_file = src_file;
            info.source_offset = src_off;
            m = registerModule(info);
        }

        uint32_t entry_count = 0;
        readRaw(f, entry_count);
        for (uint32_t i = 0; i < entry_count; ++i) {
            uint32_t ep = 0;
            readRaw(f, ep);
            if (m != nullptr) m->addEntryPoint(ep);
        }

        uint32_t ev_count = 0;
        readRaw(f, ev_count);
        for (uint32_t i = 0; i < ev_count; ++i) {
            uint32_t off = 0;
            uint32_t access = 0;
            uint16_t origin = 0, analysis = 0;
            readRaw(f, off);
            readRaw(f, access);
            readRaw(f, origin);
            readRaw(f, analysis);
            if (m != nullptr && off < m->size) {
                m->evidence[off].access |= access;
                m->evidence[off].origin |= origin;
                m->evidence[off].analysis |= analysis;
            }
        }
    }

    uint32_t transform_count = 0;
    readRaw(f, transform_count);
}

Module* CDL::getModule(const std::string& id) {
    auto it = modules_.find(id);
    return it == modules_.end() ? nullptr : &it->second;
}

std::vector<std::string> CDL::moduleIds() const {
    std::vector<std::string> ids;
    ids.reserve(modules_.size());
    for (const auto& kv : modules_) ids.push_back(kv.first);
    return ids;
}

std::string CDL::statsJson() const {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& kv : modules_) {
        const Module& m = kv.second;
        Module::Stats s = m.computeStats();
        if (!first) out << ",";
        first = false;
        out << "\"" << m.id << "\":{";
        out << "\"base\":" << m.base << ",";
        out << "\"size\":" << s.total << ",";
        out << "\"executed\":" << s.executed << ",";
        out << "\"data_read\":" << s.data_read << ",";
        out << "\"data_written\":" << s.data_written << ",";
        out << "\"stack_read\":" << s.stack_read << ",";
        out << "\"stack_written\":" << s.stack_written << ",";
        out << "\"conflicts\":" << s.conflicts << ",";
        out << "\"unknown\":" << s.unknown << ",";
        out << "\"entry_points\":" << s.entries;
        out << "}";
    }
    out << "}";
    return out.str();
}

// ------------------------------------------------------------------
// Global instance
// ------------------------------------------------------------------
static CDL s_cdl;

CDL& GetCDL() {
    return s_cdl;
}

void ResetCDL() {
    s_cdl = CDL();
}

} // namespace PC98CDL
