/*
 *  Copyright (C) 2026  DOSBox-X authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Memory region snapshot/diff capture for loader/decompressor analysis.
 */

#include "mem_capture.h"
#include "mem.h"
#include <cstring>
#include <unordered_map>
#include <atomic>

namespace MemoryCapture {

struct Capture {
    uint32_t id;
    uint32_t start;
    uint32_t length;
    std::vector<uint8_t> snapshot;
};

static std::unordered_map<uint32_t, Capture> captures;
static std::atomic<uint32_t> next_capture_id{1};

bool is_capturing() {
    return !captures.empty();
}

std::vector<uint8_t> dump_memory_range(uint32_t start_linear, uint32_t length) {
    std::vector<uint8_t> bytes;
    if (length == 0) return bytes;
    bytes.resize(length);
    MEM_BlockRead(start_linear, bytes.data(), length);
    return bytes;
}

uint32_t begin_write_capture(uint32_t start_linear, uint32_t length) {
    uint32_t id = next_capture_id.fetch_add(1, std::memory_order_relaxed);
    Capture cap;
    cap.id = id;
    cap.start = start_linear;
    cap.length = length;
    cap.snapshot = dump_memory_range(start_linear, length);
    captures[id] = std::move(cap);
    return id;
}

std::vector<ChangedRange> end_write_capture(uint32_t id) {
    std::vector<ChangedRange> ranges;
    auto it = captures.find(id);
    if (it == captures.end()) return ranges;

    Capture& cap = it->second;
    const std::vector<uint8_t> current = dump_memory_range(cap.start, cap.length);

    if (current.size() != cap.snapshot.size()) {
        ChangedRange r;
        r.start_linear = cap.start;
        r.length = static_cast<uint32_t>(current.size());
        ranges.push_back(r);
    } else {
        const uint8_t* oldb = cap.snapshot.data();
        const uint8_t* newb = current.data();
        uint32_t i = 0;
        while (i < cap.length) {
            while (i < cap.length && oldb[i] == newb[i]) ++i;
            if (i >= cap.length) break;
            uint32_t run_start = i;
            while (i < cap.length && oldb[i] != newb[i]) ++i;
            ChangedRange r;
            r.start_linear = cap.start + run_start;
            r.length = i - run_start;
            ranges.push_back(r);
        }
    }

    captures.erase(it);
    return ranges;
}

} // namespace MemoryCapture
