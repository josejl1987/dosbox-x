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

#ifndef DOSBOX_MEM_CAPTURE_H
#define DOSBOX_MEM_CAPTURE_H

#include <config.h>
#include <cstdint>
#include <vector>

namespace MemoryCapture {

struct ChangedRange {
    uint32_t start_linear;
    uint32_t length;
};

// Begin snapshot-diff capture of a linear address range.
// Returns a capture handle that must be passed to end_write_capture.
uint32_t begin_write_capture(uint32_t start_linear, uint32_t length);

// End capture for the given handle and return all byte ranges that changed.
// Returns an empty vector if the handle is unknown.
std::vector<ChangedRange> end_write_capture(uint32_t id);

// Dump an arbitrary linear range into a byte vector.
std::vector<uint8_t> dump_memory_range(uint32_t start_linear, uint32_t length);

// Returns true if any capture is currently active.
bool is_capturing();

} // namespace MemoryCapture

#endif // DOSBOX_MEM_CAPTURE_H
