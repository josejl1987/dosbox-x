#include "lua_symbol_bindings.h"
#include "symbol_manager.h"
#include "symbolic_breakpoints.h"

#include <memory>
#include <string>
#include <vector>

namespace LuaEngineSymbols {

void RegisterSymbolBindings(lua_State* L) {
    // Ensure manager exists using singleton access
    GetSymbolicBreakpointManager();

    // Create symbols table
    lua_newtable(L);
    
    // Symbol management functions
    lua_pushcfunction(L, lua_LoadSymbolFile);
    lua_setfield(L, -2, "loadSymbolFile");
    
    lua_pushcfunction(L, lua_ClearSymbols);
    lua_setfield(L, -2, "clearSymbols");
    
    lua_pushcfunction(L, lua_HasSymbols);
    lua_setfield(L, -2, "hasSymbols");
    
    lua_pushcfunction(L, lua_GetSymbolCount);
    lua_setfield(L, -2, "getSymbolCount");
    
    lua_pushcfunction(L, lua_GetLoadedFileName);
    lua_setfield(L, -2, "getLoadedFileName");
    
    lua_pushcfunction(L, lua_GetLoadedFormat);
    lua_setfield(L, -2, "getLoadedFormat");
    
    // Symbol lookup functions
    lua_pushcfunction(L, lua_GetSymbolName);
    lua_setfield(L, -2, "getSymbolName");
    
    lua_pushcfunction(L, lua_GetSymbolAddress);
    lua_setfield(L, -2, "getSymbolAddress");
    
    lua_pushcfunction(L, lua_FindSymbol);
    lua_setfield(L, -2, "findSymbol");
    
    lua_pushcfunction(L, lua_GetAllSymbols);
    lua_setfield(L, -2, "getAllSymbols");
    
    lua_pushcfunction(L, lua_GetSymbolsInRange);
    lua_setfield(L, -2, "getSymbolsInRange");
    
    lua_pushcfunction(L, lua_GetSymbolNames);
    lua_setfield(L, -2, "getSymbolNames");
    
    // Segment mapping functions
    lua_pushcfunction(L, lua_AddSegmentMapping);
    lua_setfield(L, -2, "addSegmentMapping");
    
    lua_pushcfunction(L, lua_RemoveSegmentMapping);
    lua_setfield(L, -2, "removeSegmentMapping");
    
    lua_pushcfunction(L, lua_UpdateSegmentMapping);
    lua_setfield(L, -2, "updateSegmentMapping");
    
    lua_pushcfunction(L, lua_EnableSegmentMapping);
    lua_setfield(L, -2, "enableSegmentMapping");
    
    lua_pushcfunction(L, lua_GetSegmentMappings);
    lua_setfield(L, -2, "getSegmentMappings");
    
    lua_pushcfunction(L, lua_FindSegmentMapping);
    lua_setfield(L, -2, "findSegmentMapping");
    
    lua_pushcfunction(L, lua_ClearSegmentMappings);
    lua_setfield(L, -2, "clearSegmentMappings");
    
    lua_pushcfunction(L, lua_AutoDetectSegments);
    lua_setfield(L, -2, "autoDetectSegments");
    
    lua_pushcfunction(L, lua_RemapAllSymbols);
    lua_setfield(L, -2, "remapAllSymbols");
    
    // Address conversion functions
    lua_pushcfunction(L, lua_SegmentOffsetToLinear);
    lua_setfield(L, -2, "segmentOffsetToLinear");
    
    lua_pushcfunction(L, lua_LinearToSegmentOffset);
    lua_setfield(L, -2, "linearToSegmentOffset");
    
    lua_pushcfunction(L, lua_MapAddressToMemory);
    lua_setfield(L, -2, "mapAddressToMemory");

    // Utility functions
    lua_pushcfunction(L, lua_AddSymbol);
    lua_setfield(L, -2, "addSymbol");
    lua_pushcfunction(L, lua_AddSymbolsBulk);
    lua_setfield(L, -2, "addSymbolsBulk");
    
    lua_pushcfunction(L, lua_RemoveSymbol);
    lua_setfield(L, -2, "removeSymbol");

    // Symbolic breakpoint helpers
    lua_pushcfunction(L, lua_GetSymbolicBreakpoints);
    lua_setfield(L, -2, "getSymbolicBreakpoints");

    lua_pushcfunction(L, lua_AddSymbolicBreakpoint);
    lua_setfield(L, -2, "addSymbolicBreakpoint");

    lua_pushcfunction(L, lua_RemoveSymbolicBreakpoint);
    lua_setfield(L, -2, "removeSymbolicBreakpoint");

    lua_pushcfunction(L, lua_ToggleSymbolicBreakpoint);
    lua_setfield(L, -2, "toggleSymbolicBreakpoint");

    lua_pushcfunction(L, lua_SaveSymbolicBreakpoints);
    lua_setfield(L, -2, "saveSymbolicBreakpoints");

    lua_pushcfunction(L, lua_LoadSymbolicBreakpoints);
    lua_setfield(L, -2, "loadSymbolicBreakpoints");
    
    // Set the symbols table as global
    lua_setglobal(L, "symbols");
}

// Symbol management functions
int lua_LoadSymbolFile(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);

    if(!g_symbol_manager) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Symbol manager not initialized");
        return 2;
    }

    const bool ok = g_symbol_manager->loadFromFile(filename);
    lua_pushboolean(L, ok ? 1 : 0);
    if(!ok) {
        lua_pushstring(L, "Failed to load symbol file");
        return 2;
    }

    return 1;
}

int lua_ClearSymbols(lua_State* L) {
    if (g_symbol_manager) {
        g_symbol_manager->clearSymbols();
    }
    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }
    return 0;
}

int lua_HasSymbols(lua_State* L) {
    bool has_symbols = g_symbol_manager && g_symbol_manager->hasSymbols();
    lua_pushboolean(L, has_symbols);
    return 1;
}

int lua_GetSymbolCount(lua_State* L) {
    size_t count = g_symbol_manager ? g_symbol_manager->getSymbolCount() : 0;
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int lua_GetLoadedFileName(lua_State* L) {
    std::string filename = g_symbol_manager ? g_symbol_manager->getLoadedFileName() : "";
    lua_pushstring(L, filename.c_str());
    return 1;
}

int lua_GetLoadedFormat(lua_State* L) {
    if (!g_symbol_manager) {
        lua_pushstring(L, "UNKNOWN");
        return 1;
    }
    
    auto format = g_symbol_manager->getLoadedFormat();
    const char* format_name = "UNKNOWN";
    switch (format) {
        case SymbolFormat::MASM_MAP: format_name = "MASM_MAP"; break;
        case SymbolFormat::MASM_LST: format_name = "MASM_LST"; break;
        case SymbolFormat::WATCOM_MAP: format_name = "WATCOM_MAP"; break;
        case SymbolFormat::BORLAND_MAP: format_name = "BORLAND_MAP"; break;
        case SymbolFormat::GNU_MAP: format_name = "GNU_MAP"; break;
        case SymbolFormat::COFF_SYMBOLS: format_name = "COFF_SYMBOLS"; break;
    }
    
    lua_pushstring(L, format_name);
    return 1;
}

// Symbol lookup functions
int lua_GetSymbolName(lua_State* L) {
    uint32_t address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    bool include_offset = lua_gettop(L) > 1 ? lua_toboolean(L, 2) : true;
    
    std::string name = g_symbol_manager ? g_symbol_manager->getSymbolName(address, include_offset) : "";
    lua_pushstring(L, name.c_str());
    return 1;
}

int lua_GetSymbolAddress(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    
    uint32_t address = g_symbol_manager ? g_symbol_manager->getSymbolAddress(name) : 0;
    lua_pushinteger(L, static_cast<lua_Integer>(address));
    return 1;
}

int lua_FindSymbol(lua_State* L) {
    uint32_t address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    
    if (!g_symbol_manager) {
        lua_pushnil(L);
        return 1;
    }
    
    Symbol* symbol = g_symbol_manager->findSymbol(address);
    if (symbol) {
        PushSymbolToLua(L, *symbol);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_GetAllSymbols(lua_State* L) {
    if (!g_symbol_manager) {
        lua_newtable(L);
        return 1;
    }
    
    auto symbols = g_symbol_manager->getAllSymbols();
    
    lua_newtable(L);
    for (size_t i = 0; i < symbols.size(); ++i) {
        PushSymbolToLua(L, symbols[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    
    return 1;
}

int lua_GetSymbolsInRange(lua_State* L) {
    uint32_t start_addr = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    uint32_t end_addr = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    
    if (!g_symbol_manager) {
        lua_newtable(L);
        return 1;
    }
    
    auto symbols = g_symbol_manager->getSymbolsInRange(start_addr, end_addr);
    
    lua_newtable(L);
    for (size_t i = 0; i < symbols.size(); ++i) {
        PushSymbolToLua(L, symbols[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    
    return 1;
}

int lua_GetSymbolNames(lua_State* L) {
    if (!g_symbol_manager) {
        lua_newtable(L);
        return 1;
    }
    
    auto names = g_symbol_manager->getSymbolNames();
    
    lua_newtable(L);
    for (size_t i = 0; i < names.size(); ++i) {
        lua_pushstring(L, names[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    
    return 1;
}

// Segment mapping functions
int lua_AddSegmentMapping(lua_State* L) {
    // Symbol manager is now initialized by LuaEngine
    if (!g_symbol_manager) {
        luaL_error(L, "Symbol manager not initialized");
        return 0;
    }

    SegmentMapping mapping = GetSegmentMappingFromLua(L, 1);
    g_symbol_manager->addSegmentMapping(mapping);
    return 0;
}

int lua_RemoveSegmentMapping(lua_State* L) {
    const char* segment_name = luaL_checkstring(L, 1);
    
    if (g_symbol_manager) {
        g_symbol_manager->removeSegmentMapping(segment_name);
    }
    return 0;
}

int lua_UpdateSegmentMapping(lua_State* L) {
    const char* segment_name = luaL_checkstring(L, 1);
    uint32_t memory_base = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    
    if (g_symbol_manager) {
        g_symbol_manager->updateSegmentMapping(segment_name, memory_base);
    }
    return 0;
}

int lua_EnableSegmentMapping(lua_State* L) {
    const char* segment_name = luaL_checkstring(L, 1);
    bool enabled = lua_toboolean(L, 2);
    
    if (g_symbol_manager) {
        g_symbol_manager->enableSegmentMapping(segment_name, enabled);
    }
    return 0;
}

int lua_GetSegmentMappings(lua_State* L) {
    if (!g_symbol_manager) {
        lua_newtable(L);
        return 1;
    }
    
    auto mappings = g_symbol_manager->getSegmentMappings();
    
    lua_newtable(L);
    for (size_t i = 0; i < mappings.size(); ++i) {
        PushSegmentMappingToLua(L, mappings[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    
    return 1;
}

int lua_FindSegmentMapping(lua_State* L) {
    const char* segment_name = luaL_checkstring(L, 1);
    
    if (!g_symbol_manager) {
        lua_pushnil(L);
        return 1;
    }
    
    SegmentMapping* mapping = g_symbol_manager->findSegmentMapping(segment_name);
    if (mapping) {
        PushSegmentMappingToLua(L, *mapping);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int lua_ClearSegmentMappings(lua_State* L) {
    if (g_symbol_manager) {
        g_symbol_manager->clearSegmentMappings();
    }
    return 0;
}

int lua_AutoDetectSegments(lua_State* L) {
    if (g_symbol_manager) {
        g_symbol_manager->autoDetectSegments();
    }
    return 0;
}

int lua_RemapAllSymbols(lua_State* L) {
    if (g_symbol_manager) {
        g_symbol_manager->remapAllSymbols();
    }
    return 0;
}

// Address conversion functions
int lua_SegmentOffsetToLinear(lua_State* L) {
    uint16_t segment = static_cast<uint16_t>(luaL_checkinteger(L, 1));
    uint16_t offset = static_cast<uint16_t>(luaL_checkinteger(L, 2));
    
    uint32_t linear = g_symbol_manager ? 
        g_symbol_manager->segmentOffsetToLinear(segment, offset) :
        (static_cast<uint32_t>(segment) << 4) + offset;
    
    lua_pushinteger(L, static_cast<lua_Integer>(linear));
    return 1;
}

int lua_LinearToSegmentOffset(lua_State* L) {
    uint32_t linear = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    
    uint16_t segment, offset;
    if (g_symbol_manager) {
        g_symbol_manager->linearToSegmentOffset(linear, segment, offset);
    } else {
        segment = static_cast<uint16_t>(linear >> 4);
        offset = static_cast<uint16_t>(linear & 0xFFFF);
    }
    
    lua_pushinteger(L, segment);
    lua_pushinteger(L, offset);
    return 2;
}

int lua_MapAddressToMemory(lua_State* L) {
    uint16_t segment = static_cast<uint16_t>(luaL_checkinteger(L, 1));
    uint16_t offset = static_cast<uint16_t>(luaL_checkinteger(L, 2));
    const char* segment_name = lua_gettop(L) > 2 ? luaL_checkstring(L, 3) : "";
    
    uint32_t mapped_address = 0;
    if (g_symbol_manager) {
        // Use private method through public interface
        mapped_address = g_symbol_manager->segmentOffsetToLinear(segment, offset);
    } else {
        mapped_address = (static_cast<uint32_t>(segment) << 4) + offset;
    }
    
    lua_pushinteger(L, static_cast<lua_Integer>(mapped_address));
    return 1;
}

// Utility functions
int lua_AddSymbol(lua_State* L) {
    // Symbol manager is now initialized by LuaEngine
    if (!g_symbol_manager) {
        luaL_error(L, "Symbol manager not initialized");
        return 0;
    }

    Symbol symbol = GetSymbolFromLua(L, 1);
    g_symbol_manager->addSymbol(symbol);
    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }
    return 0;
}

int lua_AddSymbolsBulk(lua_State* L) {
    // Symbol manager is now initialized by LuaEngine
    if (!g_symbol_manager) {
        luaL_error(L, "Symbol manager not initialized");
        return 0;
    }

    if (lua_gettop(L) < 2 || !lua_istable(L, 2)) {
        return luaL_error(L, "addSymbolsBulk(base_address, symbols_table) expects a base and a table");
    }

    uint32_t base_addr = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    size_t count = lua_rawlen(L, 2);

    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, 2, static_cast<lua_Integer>(i)); // push entry
        if (lua_istable(L, -1)) {
            // Accept { offset=..., name=... } or { offset, name }
            uint32_t offset = 0;
            const char* name_cstr = nullptr;

            lua_getfield(L, -1, "offset");
            if (lua_isnumber(L, -1)) {
                offset = static_cast<uint32_t>(lua_tointeger(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "name");
            if (lua_isstring(L, -1)) {
                name_cstr = lua_tostring(L, -1);
            }
            lua_pop(L, 1);

            if (!name_cstr) {
                // fallback to positional
                lua_rawgeti(L, -1, 1);
                if (lua_isnumber(L, -1)) offset = static_cast<uint32_t>(lua_tointeger(L, -1));
                lua_pop(L, 1);

                lua_rawgeti(L, -1, 2);
                if (lua_isstring(L, -1)) name_cstr = lua_tostring(L, -1);
                lua_pop(L, 1);
            }

            if (name_cstr) {
                uint32_t addr = base_addr + offset;
                Symbol sym(addr, std::string(name_cstr));
                sym.file_address = offset;
                sym.segment = static_cast<uint16_t>(base_addr >> 4);
                sym.offset = static_cast<uint16_t>(offset & 0xFFFF);
                sym.segment_name = "";
                g_symbol_manager->addSymbol(sym);
            }
        }
        lua_pop(L, 1); // pop entry
    }

    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }

    return 0;
}

int lua_RemoveSymbol(lua_State* L) {
    if (!g_symbol_manager) {
        return 0;
    }
    
    if (lua_type(L, 1) == LUA_TSTRING) {
        const char* name = lua_tostring(L, 1);
        g_symbol_manager->removeSymbol(name);
    } else {
        uint32_t address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        g_symbol_manager->removeSymbol(address);
    }
    if (GetSymbolicBreakpointManager()) {
        GetSymbolicBreakpointManager()->resolveAll();
    }
    return 0;
}

int lua_GetSymbolicBreakpoints(lua_State* L) {
    if (!GetSymbolicBreakpointManager()) {
        lua_newtable(L);
        return 1;
    }

    auto& bps = GetSymbolicBreakpointManager()->getBreakpoints();
    lua_newtable(L);
    for (size_t i = 0; i < bps.size(); ++i) {
        const auto& bp = bps[i];
        lua_newtable(L);

        lua_pushstring(L, bp.symbol_name.c_str());
        lua_setfield(L, -2, "symbol");

        lua_pushinteger(L, static_cast<lua_Integer>(bp.offset));
        lua_setfield(L, -2, "offset");

        lua_pushboolean(L, bp.enabled);
        lua_setfield(L, -2, "enabled");

        lua_pushinteger(L, static_cast<lua_Integer>(bp.resolved_linear_address));
        lua_setfield(L, -2, "address");

        lua_pushstring(L, bp.description.c_str());
        lua_setfield(L, -2, "description");

        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int lua_AddSymbolicBreakpoint(lua_State* L) {
    const char* sym = luaL_checkstring(L, 1);
    int32_t offset = static_cast<int32_t>(luaL_checkinteger(L, 2));
    const char* desc = (lua_gettop(L) >= 3 && lua_isstring(L, 3)) ? lua_tostring(L, 3) : "";
    bool enabled = lua_gettop(L) >= 4 ? lua_toboolean(L, 4) != 0 : true;

    auto* manager = GetSymbolicBreakpointManager();
    manager->addBreakpoint(sym, offset, desc ? desc : "");
    if (!enabled) {
        auto& bps = manager->getBreakpoints();
        if (!bps.empty()) {
            manager->toggleBreakpoint(bps.size() - 1, false);
        }
    }
    return 0;
}

int lua_RemoveSymbolicBreakpoint(lua_State* L) {
    if (!GetSymbolicBreakpointManager()) {
        return 0;
    }

    size_t index = static_cast<size_t>(luaL_checkinteger(L, 1));
    if (index == 0) {
        return 0;
    }
    GetSymbolicBreakpointManager()->removeBreakpoint(index - 1);
    return 0;
}

int lua_ToggleSymbolicBreakpoint(lua_State* L) {
    if (!GetSymbolicBreakpointManager()) {
        return 0;
    }

    size_t index = static_cast<size_t>(luaL_checkinteger(L, 1));
    bool enabled = lua_toboolean(L, 2) != 0;
    if (index == 0) {
        return 0;
    }
    GetSymbolicBreakpointManager()->toggleBreakpoint(index - 1, enabled);
    return 0;
}

int lua_SaveSymbolicBreakpoints(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);

    // Use non-allocating getter to avoid creating empty manager during shutdown
    SymbolicBreakpointManager* manager = TryGetSymbolicBreakpointManager();
    if (!manager) {
        lua_pushboolean(L, 0); // False - manager not initialized
        lua_pushstring(L, "Symbolic breakpoint manager not initialized");
        return 2;
    }

    // Check if there are any breakpoints to save
    const size_t bp_count = manager->getBreakpoints().size();
    if (bp_count == 0) {
        lua_pushboolean(L, 0); // False - no breakpoints to save
        lua_pushstring(L, "No breakpoints to save");
        return 2;
    }

    // Attempt to save
    manager->saveToFile(filename);

    // Return success with count information
    lua_pushboolean(L, 1); // True - saved successfully
    std::string success_msg = "Saved " + std::to_string(bp_count) + " breakpoints successfully";
    lua_pushstring(L, success_msg.c_str());
    return 2;
}

int lua_LoadSymbolicBreakpoints(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);

    // Ensure manager exists using singleton access
    SymbolicBreakpointManager* manager = GetSymbolicBreakpointManager();

    // Attempt to load
    manager->loadFromFile(filename);

    // Return count of loaded breakpoints
    lua_pushinteger(L, static_cast<lua_Integer>(manager->getBreakpoints().size()));
    return 1;
}

// Helper functions for Lua table creation
void PushSymbolToLua(lua_State* L, const Symbol& symbol) {
    lua_newtable(L);
    
    lua_pushinteger(L, static_cast<lua_Integer>(symbol.address));
    lua_setfield(L, -2, "address");
    
    lua_pushstring(L, symbol.name.c_str());
    lua_setfield(L, -2, "name");
    
    lua_pushstring(L, symbol.module.c_str());
    lua_setfield(L, -2, "module");
    
    lua_pushstring(L, symbol.type.c_str());
    lua_setfield(L, -2, "type");
    
    lua_pushinteger(L, static_cast<lua_Integer>(symbol.size));
    lua_setfield(L, -2, "size");
    
    lua_pushinteger(L, symbol.segment);
    lua_setfield(L, -2, "segment");
    
    lua_pushinteger(L, symbol.offset);
    lua_setfield(L, -2, "offset");
    
    lua_pushinteger(L, static_cast<lua_Integer>(symbol.file_address));
    lua_setfield(L, -2, "file_address");
    
    lua_pushstring(L, symbol.segment_name.c_str());
    lua_setfield(L, -2, "segment_name");

    if (!symbol.sourceline.empty()) {
        lua_pushstring(L, symbol.sourceline.c_str());
        lua_setfield(L, -2, "sourceline");
    }
}

void PushSegmentMappingToLua(lua_State* L, const SegmentMapping& mapping) {
    lua_newtable(L);
    
    lua_pushstring(L, mapping.segment_name.c_str());
    lua_setfield(L, -2, "segment_name");
    
    lua_pushinteger(L, mapping.file_segment);
    lua_setfield(L, -2, "file_segment");
    
    lua_pushinteger(L, static_cast<lua_Integer>(mapping.memory_base));
    lua_setfield(L, -2, "memory_base");
    
    lua_pushinteger(L, static_cast<lua_Integer>(mapping.size));
    lua_setfield(L, -2, "size");
    
    lua_pushboolean(L, mapping.enabled);
    lua_setfield(L, -2, "enabled");
}

Symbol GetSymbolFromLua(lua_State* L, int index) {
    Symbol symbol;
    
    if (lua_type(L, index) != LUA_TTABLE) {
        return symbol;
    }
    
    lua_getfield(L, index, "address");
    if (lua_isnumber(L, -1)) {
        symbol.address = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "name");
    if (lua_isstring(L, -1)) {
        symbol.name = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "module");
    if (lua_isstring(L, -1)) {
        symbol.module = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "type");
    if (lua_isstring(L, -1)) {
        symbol.type = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "size");
    if (lua_isnumber(L, -1)) {
        symbol.size = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "segment");
    if (lua_isnumber(L, -1)) {
        symbol.segment = static_cast<uint16_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "offset");
    if (lua_isnumber(L, -1)) {
        symbol.offset = static_cast<uint16_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "file_address");
    if (lua_isnumber(L, -1)) {
        symbol.file_address = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "segment_name");
    if (lua_isstring(L, -1)) {
        symbol.segment_name = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, index, "sourceline");
    if (lua_isstring(L, -1)) {
        symbol.sourceline = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    return symbol;
}

SegmentMapping GetSegmentMappingFromLua(lua_State* L, int index) {
    SegmentMapping mapping;
    
    if (lua_type(L, index) != LUA_TTABLE) {
        return mapping;
    }
    
    lua_getfield(L, index, "segment_name");
    if (lua_isstring(L, -1)) {
        mapping.segment_name = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "file_segment");
    if (lua_isnumber(L, -1)) {
        mapping.file_segment = static_cast<uint16_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "memory_base");
    if (lua_isnumber(L, -1)) {
        mapping.memory_base = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "size");
    if (lua_isnumber(L, -1)) {
        mapping.size = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    
    lua_getfield(L, index, "enabled");
    if (lua_isboolean(L, -1)) {
        mapping.enabled = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    
    return mapping;
}

} // namespace LuaEngineSymbols
