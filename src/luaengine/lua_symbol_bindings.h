#ifndef LUA_SYMBOL_BINDINGS_H
#define LUA_SYMBOL_BINDINGS_H

#include "lua.hpp"

namespace LuaEngineSymbols {

// Forward declarations
struct Symbol;
struct SegmentMapping;

// Register all symbol-related Lua functions
void RegisterSymbolBindings(lua_State* L);

// Symbol management functions
int lua_LoadSymbolFile(lua_State* L);
int lua_ClearSymbols(lua_State* L);
int lua_HasSymbols(lua_State* L);
int lua_GetSymbolCount(lua_State* L);
int lua_GetLoadedFileName(lua_State* L);
int lua_GetLoadedFormat(lua_State* L);

// Symbol lookup functions
int lua_GetSymbolName(lua_State* L);
int lua_GetSymbolAddress(lua_State* L);
int lua_FindSymbol(lua_State* L);
int lua_GetAllSymbols(lua_State* L);
int lua_GetSymbolsInRange(lua_State* L);
int lua_GetSymbolNames(lua_State* L);

// Segment mapping functions
int lua_AddSegmentMapping(lua_State* L);
int lua_RemoveSegmentMapping(lua_State* L);
int lua_UpdateSegmentMapping(lua_State* L);
int lua_EnableSegmentMapping(lua_State* L);
int lua_GetSegmentMappings(lua_State* L);
int lua_FindSegmentMapping(lua_State* L);
int lua_ClearSegmentMappings(lua_State* L);
int lua_AutoDetectSegments(lua_State* L);
int lua_RemapAllSymbols(lua_State* L);

// Address conversion functions
int lua_SegmentOffsetToLinear(lua_State* L);
int lua_LinearToSegmentOffset(lua_State* L);
int lua_MapAddressToMemory(lua_State* L);

// Utility functions
int lua_AddSymbol(lua_State* L);
int lua_AddSymbolsBulk(lua_State* L);
int lua_RemoveSymbol(lua_State* L);

// Symbolic breakpoint helpers
int lua_GetSymbolicBreakpoints(lua_State* L);
int lua_AddSymbolicBreakpoint(lua_State* L);
int lua_RemoveSymbolicBreakpoint(lua_State* L);
int lua_ToggleSymbolicBreakpoint(lua_State* L);
int lua_SaveSymbolicBreakpoints(lua_State* L);
int lua_LoadSymbolicBreakpoints(lua_State* L);

// Helper functions for Lua table creation
void PushSymbolToLua(lua_State* L, const Symbol& symbol);
void PushSegmentMappingToLua(lua_State* L, const SegmentMapping& mapping);
Symbol GetSymbolFromLua(lua_State* L, int index);
SegmentMapping GetSegmentMappingFromLua(lua_State* L, int index);

} // namespace LuaEngineSymbols

#endif // LUA_SYMBOL_BINDINGS_H
