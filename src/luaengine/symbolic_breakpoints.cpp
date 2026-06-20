#include "symbolic_breakpoints.h"

#include "symbol_manager.h"
#include "debug.h"
#include "luaengine.h"
#include "core_debug_interface.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace LuaEngineSymbols {

// RAII singleton using function-static variable to avoid static destruction order issues
SymbolicBreakpointManager* GetSymbolicBreakpointManager() {
    static std::unique_ptr<SymbolicBreakpointManager> instance =
        std::make_unique<SymbolicBreakpointManager>();
    return instance.get();
}

SymbolicBreakpointManager* TryGetSymbolicBreakpointManager() {
    // This function is now less useful since we don't have a global to check
    // For compatibility, we'll check if the singleton exists by calling GetSymbolicBreakpointManager
    // but this will create it if it doesn't exist. Consider deprecating this function.
    return GetSymbolicBreakpointManager();
}

void SymbolicBreakpointManager::clearPhysicalBreakpoint(SymbolicBreakpoint& bp) {
    // 1. Remove the DOSBox physical breakpoint object
    if (bp.physical_bp) {
        bp.physical_bp->Activate(false);
        // We don't manually delete CBreakpoint pointers usually, DOSBox manages the pool,
        // but we must ensure it's inactive.
        bp.physical_bp = nullptr;
    }

    // 2. Remove the Breakpoint from the DOSBox internal list and Core Debugger
    if (bp.address_valid) {
        // Calculate segment/offset for internal DOSBox removal
        uint16_t seg = 0;
        uint16_t off = 0;

        // Use the address currently stored in the BP (the "old" address)
        if (g_symbol_manager) {
            g_symbol_manager->linearToSegmentOffset(bp.resolved_linear_address, seg, off);
        } else {
            // Fallback calculation
            seg = static_cast<uint16_t>(bp.resolved_linear_address >> 4);
            off = static_cast<uint16_t>(bp.resolved_linear_address & 0xFFFF);
        }

        // Remove from DOSBox internal list
        // Note: Verify if DeleteBreakpoint requires specific Seg:Off or if it handles the list logic.
        if (seg != 0 || off != 0) {
            CBreakpoint::DeleteBreakpoint(seg, off);
        }

        // Remove from the UI/Core Debugger list
        if (LuaEngineDebug::g_core_debugger) {
            LuaEngineDebug::g_core_debugger->removeBreakpoint(bp.resolved_linear_address);
        }
    }

    // We do NOT set address_valid = false here.
    // The caller (resolveAll) decides validity after recalculation.
}

void SymbolicBreakpointManager::resolveAll() {
    bool any_active = false;

    // Pre-check: Do we need hooks?
    for (const auto& bp : breakpoints_) {
        if (bp.enabled) {
            any_active = true;
            break;
        }
    }

    // Update hook state globally
    luaEngine.enableInstructionHooks(any_active);

    // Main Loop: Update every breakpoint
    for (auto& bp : breakpoints_) {
        // CRITICAL FIX: Remove the EXISTING physical breakpoint using the OLD address
        // BEFORE we calculate the NEW address.
        clearPhysicalBreakpoint(bp);

        // If disabled, mark invalid and stop processing this one
        if (!bp.enabled) {
            bp.address_valid = false;
            continue;
        }

        // Logic to calculate the NEW address
        bool calculation_success = false;
        uint32_t new_linear_address = 0;

        if (bp.is_absolute) {
            // Absolute breakpoints retain their address
            new_linear_address = bp.resolved_linear_address;
            calculation_success = true;
        } else {
            // Symbolic resolution
            if (g_symbol_manager) {
                const uint32_t base_addr = g_symbol_manager->getSymbolAddress(bp.symbol_name);

                if (base_addr != SymbolManager::INVALID_ADDRESS) {
                    const int64_t target_signed = static_cast<int64_t>(base_addr) + static_cast<int64_t>(bp.offset);

                    // Check bounds
                    if (target_signed >= 0 && target_signed <= 0xFFFFFFFF) {
                        new_linear_address = static_cast<uint32_t>(target_signed);
                        calculation_success = true;
                    }
                }
            }
        }

        // Apply results
        bp.address_valid = calculation_success;

        if (calculation_success) {
            bp.resolved_linear_address = new_linear_address;

            // Calculate Seg:Off for DOSBox
            uint16_t seg = 0;
            uint16_t off = 0;
            if (g_symbol_manager) {
                g_symbol_manager->linearToSegmentOffset(new_linear_address, seg, off);
            } else {
                seg = static_cast<uint16_t>(new_linear_address >> 4);
                off = static_cast<uint16_t>(new_linear_address & 0xFFFF);
            }

            // 1. Add DOSBox physical breakpoint
            CBreakpoint* physical_bp = CBreakpoint::AddBreakpoint(seg, off, false);
            if (physical_bp) {
                physical_bp->Activate(true);
                bp.physical_bp = physical_bp;
            }

            // 2. Add to Core Debugger UI
            if (LuaEngineDebug::g_core_debugger) {
                LuaEngineDebug::Breakpoint cb{new_linear_address};
                LuaEngineDebug::g_core_debugger->addBreakpoint(cb);
            }
        }
    }

    // Ensure DOSBox internal states are flushed/active
    CBreakpoint::ActivateBreakpoints();
}

void SymbolicBreakpointManager::addBreakpoint(const std::string& symbol, int32_t offset, const std::string& desc) {
    SymbolicBreakpoint bp;
    bp.symbol_name = symbol;
    bp.offset = offset;
    bp.enabled = true;
    bp.description = desc;
    bp.resolved_linear_address = 0;
    bp.physical_bp = nullptr;
    bp.is_absolute = false;
    bp.address_valid = false;

    breakpoints_.push_back(bp);
    resolveAll();

    // Save breakpoints immediately after adding
    saveToFile("symbolic_breakpoints.dat");
}

void SymbolicBreakpointManager::addAbsoluteBreakpoint(uint32_t linear_address, const std::string& desc) {
    SymbolicBreakpoint bp;
    bp.symbol_name = "";
    bp.offset = 0;
    bp.enabled = true;
    bp.description = desc;
    bp.resolved_linear_address = linear_address;
    bp.physical_bp = nullptr;
    bp.is_absolute = true;
    bp.address_valid = true;

    breakpoints_.push_back(bp);
    resolveAll();
}

void SymbolicBreakpointManager::removeBreakpoint(size_t index) {
    if (index >= breakpoints_.size()) {
        return;
    }

    // Clear physical BP before erasing from vector
    clearPhysicalBreakpoint(breakpoints_[index]);

    breakpoints_.erase(breakpoints_.begin() + static_cast<std::ptrdiff_t>(index));

    // Re-run resolveAll to ensure hook states are correct
    resolveAll();
}

void SymbolicBreakpointManager::toggleBreakpoint(size_t index, bool enable) {
    if (index >= breakpoints_.size()) {
        return;
    }

    breakpoints_[index].enabled = enable;

    // resolveAll handles clearing/setting physical breakpoints based on enabled state
    resolveAll();
}

void SymbolicBreakpointManager::saveToFile(const std::string& filename) {
    if (filename.empty()) return;

    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "# DOSBox-X Symbolic Breakpoints\n";
    file << "# Format: symbol|offset|enabled|description|is_absolute\n";

    for (const auto& bp : breakpoints_) {
        std::string safe_symbol = bp.symbol_name;
        std::string safe_desc = bp.description;

        // Sanitize delimiters
        std::replace(safe_symbol.begin(), safe_symbol.end(), '|', '_');
        std::replace(safe_desc.begin(), safe_desc.end(), '|', '_');

        file << safe_symbol << "|"
             << bp.offset << "|"
             << (bp.enabled ? "1" : "0") << "|"
             << safe_desc << "|"
             << (bp.is_absolute ? "1" : "0")
             << "\n";
    }

    file.close();
}

void SymbolicBreakpointManager::loadFromFile(const std::string& filename) {
    if (filename.empty()) return;

    std::ifstream file(filename);
    if (!file.is_open()) return;

    // Clear existing hooks before clearing vector
    for(auto& bp : breakpoints_) {
        clearPhysicalBreakpoint(bp);
    }
    breakpoints_.clear();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> parts;
        while (std::getline(ss, segment, '|')) {
            parts.push_back(segment);
        }

        if (parts.size() < 5) continue;

        try {
            SymbolicBreakpoint bp;
            bp.symbol_name = parts[0];
            bp.offset = static_cast<int32_t>(std::stol(parts[1]));
            bp.enabled = (parts[2] == "1");
            bp.description = parts[3];
            bp.is_absolute = (parts[4] == "1");

            // Runtime state - will be resolved by resolveAll()
            bp.physical_bp = nullptr;
            bp.resolved_linear_address = 0;
            bp.address_valid = false;

            // Minimal validation
            if (!bp.is_absolute && bp.symbol_name.empty()) continue;

            breakpoints_.push_back(bp);

        } catch (...) {
            // Skip malformed lines
        }
    }

    file.close();
    resolveAll();
}

} // namespace LuaEngineSymbols