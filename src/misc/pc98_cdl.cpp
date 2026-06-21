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

// ponytail: GetAddress is in debug.cpp, not exposed in any header
extern uint64_t GetAddress(uint16_t seg, uint32_t offset);

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
constexpr uint16_t kVersion = 2;

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
    , source_offset(info.source_offset)
    , enable_last_writer(info.enable_last_writer)
    , enable_coverage(info.enable_coverage) {
    evidence.resize(size);
    if (enable_last_writer) last_writer_pc_.resize(size, 0);
    if (enable_coverage) {
        // ponytail: each uint64_t covers 64 pages (256KB), page_index = offset >> 12
        uint32_t max_page_index = (size + 0xFFFu) >> 12;
        coverage_bitmap_.resize((max_page_index + 63) / 64, 0);
    }
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

void Module::setLastWriterPc(uint32_t offset, uint32_t pc) {
    if (!enable_last_writer || offset >= last_writer_pc_.size()) return;
    last_writer_pc_[offset] = pc;
}

uint32_t Module::lastWriterPc(uint32_t offset) const {
    if (!enable_last_writer || offset >= last_writer_pc_.size()) return 0;
    return last_writer_pc_[offset];
}

void Module::markCoveragePage(uint32_t linear) {
    if (!enable_coverage || coverage_bitmap_.empty()) return;
    uint32_t page_index = (linear - base) >> 12;
    uint32_t word_index = page_index / 64;
    uint32_t bit_index = page_index % 64;
    if (word_index < coverage_bitmap_.size()) {
        coverage_bitmap_[word_index] |= (1ULL << bit_index);
    }
}

std::string Module::coverageJson() const {
    std::ostringstream out;
    out << "{\"base\":" << base << ",\"size\":" << size << ",\"pages\":[";
    if (enable_coverage && !coverage_bitmap_.empty()) {
        uint32_t max_page = (size + 0xFFFu) >> 12;
        bool first = true;
        for (uint32_t p = 0; p < max_page; ++p) {
            uint32_t word = p / 64;
            uint32_t bit = p % 64;
            bool covered = (coverage_bitmap_[word] >> bit) & 1;
            if (!first) out << ",";
            first = false;
            out << (covered ? 1 : 0);
        }
    }
    out << "]}";
    return out.str();
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
        PhysPt phys = GetAddress(cs, ip);
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

    // ponytail: PR6 — mark coverage page if enabled
    if (m->enable_coverage) {
        m->markCoveragePage(linear);
    }

#if PC98CDL_STACK_TRACKING
    // Stack access tracking: check raw opcode bytes for PUSH/POP/CALL/RET
    // and record stack reads/writes at SS:SP.
    {
        const PhysPt phys = GetAddress(cs, ip);
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

void CDL::recordDataWrite(uint32_t linear, uint32_t len, uint32_t writer_pc) {
    if (!active_) return;
    recordAccess(linear, len, DataWrite, writer_pc);
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

void CDL::recordAccess(uint32_t linear, uint32_t len, uint32_t flag, uint32_t writer_pc) {
    Module* m = findModuleForLinear(linear);
    if (m == nullptr) return;
    uint32_t max = static_cast<uint32_t>(m->size);
    uint32_t off = linear - m->base;
    uint32_t end = std::min(off + len, max);
    for (uint32_t i = off; i < end; ++i) {
        m->markAccess(i, flag);
        // ponytail: PR6 — set last_writer_pc on DataWrite when enabled
        if (flag == DataWrite && m->enable_last_writer && writer_pc != 0) {
            m->setLastWriterPc(i, writer_pc);
        }
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

        // ponytail: PR6 — write feature flags so merge knows what's present
        uint8_t mod_flags = 0;
        if (m.enable_last_writer) mod_flags |= 1;
        if (m.enable_coverage) mod_flags |= 2;
        writeRaw(f, mod_flags);

        uint32_t entry_count = static_cast<uint32_t>(m.entry_points.size());
        writeRaw(f, entry_count);
        for (uint32_t ep : m.entry_points) writeRaw(f, ep);

        // Count non-zero evidence bytes.
        uint32_t ev_count = 0;
        for (const auto& e : m.evidence) {
            if (e.access || e.origin || e.analysis || e.last_writer_pc) ++ev_count;
        }
        writeRaw(f, ev_count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m.evidence.size()); ++i) {
            const ByteEvidence& e = m.evidence[i];
            if (!e.access && !e.origin && !e.analysis && !e.last_writer_pc) continue;
            writeRaw(f, i);
            writeRaw(f, e.access);
            writeRaw(f, e.origin);
            writeRaw(f, e.analysis);
            if (m.enable_last_writer) {
                writeRaw(f, e.last_writer_pc);
            }
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
    if (version < 1 || version > kVersion) return;

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

        // ponytail: PR6 — v2 has module flags byte, v1 does not
        uint8_t mod_flags = 0;
        if (version >= 2) {
            readRaw(f, mod_flags);
        }

        Module* m = getModule(id);
        if (m == nullptr) {
            ModuleInfo info;
            info.id = id;
            info.base = base;
            info.size = size;
            info.source_file = src_file;
            info.source_offset = src_off;
            info.enable_last_writer = (mod_flags & 1) != 0;
            info.enable_coverage = (mod_flags & 2) != 0;
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
        bool has_last_writer = (m != nullptr && m->enable_last_writer) || (version >= 2 && (mod_flags & 1));
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
            // ponytail: PR6 — v2 may have last_writer_pc per entry
            if (version >= 2 && has_last_writer) {
                uint32_t lwpc = 0;
                readRaw(f, lwpc);
                if (m != nullptr && off < m->size && lwpc != 0) {
                    m->setLastWriterPc(off, lwpc);
                }
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

// ponytail: PR6 — JSON export for coverage + last-writer provenance
std::string CDL::exportCoverageJson() const {
    std::ostringstream out;
    out << "{\"modules\":{";
    bool first_mod = true;
    for (const auto& kv : modules_) {
        const Module& m = kv.second;
        if (!first_mod) out << ",";
        first_mod = false;
        out << "\"" << m.id << "\":{";
        out << "\"base\":" << m.base << ",\"size\":" << m.size << ",";
        // Coverage
        if (m.enable_coverage && !m.coverage_bitmap_.empty()) {
            out << "\"coverage\":" << m.coverageJson() << ",";
        } else {
            // Compute coverage from InsnStart bits
            uint32_t max_page = (m.size + 0xFFFu) >> 12;
            out << "\"coverage\":{\"base\":" << m.base << ",\"size\":" << m.size << ",\"pages\":[";
            for (uint32_t p = 0; p < max_page; ++p) {
                if (p > 0) out << ",";
                uint32_t page_start = p << 12;
                uint32_t page_end = std::min(page_start + 0x1000u, m.size);
                bool covered = false;
                for (uint32_t i = page_start; i < page_end; ++i) {
                    if (m.evidence[i].access & InsnStart) { covered = true; break; }
                }
                out << (covered ? 1 : 0);
            }
            out << "]},";
        }
        // Last-writer provenance
        out << "\"last_writer\":{";
        if (m.enable_last_writer && !m.last_writer_pc_.empty()) {
            bool first_lw = true;
            for (uint32_t i = 0; i < m.size; ++i) {
                if (m.last_writer_pc_[i] != 0) {
                    if (!first_lw) out << ",";
                    first_lw = false;
                    out << "\"" << i << "\":" << m.last_writer_pc_[i];
                }
            }
        }
        out << "}}";
    }
    out << "}}";
    return out.str();
}

std::string CDL::exportJsonl() const {
    std::ostringstream out;
    for (const auto& kv : modules_) {
        const Module& m = kv.second;
        out << "{\"module\":\"" << m.id << "\",\"base\":" << m.base
            << ",\"size\":" << m.size;
        // Coverage
        if (m.enable_coverage && !m.coverage_bitmap_.empty()) {
            out << ",\"coverage\":" << m.coverageJson();
        }
        // Last-writer (only non-zero entries)
        if (m.enable_last_writer && !m.last_writer_pc_.empty()) {
            out << ",\"last_writer\":{";
            bool first_lw = true;
            for (uint32_t i = 0; i < m.size; ++i) {
                if (m.last_writer_pc_[i] != 0) {
                    if (!first_lw) out << ",";
                    first_lw = false;
                    out << "\"" << i << "\":" << m.last_writer_pc_[i];
                }
            }
            out << "}";
        }
        out << "}\n";
    }
    return out.str();
}

void CDL::exportCoverageToFile(const std::string& path) const {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return;
    f << exportCoverageJson();
    f.flush();
}

void CDL::exportJsonlToFile(const std::string& path) const {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return;
    f << exportJsonl();
    f.flush();
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
