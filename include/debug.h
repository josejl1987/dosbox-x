#pragma  once
#include <mem.h>
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





enum EBreakpoint { BKPNT_UNKNOWN, BKPNT_PHYSICAL, BKPNT_INTERRUPT, BKPNT_MEMORY, BKPNT_MEMORY_PROT, BKPNT_MEMORY_LINEAR };

#define BPINT_ALL 0x100

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

    static std::list<CBreakpoint*>	BPoints;
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
