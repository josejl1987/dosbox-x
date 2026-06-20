/*
 *  Copyright (C) 2026  DOSBox-X authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DOSBOX_LUA_RE_HOOKS_H
#define DOSBOX_LUA_RE_HOOKS_H

#include <config.h>

#ifdef C_LUA_RE_HOOKS

#include <cstdint>

struct LuaEngine;

namespace LuaReHooks {

void SetEngine(LuaEngine* engine);
void RegisterLuaReHooksAPI(LuaEngine& engine);

void OnFileReadComplete(uint16_t entry, const char* filename, uint32_t fileOffset,
                         uint16_t requested, uint16_t actual, bool success);

void OnFileWriteComplete(uint16_t entry, const char* filename, uint32_t fileOffset,
                          uint16_t requested, uint16_t actual, bool success);

void OnFileSeekComplete(uint16_t entry, const char* filename, uint32_t position, bool success);

void OnFileOpenComplete(uint16_t entry, const char* filename, bool success);

void OnFileClose(uint16_t entry, const char* filename, uint8_t refCount);

// CPU interrupt notification (fired from CPU_Interrupt)
void OnDOSInterrupt(uint8_t num, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx,
                    uint16_t cs, uint16_t ds, uint16_t es, uint16_t ss,
                    uint16_t return_ip);

// Frame boundary notification (fired from LuaEngine::LuaFrameBoundary)
void OnFrame();

// Emulator shutdown notification (fired before Lua state is closed)
void OnExit();

// Save CDL to the registered finalize path immediately (can be called from
// shutdown paths that bypass Lua destructors and atexit handlers).
void SaveCDLFinalize();

} // namespace LuaReHooks

#endif // C_LUA_RE_HOOKS

#endif // DOSBOX_LUA_RE_HOOKS_H
