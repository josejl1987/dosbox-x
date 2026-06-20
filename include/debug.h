#pragma  once
#include <mem.h>
#include <list>
#include <unordered_map>
#include <vector>
#include <atomic>
/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */



void DEBUG_SetupConsole(void);
void DEBUG_DrawScreen(void);
bool DEBUG_Breakpoint(void);
bool DEBUG_IntBreakpoint(uint8_t intNum);
void DEBUG_Enable(bool pressed);
void DEBUG_CheckExecuteBreakpoint(uint16_t seg, uint32_t off);
bool DEBUG_ExitLoop(void);
void DEBUG_RefreshPage(char scroll);
Bitu DEBUG_EnableDebugger(void);
void DEBUG_ShowMsg(char const* format, ...);

// Debug state variables
extern bool exitLoop;

// Debug variables - available in all builds for Lua engine compatibility
extern bool debugging;
extern bool debug_running;

// Debug execution functions
#if C_DEBUG
int32_t DEBUG_Run(int32_t amount, bool quickexit);
#endif
void DEBUG_RunLua(int count, bool usebreakpoint);





enum EBreakpoint { BKPNT_UNKNOWN, BKPNT_PHYSICAL, BKPNT_INTERRUPT, BKPNT_MEMORY, BKPNT_MEMORY_PROT, BKPNT_MEMORY_LINEAR, BKPNT_MEMORY_FREEZE };

#define BPINT_ALL 0x100

// Fast hash function for PhysPt addresses
struct PhysPtHash {
    std::size_t operator()(const PhysPt& key) const noexcept {
        // Use Thomas Wang's 64-bit mix function - very fast and good distribution
        uint64_t k = static_cast<uint64_t>(key);
        k = (~k) + (k << 21); // k = k * 2^21 + (~k)
        k = k ^ (k >> 24);
        k = (k + (k << 3)) + (k << 8); // k = k * 2^8 + k * 2^3
        k = k ^ (k >> 14);
        k = (k + (k << 2)) + (k << 4); // k = k * 2^4 + k * 2^2
        k = k ^ (k >> 28);
        k = k + (k << 31);
        return static_cast<std::size_t>(k);
    }
};

class CBreakpoint
{
public:

    CBreakpoint(void);
    void					SetAddress(uint16_t seg, uint32_t off);;
    void					SetAddress(PhysPt adr) { location = adr; type = BKPNT_PHYSICAL; };
    void					SetInt(uint8_t _intNr, uint16_t ah, uint16_t al);;
    void					SetOnce(bool _once) { once = _once; };
    void					SetType(EBreakpoint _type) { type = _type; };
    void					SetValue(uint8_t value) { ahValue = value; };
    void					SetOther(uint8_t other) { alValue = other; };

    bool					IsActive(void) { return active; };
    void					Activate(bool _active);

    EBreakpoint				GetType(void) { return type; };
    bool					GetOnce(void) { return once; };
    PhysPt					GetLocation(void) { return location; };
    uint16_t					GetSegment(void) { return segment; };
    uint32_t					GetOffset(void) { return offset; };
    uint8_t					GetIntNr(void) { return intNr; };
    uint16_t					GetValue(void) { return ahValue; };
    uint16_t					GetOther(void) { return alValue; };

    // statics
    static CBreakpoint* AddBreakpoint(uint16_t seg, uint32_t off, bool once);
    static CBreakpoint* AddIntBreakpoint(uint8_t intNum, uint16_t ah, uint16_t al, bool once);
    static CBreakpoint* AddMemBreakpoint(uint16_t seg, uint32_t off);
    static void				DeactivateBreakpoints();
    static void				ActivateBreakpoints();
    static void				ActivateBreakpointsExceptAt(PhysPt adr);
    static bool				CheckBreakpoint(uint16_t seg, uint32_t off);
    static bool				CheckIntBreakpoint(PhysPt adr, uint8_t intNr, uint16_t ahValue, uint16_t alValue);
    static CBreakpoint* FindPhysBreakpoint(uint16_t seg, uint32_t off, bool once);
    static CBreakpoint* FindOtherActiveBreakpoint(PhysPt adr, CBreakpoint* skip);
    static bool				IsBreakpoint(uint16_t seg, uint32_t off);
    static bool				DeleteBreakpoint(uint16_t seg, uint32_t off);
    static bool				DeleteByIndex(uint16_t index);
    static void				DeleteAll(void);
    static void				ShowList(void);

    // OPTIMIZATION: Fast mode management functions
    static void				EnableFastMode(bool enable = true);
    static bool				IsFastModeEnabled() { return fast_mode_enabled; }

    // OPTIMIZATION: Fast breakpoint count management (relaxed atomic operations)
    static void				IncrementBreakpointCount() { active_breakpoint_count.fetch_add(1, std::memory_order_relaxed); }
    static void				DecrementBreakpointCount() { active_breakpoint_count.fetch_sub(1, std::memory_order_relaxed); }

    // OPTIMIZATION: Global debugging state for fast-path rejection
    static volatile bool debugging_active;


private:
    EBreakpoint	type;
    // Physical
    PhysPt		location;
#if !C_HEAVY_DEBUG
    uint8_t		oldData;
#endif
    uint16_t		segment;
    uint32_t		offset;
    // Int
    uint8_t		intNr;
    uint16_t		ahValue;
    uint16_t		alValue;
    // Shared
    bool		active;
    bool		once;

    static std::vector<CBreakpoint*>	BPoints;
    // OPTIMIZATION: Separate vector for memory breakpoints to avoid iterating over all breakpoints
    static std::vector<CBreakpoint*> memoryBreakpoints;
    // OPTIMIZATION: Hash map with fast hash for O(1) breakpoint lookup
    static std::unordered_map<PhysPt, CBreakpoint*, PhysPtHash> BPointsByLocation;
    // OPTIMIZATION: Atomic counter for fast-path check
    static std::atomic<size_t> active_breakpoint_count;
    // OPTIMIZATION: Page bitmap for fast rejection of non-breakpoint pages
    static std::vector<uint8_t> breakpoint_pages;
    static const size_t PAGE_SHIFT = 12; // 4KB pages
    // OPTIMIZATION: Fast mode direct lookup table for real-mode addresses
    static std::vector<CBreakpoint*> fast_breakpoint_table;
    static bool fast_mode_enabled;
    static void UpdatePageBitmap(PhysPt addr, bool add);
    static void RebuildPageBitmap();
#if C_HEAVY_DEBUG
    friend bool DEBUG_HeavyIsBreakpoint(void);
#endif
};



extern Bitu cycle_count;
extern Bitu debugCallback;
void DEBUG_Enable_Handler(bool pressed);

#ifdef C_HEAVY_DEBUG
bool DEBUG_HeavyIsBreakpoint(void);
void DEBUG_HeavyWriteLogInstruction(void);
#endif

uint64_t GetAddress(uint16_t seg, uint32_t offset);
