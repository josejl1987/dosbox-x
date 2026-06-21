// CPU API Registration for DOSBox-X Lua Engine
// Separated from main LuaEngine.cpp for better organization

#include "luaengine.h"

// Required includes for CPU operations
#include "regs.h"        // For register access macros
#include "cpu.h"         // For SegValue and CPU state functions
#include "logging.h"     // For DEBUG_ShowMsg
#include "debug_bridge.h"

#ifdef C_LUA

void LuaEngine::registerCpuAPI() {
    auto cpu_table = lua.create_table();

    // Optimized register access functions (no lambda captures for better performance)
    cpu_table["get_ax"] = []() -> uint16_t { return reg_ax; };
    cpu_table["get_bx"] = []() -> uint16_t { return reg_bx; };
    cpu_table["get_cx"] = []() -> uint16_t { return reg_cx; };
    cpu_table["get_dx"] = []() -> uint16_t { return reg_dx; };
    cpu_table["get_si"] = []() -> uint16_t { return reg_si; };
    cpu_table["get_di"] = []() -> uint16_t { return reg_di; };
    cpu_table["get_sp"] = []() -> uint16_t { return reg_sp; };
    cpu_table["get_bp"] = []() -> uint16_t { return reg_bp; };
    cpu_table["get_ip"] = []() -> uint16_t { return reg_ip; };
    cpu_table["get_al"] = []() -> uint8_t { return reg_al; };
    cpu_table["get_ah"] = []() -> uint8_t { return reg_ah; };
    cpu_table["get_bl"] = []() -> uint8_t { return reg_bl; };
    cpu_table["get_bh"] = []() -> uint8_t { return reg_bh; };
    cpu_table["get_cl"] = []() -> uint8_t { return reg_cl; };
    cpu_table["get_ch"] = []() -> uint8_t { return reg_ch; };
    cpu_table["get_dl"] = []() -> uint8_t { return reg_dl; };
    cpu_table["get_dh"] = []() -> uint8_t { return reg_dh; };
    cpu_table["get_cs"] = []() -> uint16_t { return SegValue(::cs); };
    cpu_table["get_ds"] = []() -> uint16_t { return SegValue(::ds); };
    cpu_table["get_es"] = []() -> uint16_t { return SegValue(::es); };
    cpu_table["get_ss"] = []() -> uint16_t { return SegValue(::ss); };
    cpu_table["get_fs"] = []() -> uint16_t { return SegValue(::fs); };
    cpu_table["get_gs"] = []() -> uint16_t { return SegValue(::gs); };
    cpu_table["set_ax"] = [](uint16_t value) { reg_ax = value; };
    cpu_table["set_bx"] = [](uint16_t value) { reg_bx = value; };
    cpu_table["set_cx"] = [](uint16_t value) { reg_cx = value; };
    cpu_table["set_dx"] = [](uint16_t value) { reg_dx = value; };
    cpu_table["set_si"] = [](uint16_t value) { reg_si = value; };
    cpu_table["set_di"] = [](uint16_t value) { reg_di = value; };
    cpu_table["set_sp"] = [](uint16_t value) { reg_sp = value; };
    cpu_table["set_bp"] = [](uint16_t value) { reg_bp = value; };
    cpu_table["set_ip"] = [](uint16_t value) { reg_ip = value; };
    cpu_table["set_al"] = [](uint8_t value) { reg_al = value; };
    cpu_table["set_ah"] = [](uint8_t value) { reg_ah = value; };
    cpu_table["set_bl"] = [](uint8_t value) { reg_bl = value; };
    cpu_table["set_bh"] = [](uint8_t value) { reg_bh = value; };
    cpu_table["set_cl"] = [](uint8_t value) { reg_cl = value; };
    cpu_table["set_ch"] = [](uint8_t value) { reg_ch = value; };
    cpu_table["set_dl"] = [](uint8_t value) { reg_dl = value; };
    cpu_table["set_dh"] = [](uint8_t value) { reg_dh = value; };
    cpu_table["get_flags"] = []() -> uint32_t { return reg_flags; };
    cpu_table["set_flags"] = [](uint32_t value) { reg_flags = value; };

    // Batch CPU state access functions for TAS tools
    cpu_table["get_state"] = [this]() -> sol::table {
        if(sol2_cache.fast_cpu_access) {
            // Fast path: reuse cached table
            sol2_cache.cached_cpu_state.clear();
            sol2_cache.cached_cpu_state["ax"] = reg_ax;
            sol2_cache.cached_cpu_state["bx"] = reg_bx;
            sol2_cache.cached_cpu_state["cx"] = reg_cx;
            sol2_cache.cached_cpu_state["dx"] = reg_dx;
            sol2_cache.cached_cpu_state["si"] = reg_si;
            sol2_cache.cached_cpu_state["di"] = reg_di;
            sol2_cache.cached_cpu_state["sp"] = reg_sp;
            sol2_cache.cached_cpu_state["bp"] = reg_bp;
            sol2_cache.cached_cpu_state["ip"] = reg_ip;
            sol2_cache.cached_cpu_state["cs"] = SegValue(::cs);
            sol2_cache.cached_cpu_state["ds"] = SegValue(::ds);
            sol2_cache.cached_cpu_state["es"] = SegValue(::es);
            sol2_cache.cached_cpu_state["ss"] = SegValue(::ss);
            sol2_cache.cached_cpu_state["flags"] = reg_flags;
            return sol2_cache.cached_cpu_state;
        }
        else {
            // Standard path: create new table
            auto result = lua.create_table();
            result["ax"] = reg_ax;
            result["bx"] = reg_bx;
            result["cx"] = reg_cx;
            result["dx"] = reg_dx;
            result["si"] = reg_si;
            result["di"] = reg_di;
            result["sp"] = reg_sp;
            result["bp"] = reg_bp;
            result["ip"] = reg_ip;
            result["cs"] = SegValue(::cs);
            result["ds"] = SegValue(::ds);
            result["es"] = SegValue(::es);
            result["ss"] = SegValue(::ss);
            result["flags"] = reg_flags;
            return result;
        }
        };

    // Performance control
    cpu_table["set_fast_mode"] = [this](bool enable) {
        sol2_cache.fast_cpu_access = enable;
        DEBUG_ShowMsg("LuaEngine: CPU fast mode %s", enable ? "enabled" : "disabled");
        };

    lua["cpu"] = cpu_table;
    luaEngine.log_info("CPU API registered with register access and batch operations");
}
#endif
