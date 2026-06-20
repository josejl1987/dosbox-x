/*
 *  Copyright (C) 2026  DOSBox-X authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Native reverse-engineering hooks exposed to Lua. These are called from
 *  the DOSBox-X DOS file layer and run at full native speed.
 */

#include "lua_re_hooks.h"

#ifdef C_LUA_RE_HOOKS

#include "luaengine.h"
#include "regs.h"
#include "debug.h"
#include "mem_capture.h"
#include "pc98_cdl.h"
#include <sol/sol.hpp>
#include <mutex>
#include <cstdlib>

namespace LuaReHooks {

static LuaEngine* s_engine = nullptr;
static std::recursive_mutex s_hooks_mutex;

// Path used for a guaranteed save at process exit (independent of destructors).
static std::string s_cdl_finalize_path;
static bool s_cdl_atexit_registered = false;

void SaveCDLFinalize() {
    if (s_cdl_finalize_path.empty()) return;
    PC98CDL::GetCDL().save(s_cdl_finalize_path);
}

static void CDLFinalizeAtExit() {
    SaveCDLFinalize();
}

void SetEngine(LuaEngine* engine) {
    s_engine = engine;
}

static sol::table ensure_callbacks_table(const std::string& event_name) {
    if (!s_engine) return sol::nil;
    sol::state& lua = s_engine->lua;

    sol::object hooks_obj = lua["re_hooks_callbacks"];
    sol::table hooks;
    if (!hooks_obj.valid() || hooks_obj.get_type() != sol::type::table) {
        hooks = lua.create_table();
        lua["re_hooks_callbacks"] = hooks;
    } else {
        hooks = hooks_obj.as<sol::table>();
    }

    sol::object event_obj = hooks[event_name];
    sol::table event_table;
    if (!event_obj.valid() || event_obj.get_type() != sol::type::table) {
        event_table = lua.create_table();
        hooks[event_name] = event_table;
    } else {
        event_table = event_obj.as<sol::table>();
    }
    return event_table;
}

static void fire_event(const std::string& event_name, sol::table& event) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;

    std::lock_guard<std::recursive_mutex> lock(s_hooks_mutex);
    sol::table callbacks = ensure_callbacks_table(event_name);
    if (!callbacks.valid()) return;

    // Iterate numeric indices and call each registered function.
    for (size_t i = 1;; ++i) {
        sol::protected_function cb = callbacks[i];
        if (!cb.valid()) break;
        auto result = cb(event);
        if (!result.valid()) {
            sol::error err = result;
            s_engine->log_warning(std::string("re hook callback error: ") + err.what());
        }
    }
}

static sol::table make_event_table(const std::string& type) {
    sol::table ev = s_engine->lua.create_table();
    ev["type"] = type;
    ev["caller_cs"] = static_cast<int>(SegValue(cs));
    ev["caller_ip"] = static_cast<int>(reg_ip);
    return ev;
}

void OnFileReadComplete(uint16_t entry, const char* filename, uint32_t fileOffset,
                         uint16_t requested, uint16_t actual, bool success) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;
    sol::table ev = make_event_table("file_read_complete");
    ev["handle"] = entry;
    ev["filename"] = filename ? filename : "";
    ev["file_offset"] = static_cast<uint32_t>(fileOffset);
    ev["requested"] = requested;
    ev["actual"] = actual;
    ev["destination_segment"] = static_cast<int>(SegValue(ds));
    ev["destination_offset"] = static_cast<int>(reg_dx);
    ev["success"] = success;
    fire_event("file_read_complete", ev);
}

void OnFileWriteComplete(uint16_t entry, const char* filename, uint32_t fileOffset,
                          uint16_t requested, uint16_t actual, bool success) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;
    sol::table ev = make_event_table("file_write_complete");
    ev["handle"] = entry;
    ev["filename"] = filename ? filename : "";
    ev["file_offset"] = static_cast<uint32_t>(fileOffset);
    ev["requested"] = requested;
    ev["actual"] = actual;
    ev["success"] = success;
    fire_event("file_write_complete", ev);
}

void OnFileSeekComplete(uint16_t entry, const char* filename, uint32_t position, bool success) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;
    sol::table ev = make_event_table("file_seek_complete");
    ev["handle"] = entry;
    ev["filename"] = filename ? filename : "";
    ev["position"] = static_cast<uint32_t>(position);
    ev["success"] = success;
    fire_event("file_seek_complete", ev);
}

void OnFileOpenComplete(uint16_t entry, const char* filename, bool success) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;
    sol::table ev = make_event_table("file_open_complete");
    ev["handle"] = entry;
    ev["filename"] = filename ? filename : "";
    ev["success"] = success;
    fire_event("file_open_complete", ev);
}

void OnFileClose(uint16_t entry, const char* filename, uint8_t refCount) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;
    sol::table ev = make_event_table("file_close");
    ev["handle"] = entry;
    ev["filename"] = filename ? filename : "";
    ev["ref_count"] = refCount;
    fire_event("file_close", ev);
}

void OnDOSInterrupt(uint8_t num, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx,
                    uint16_t cs, uint16_t ds, uint16_t es, uint16_t ss,
                    uint16_t return_ip) {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;

    sol::table ev = s_engine->lua.create_table();
    ev["type"] = "interrupt";
    ev["num"] = num;
    ev["ax"] = ax;
    ev["bx"] = bx;
    ev["cx"] = cx;
    ev["dx"] = dx;
    ev["cs"] = cs;
    ev["ds"] = ds;
    ev["es"] = es;
    ev["ss"] = ss;
    ev["ip"] = return_ip;
    ev["return_cs"] = cs;
    ev["return_ip"] = return_ip;
    fire_event("interrupt", ev);
}

void OnFrame() {
    if (!s_engine) return;
    if (s_engine->luaRunning.load(std::memory_order_relaxed) <= 0) return;

    sol::table ev = s_engine->lua.create_table();
    ev["type"] = "frame";
    fire_event("frame", ev);
}

void OnExit() {
    if (!s_engine) return;
    // Do not check luaRunning here; shutdown may already have set it to 0,
    // but the Lua state is still valid for one final callback.
    sol::table ev = s_engine->lua.create_table();
    ev["type"] = "exit";
    fire_event("exit", ev);
}

// Lua-facing registration API.
//
// re.on_file_read_complete(function(ev) ... end)
// re.capture_begin(seg, off, length)
// re.capture_end() -> array of {start_linear, length}
// re.dump_range(seg, off, length) -> byte vector

static int re_hook_register(const char* event_name, sol::protected_function fn) {
    if (!s_engine) return 0;
    std::lock_guard<std::recursive_mutex> lock(s_hooks_mutex);
    sol::table callbacks = ensure_callbacks_table(event_name);
    if (!callbacks.valid()) return 0;

    size_t idx = 1;
    while (callbacks[idx].valid()) ++idx;
    callbacks[idx] = fn;
    return static_cast<int>(idx);
}

static bool re_hook_unregister(const char* event_name, int id) {
    if (!s_engine) return false;
    std::lock_guard<std::recursive_mutex> lock(s_hooks_mutex);
    sol::table callbacks = ensure_callbacks_table(event_name);
    if (!callbacks.valid()) return false;
    callbacks[id] = sol::nil;
    return true;
}

void RegisterLuaReHooksAPI(LuaEngine& engine) {
    sol::state& lua = engine.lua;
    sol::table re = lua.create_named_table("re");

    re["on_file_read_complete"] = [](sol::protected_function fn) { return re_hook_register("file_read_complete", fn); };
    re["on_file_write_complete"] = [](sol::protected_function fn) { return re_hook_register("file_write_complete", fn); };
    re["on_file_seek_complete"] = [](sol::protected_function fn) { return re_hook_register("file_seek_complete", fn); };
    re["on_file_open_complete"] = [](sol::protected_function fn) { return re_hook_register("file_open_complete", fn); };
    re["on_file_close"] = [](sol::protected_function fn) { return re_hook_register("file_close", fn); };
    re["on_interrupt"] = [](sol::protected_function fn) { return re_hook_register("interrupt", fn); };
    re["on_frame"] = [](sol::protected_function fn) { return re_hook_register("frame", fn); };
    re["on_exit"] = [](sol::protected_function fn) { return re_hook_register("exit", fn); };

    re["set_cdl_finalize_path"] = [](const std::string& path) {
        s_cdl_finalize_path = path;
        FILE* reg = fopen("tools/pc98rev/incoming/atexit_registered.txt", "w");
        if (reg) { fprintf(reg, "registered path='%s'\n", path.c_str()); fclose(reg); }
        if (!s_cdl_atexit_registered) {
            std::atexit(CDLFinalizeAtExit);
            s_cdl_atexit_registered = true;
        }
    };

    re["unregister"] = [](const std::string& event_name, int id) {
        return re_hook_unregister(event_name.c_str(), id);
    };

    re["capture_begin"] = [](uint16_t seg, uint32_t off, uint32_t length) -> uint32_t {
        uint32_t linear = (static_cast<uint32_t>(seg) << 4u) + off;
        return MemoryCapture::begin_write_capture(linear, length);
    };

    re["capture_end"] = [](uint32_t id) -> sol::table {
        auto ranges = MemoryCapture::end_write_capture(id);
        if (!s_engine) return sol::nil;
        sol::state& lua = s_engine->lua;
        sol::table result = lua.create_table();
        for (size_t i = 0; i < ranges.size(); ++i) {
            sol::table entry = lua.create_table();
            entry["start_linear"] = ranges[i].start_linear;
            entry["start_segment"] = ranges[i].start_linear >> 4;
            entry["start_offset"] = ranges[i].start_linear & 0xF;
            entry["length"] = ranges[i].length;
            result[i + 1] = entry;
        }
        return result;
    };

    re["dump_range"] = [](uint16_t seg, uint32_t off, uint32_t length) -> sol::table {
        if (!s_engine) return sol::nil;
        uint32_t linear = (static_cast<uint32_t>(seg) << 4u) + off;
        std::vector<uint8_t> bytes = MemoryCapture::dump_memory_range(linear, length);
        sol::state& lua = s_engine->lua;
        sol::table result = lua.create_table();
        for (size_t i = 0; i < bytes.size(); ++i) {
            result[i + 1] = bytes[i];
        }
        return result;
    };

    SetEngine(&engine);
}

} // namespace LuaReHooks

void LuaEngine::registerReHooksAPI() {
#ifdef C_LUA_RE_HOOKS
    LuaReHooks::RegisterLuaReHooksAPI(*this);
#endif
}

void LuaEngine::setRunUntilTarget(uint16_t seg, uint32_t off, sol::protected_function callback) {
#ifdef C_LUA_RE_HOOKS
    std::lock_guard<std::mutex> lock(run_until_mutex_);
    run_until_target_ = (static_cast<uint32_t>(seg) << 4u) + off;
    run_until_callback_ = callback;
    enableInstructionHooks(true);
#else
    (void)seg;
    (void)off;
    (void)callback;
#endif
}

void LuaEngine::clearRunUntilTarget() {
#ifdef C_LUA_RE_HOOKS
    std::lock_guard<std::mutex> lock(run_until_mutex_);
    run_until_target_ = 0;
    run_until_callback_ = sol::protected_function();
#endif
}

#endif // C_LUA_RE_HOOKS
