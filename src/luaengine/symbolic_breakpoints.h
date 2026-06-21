#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward-declare the opaque handle type from debug_bridge.h
// (Can't include debug_bridge.h here due to #if C_LUA guard ordering)
using DebugBreakpointHandle = void*;

namespace LuaEngineSymbols {

class SymbolManager; // Forward declaration

struct SymbolicBreakpoint {
    std::string symbol_name;
    int32_t offset = 0;
    bool enabled = true;
    std::string description;
    uint32_t resolved_linear_address = 0;
    DebugBreakpointHandle physical_bp = nullptr;
    bool is_absolute = false; // true when created directly from a linear address without symbol
    bool address_valid = false; // true when resolved_linear_address is valid
};

class SymbolicBreakpointManager {
public:
    void addBreakpoint(const std::string& symbol, int32_t offset, const std::string& desc = "");
    void addAbsoluteBreakpoint(uint32_t linear_address, const std::string& desc = "");
    void removeBreakpoint(size_t index);
    void toggleBreakpoint(size_t index, bool enable);

    void resolveAll();

    void saveToFile(const std::string& filename);
    void loadFromFile(const std::string& filename);

    std::vector<SymbolicBreakpoint>& getBreakpoints() { return breakpoints_; }

private:
    void clearPhysicalBreakpoint(SymbolicBreakpoint& bp);

    std::vector<SymbolicBreakpoint> breakpoints_;
};

// Singleton access function to ensure single instance
SymbolicBreakpointManager* GetSymbolicBreakpointManager();

// Non-allocating getter - only returns existing instance without creating new one
SymbolicBreakpointManager* TryGetSymbolicBreakpointManager();

} // namespace LuaEngineSymbols
