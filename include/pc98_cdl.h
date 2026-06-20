/*
 *  Copyright (C) 2026  DOSBox-X authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  PC-98 Code/Data Logger (CDL)
 *
 *  A BizHawk-style evidence collector for 16-bit x86/DOS overlays. It stores
 *  per-byte bitmasks: runtime access, provenance/origin, and static/manual
 *  analysis. None of the three groups overwrite one another.
 */

#ifndef DOSBOX_PC98_CDL_H
#define DOSBOX_PC98_CDL_H

#include <config.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PC98CDL {

enum AccessFlag : uint32_t {
    InsnStart       = 1u << 0,
    InsnByte        = 1u << 1,
    DataRead        = 1u << 2,
    DataWrite       = 1u << 3,
    StackRead       = 1u << 4,
    StackWrite      = 1u << 5,
    CodePtrRead     = 1u << 6,
    DataPtrRead     = 1u << 7,
    StringSource    = 1u << 8,
    StringDest      = 1u << 9,
    BlockSource     = 1u << 10,
    BlockDest       = 1u << 11,
    IoPayload       = 1u << 12,
    SelfModifyWrite = 1u << 13
};

enum OriginFlag : uint16_t {
    DirectFileLoad  = 1u << 0,
    MemoryCopy      = 1u << 1,
    Decompressed    = 1u << 2,
    Relocated       = 1u << 3,
    LoaderPatched   = 1u << 4,
    Generated       = 1u << 5
};

enum AnalysisFlag : uint16_t {
    StaticCode      = 1u << 0,
    StaticData      = 1u << 1,
    UserCode        = 1u << 2,
    UserData        = 1u << 3,
    Text            = 1u << 4,
    PointerTable    = 1u << 5,
    Padding         = 1u << 6,
    Ambiguous       = 1u << 7
};

struct ByteEvidence {
    uint32_t access = 0;
    uint16_t origin = 0;
    uint16_t analysis = 0;
};

struct TransformMapping {
    std::string src_file;
    uint32_t src_start = 0;
    uint32_t src_len = 0;
    std::string dst_module;
    uint32_t dst_start = 0;
    uint32_t dst_len = 0;
    uint16_t origin = 0;
};

struct LoadSpan {
    bool active = false;
    std::string file;
    uint32_t file_offset = 0;
    uint32_t destination = 0; // linear destination
    uint32_t size = 0;
};

struct ModuleInfo {
    std::string id;
    uint32_t base = 0;          // linear load address in PC-98 memory
    uint32_t size = 0;
    std::string source_file;    // optional original file name
    uint32_t source_offset = 0; // optional offset inside source_file
};

class Module {
public:
    std::string id;
    uint32_t base = 0;
    uint32_t size = 0;
    std::string source_file;
    uint32_t source_offset = 0;

    std::vector<ByteEvidence> evidence;
    std::unordered_set<uint32_t> entry_points;   // module-relative offsets

    explicit Module() = default;
    explicit Module(const ModuleInfo& info);

    // returns module-relative offset, or <0 if outside
    int64_t relativeOffset(uint32_t linear) const;

    void markAccess(uint32_t offset, uint32_t flag);
    void markOrigin(uint32_t offset, uint16_t flag);
    void markAnalysis(uint32_t offset, uint16_t flag);
    void addEntryPoint(uint32_t module_offset);

    // stats used by consumers
    struct Stats {
        uint32_t total = 0;
        uint32_t executed = 0;
        uint32_t data_read = 0;
        uint32_t data_written = 0;
        uint32_t stack_read = 0;
        uint32_t stack_written = 0;
        uint32_t conflicts = 0;
        uint32_t unknown = 0;
        uint32_t entries = 0;
    };
    Stats computeStats() const;
};

class CDL {
public:
    CDL();
    ~CDL();

    bool start(const std::string& session_name = "session");
    void stop();
    bool active() const { return active_; }

    // Module management
    Module* registerModule(const ModuleInfo& info);
    Module* getModule(const std::string& id);
    bool setActiveModule(const std::string& id);
    Module* activeModule() { return active_module_; }

    // Load provenance
    void beginLoad(const LoadSpan& span);
    void endLoad();
    bool loadActive() const { return current_load.active; }
    const LoadSpan& currentLoad() const { return current_load; }

    // Runtime access recording (called from CPU hooks)
    void recordInsnFetch(uint16_t cs, uint32_t ip);
    void recordDataRead(uint32_t linear, uint32_t len);
    void recordDataWrite(uint32_t linear, uint32_t len);
    void recordStackRead(uint32_t linear, uint32_t len);
    void recordStackWrite(uint32_t linear, uint32_t len);
    void recordCodePointer(uint32_t linear, uint32_t len);
    void recordDataPointer(uint32_t linear, uint32_t len);

    // Persistence (binary format consumed by Python tools)
    void save(const std::string& path) const;

    // Merge an existing binary CDL file into this collector.
    void merge(const std::string& path);

    // Summary accessors used by Lua API
    std::vector<std::string> moduleIds() const;
    std::string statsJson() const;

private:
    bool active_ = false;
    std::string session_name_;
    std::unordered_map<std::string, Module> modules_;
    std::unordered_map<uint32_t, std::vector<Module*>> modules_by_addr_;
    Module* active_module_ = nullptr;
    LoadSpan current_load;

    // helpers
    Module* findModuleForLinear(uint32_t linear);
    void recordAccess(uint32_t linear, uint32_t len, uint32_t flag);
};

// Singleton accessor used by Lua/CPU hooks.
CDL& GetCDL();
void ResetCDL();

} // namespace PC98CDL

#endif // DOSBOX_PC98_CDL_H
