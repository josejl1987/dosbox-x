#include "luaengine.h"
#include <debug.h>
#include <regs.h>
#include <paging.h>







// Custom error handling function
int customErrorHandler(lua_State* L) {
    const char* error_message = lua_tostring(L, -1);
    fprintf(stderr, "Lua Error: %s\n", error_message);
    return 1; // Return an error value
}


static int emu_frameadvance2(lua_State* L) {
    // We're going to sleep for a frame-advance. Take notes.

    //if(frameAdvanceWaiting)
    //    return luaL_error(L, "can't call emu.frameadvance() from here");

    //frameAdvanceWaiting = true;

    // Now we can yield to the main
    return lua_yield(L, 0);


    // It's actually rather disappointing...
}



int LuaEngine::LoadCode(const char* filename, const char* arg)
{

    if(filename != luaScriptName)
    {
        if(luaScriptName) free(luaScriptName);
        luaScriptName = strdup(filename);
    }

    std::string getfilepath = filename;

    getfilepath = getfilepath.substr(0, getfilepath.find_last_of("/\\") + 1);

    //if(SetCurrentDir(getfilepath.c_str()) != 0)
    //{
    //}

    ////stop any lua we might already have had running
    //FCEU_LuaStop();

    //Reinit the error count
    luaexiterrorcount = 8;

    lua.open_libraries(sol::lib::base, sol::lib::coroutine);

    lua.set_function(frameAdvanceThread, &LuaEngine::emu_frameadvance, this);
    auto debugTable = lua.create_named_table("debugger");
    debugTable.set_function("add_breakpoint", &LuaEngine::debug_addbreakpoint, this);
    debugTable.set_function("add_logpoint", &LuaEngine::debug_addlogpoint, this);
    debugTable.set_function("remove_breakpoint", &LuaEngine::debug_removebreakpoint, this);
    debugTable.set_function("get_register_value", &LuaEngine::debug_getregistervalue, this);
    debugTable.set_function("add_mem_breakpoint", &LuaEngine::debug_addmembreakpoint, this);
    debugTable.set_function("enable_debugger", &LuaEngine::debug_enabledebugger, this);


    lua.script_file("hello.lua");
    loop_coroutine = lua["loop"];
    loop_coroutine();
    lua["counter"] = 20;






    // Save the thread to the registry. This is why I make the thread FIRST.


    // Initialize settings
    luaRunning = 1;
    skipRerecords = 0;
    numMemHooks = 0;
    transparencyModifier = 255; // opaque







    LuaFrameBoundary();


    return 1;
}

// emu.frameadvance()
//
//  Executes a frame advance. Occurs by yielding the coroutine, then re-running
//  when we break out.
int LuaEngine::emu_frameadvance() {
    // We're going to sleep for a frame-advance. Take notes.



    frameAdvanceWaiting = true;

    // Now we can yield to the main

    return 1;
    // It's actually rather disappointing...
}

extern CPU_Regs cpu_regs;

uint32_t LuaEngine::debug_getregistervalue(const X86Registers& reg)
{

    // Function to access register values

    switch(reg) {
    case X86Registers::REGI_AL:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0];
    case X86Registers::REGI_AH:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1];
    case X86Registers::REGI_AX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].word[0];
    case X86Registers::REGI_EAX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].dword[0];
    case X86Registers::REGI_BL:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[0];
    case X86Registers::REGI_BH:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[1];
    case X86Registers::REGI_BX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].word[0];
    case X86Registers::REGI_EBX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].dword[0];
    case X86Registers::REGI_CL:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[0];
    case X86Registers::REGI_CH:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[1];
    case X86Registers::REGI_CX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].word[0];
    case X86Registers::REGI_ECX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].dword[0];
    case X86Registers::REGI_DL:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0];
    case X86Registers::REGI_DH:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1];
    case X86Registers::REGI_DX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].word[0];
    case X86Registers::REGI_EDX:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].dword[0];
    case X86Registers::REGI_SI:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].word[0];
    case X86Registers::REGI_ESI:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].dword[0];
    case X86Registers::REGI_DI:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].word[0];
    case X86Registers::REGI_EDI:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].dword[0];
    case X86Registers::REGI_SP:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].word[0];
    case X86Registers::REGI_ESP:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].dword[0];
    case X86Registers::REGI_BP:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].word[0];
    case X86Registers::REGI_EBP:
        return cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].dword[0];
    case X86Registers::REGI_IP:
        return cpu_regs.ip.word[0];
    case X86Registers::REGI_EIP:
        return cpu_regs.ip.dword[0];
    case X86Registers::REGI_FLAGS:
        return cpu_regs.flags;
    case X86Registers::REGI_CS:
        return SegValue(cs);
    case X86Registers::REGI_DS:
        return SegValue(ds);
    case X86Registers::REGI_ES:
        return SegValue(es);
    default:
        return 0; // Handle invalid enum values here
    }

}

void LuaEngine::debug_addlogpoint(uint16_t seg, uint32_t off, const std::string& name)
{
    LogPoint lp = { seg, off, name };
    this->logpoints.push_back(lp);
}

void LuaEngine::debug_addbreakpoint(uint16_t seg, uint32_t off, bool once)
{
    CBreakpoint::AddBreakpoint(seg, off, once);
}

void LuaEngine::debug_removebreakpoint(uint16_t seg, uint32_t off, bool once)
{
    CBreakpoint::DeleteBreakpoint(seg, off);
}

void LuaEngine::debug_addmembreakpoint(uint16_t seg, uint32_t off)
{
    CBreakpoint::AddMemBreakpoint(seg, off);
}

void LuaEngine::debug_removemembreakpoint(uint16_t seg, uint32_t off)
{
    CBreakpoint::DeleteBreakpoint(seg, off);
}

void LuaEngine::debug_enabledebugger()
{
    DEBUG_Enable_Handler(true);
}



std::string getAddressLabel(int index) {
    static const std::vector<std::string> addressLabels = {
        "BZ_EXEC_LOAD",
        "FUN_3000_1396",
        "FUN_3000_1401",
        "LAB_3000_1418",
        "BZ_LOAD_LIB",
        "NORMAL_LOAD_LIB",
        "UNPACK_CALL",
        "NORMAL_LOAD",
        "NORMAL_SAVE",
        "LAB_3000_03d5",
        "LAB_3000_03f8",
        "CHECK_DISK_NUMBER2",
        "BZ_LOAD_LIB2",
        "LAB_3000_07e7",
        "LAB_3000_0ba1",
        "LAB_3000_0c3d",
        "LAB_3000_0cda",
        "FUN_3000_0f8f",
        "LAB_3000_0d30",
        "LAB_3000_0047"
    };

    if(index >= 0 && index < addressLabels.size()) {
        return addressLabels[index];
    }
    else {
        return "Invalid index";
    }
}

uint8_t GetByte(int seg, int ofs1) {

    uint8_t value;

    mem_readb_checked((PhysPt)GetAddress(seg, ofs1), &value);

    return value;

}


uint16_t GetWord(int seg, int ofs1) {

    uint16_t value;

    mem_readw_checked((PhysPt)GetAddress(seg, ofs1), &value);

    return value;

}


std::vector<char> GetBytes(int seg, int ofs1) {

    std::vector<char> buffer;
    for(uint16_t x = 0; x < 1024; x++) {
        uint8_t value;

        if(!mem_readb_checked((PhysPt)GetAddress(seg, ofs1 + x), &value))
        {
            buffer.push_back(value);

        }
    }
    return buffer;

}

void LuaEngine::debugHook(AddressLabel index) {

    // Define the individual register variables
    int al = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0];
    int ah = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1];
    int ax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].word[0];
    int eax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].dword[0];

    int bl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[0];
    int bh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[1];
    int bx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].word[0];
    int ebx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].dword[0];

    int cl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[0];
    int ch = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[1];
    int cx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].word[0];
    int ecx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].dword[0];

    int dl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0];
    int dh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1];
    int dx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].word[0];
    int edx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].dword[0];

    int si = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].word[0];
    int esi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].dword[0];

    int di = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].word[0];
    int edi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].dword[0];

    int sp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].word[0];
    int esp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].dword[0];

    int bp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].word[0];
    int ebp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].dword[0];

    int ip = cpu_regs.ip.word[0];
    int eip = cpu_regs.ip.dword[0];

    int flags = cpu_regs.flags;

    int cs = this->debug_getregistervalue(X86Registers::REGI_CS);
    int ds = this->debug_getregistervalue(X86Registers::REGI_DS);
    int es = this->debug_getregistervalue(X86Registers::REGI_ES);

    switch(index)
    {
    case BZ_EXEC_LOAD:

    {


        DEBUG_ShowMsg("Normal Load Lib Call BX %02x SI %02x AX %02x DI %02x DX %02x \n", bx, si, ax, di, dx);

        auto buf = GetBytes(ds, di);


    }

    break;
    case FUN_3000_1396:
        break;
    case FUN_3000_1401:
        break;
    case LAB_3000_1418:
        break;
    case BZ_LOAD_LIB:

    {
        DEBUG_ShowMsg("BZ Load Lib Call BX %02x SI %02x AX %02x DI %02x DX %02x CX %02x \n", bx, si, ax, di, dx, cx);
        auto buf = GetBytes(ds, dx);


    }

    break;
    case NORMAL_LOAD_LIB: {


        DEBUG_ShowMsg("Normal Load Lib Call BX %02x SI %02x AX %02x DI %02x DX %02x \n", bx, si, ax, di, dx);

        auto buf = GetBytes(ax, dx + 1);


    }
                        break;
    case UNPACK_CALL:

        DEBUG_ShowMsg("Unpack Call BX %02x SI %02x AX %02x DI %02x DX %02x  \n", bx, si, ax, di, dx);




        break;
    case NORMAL_LOAD: {


        DEBUG_ShowMsg("Normal Load Call BX %02x SI %02x AX %02x DI %02x DX %02x \n", bx, si, ax, di, dx);

        auto buf = GetBytes(ax, dx + 1);

    }
                    break;
    case NORMAL_SAVE:
        break;
    case LAB_3000_03d5:
        break;
    case LAB_3000_03f8:
        break;
    case CHECK_DISK_NUMBER2:
        break;
    case BZ_LOAD_LIB2:
        break;
    case LAB_3000_07e7:
        break;
    case LAB_3000_0ba1:
        break;
    case LAB_3000_0c3d:
        break;
    case LAB_3000_0cda:
        break;
    case FUN_3000_0f8f:
        break;
    case LAB_3000_0d30:
        break;
    case LAB_3000_0047:
        break;
    default:
        break;
    }


}

void LuaEngine::KHDHook() {

    // Define the individual register variables
    int al = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0];
    int ah = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1];
    int ax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].word[0];
    int eax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].dword[0];

    int bl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[0];
    int bh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[1];
    int bx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].word[0];
    int ebx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].dword[0];

    int cl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[0];
    int ch = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[1];
    int cx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].word[0];
    int ecx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].dword[0];

    int dl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0];
    int dh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1];
    int dx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].word[0];
    int edx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].dword[0];

    int si = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].word[0];
    int esi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].dword[0];

    int di = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].word[0];
    int edi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].dword[0];

    int sp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].word[0];
    int esp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].dword[0];

    int bp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].word[0];
    int ebp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].dword[0];

    int ip = cpu_regs.ip.word[0];
    int eip = cpu_regs.ip.dword[0];

    int flags = cpu_regs.flags;

    int cs = this->debug_getregistervalue(X86Registers::REGI_CS);
    int ds = this->debug_getregistervalue(X86Registers::REGI_DS);
    int es = this->debug_getregistervalue(X86Registers::REGI_ES);

    int dest = GetWord(ds, 0x15DD);

    auto khd = GetBytes(ds, dx + 1);
    DEBUG_ShowMsg("Load KHD %s\n", khd.data());
    auto name = GetBytes(ds, si);
    DEBUG_ShowMsg("Load file %s at %04x:%04x\n", name.data(), ds, dest);


}
void LuaEngine::KLBHook() {

    // Define the individual register variables
    int al = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0];
    int ah = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1];
    int ax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].word[0];
    int eax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].dword[0];

    int bl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[0];
    int bh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[1];
    int bx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].word[0];
    int ebx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].dword[0];

    int cl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[0];
    int ch = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[1];
    int cx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].word[0];
    int ecx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].dword[0];

    int dl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0];
    int dh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1];
    int dx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].word[0];
    int edx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].dword[0];

    int si = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].word[0];
    int esi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].dword[0];

    int di = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].word[0];
    int edi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].dword[0];

    int sp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].word[0];
    int esp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].dword[0];

    int bp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].word[0];
    int ebp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].dword[0];

    int ip = cpu_regs.ip.word[0];
    int eip = cpu_regs.ip.dword[0];

    int flags = cpu_regs.flags;

    int cs = this->debug_getregistervalue(X86Registers::REGI_CS);
    int ds = this->debug_getregistervalue(X86Registers::REGI_DS);
    int es = this->debug_getregistervalue(X86Registers::REGI_ES);





}


void LuaEngine::LuaFrameBoundary()
{

    // Define the individual register variables
    int al = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0];
    int ah = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1];
    int ax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].word[0];
    int eax = cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].dword[0];

    int bl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[0];
    int bh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].byte[1];
    int bx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].word[0];
    int ebx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BX)].dword[0];

    int cl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[0];
    int ch = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].byte[1];
    int cx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].word[0];
    int ecx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_CX)].dword[0];

    int dl = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0];
    int dh = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1];
    int dx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].word[0];
    int edx = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].dword[0];

    int si = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].word[0];
    int esi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SI)].dword[0];

    int di = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].word[0];
    int edi = cpu_regs.regs[static_cast<int>(X86Registers::REGI_DI)].dword[0];

    int sp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].word[0];
    int esp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_SP)].dword[0];

    int bp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].word[0];
    int ebp = cpu_regs.regs[static_cast<int>(X86Registers::REGI_BP)].dword[0];

    int ip = cpu_regs.ip.word[0];
    int eip = cpu_regs.ip.dword[0];

    int flags = cpu_regs.flags;

    int cs = this->debug_getregistervalue(X86Registers::REGI_CS);
    int ds = this->debug_getregistervalue(X86Registers::REGI_DS);
    int es = this->debug_getregistervalue(X86Registers::REGI_ES);

    // HA!
    if(!luaRunning)
        return;



    // Lua calling C must know that we're busy inside a frame boundary
    frameBoundary = true;
    frameAdvanceWaiting = false;


        for(auto& p : this->logpoints) {

            if(cs == p.cs && ip == p.ip) {

                DEBUG_ShowMsg("Logpoint %s BX %02x SI %02x AX %02x DI %02x DX %02x\n", p.name.c_str(), bx, si, ax, di, dx);
                auto buf = GetBytes(ds, di);

            }

        }


    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x40
        )
    {


        KLBHook();



    }

    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x49C
        )
    {
        KHDHook();
    }



    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x40
        )
    {
        char out[1024];
        char str[1024];
        char temp[1024];

        int seg = this->debug_getregistervalue(X86Registers::REGI_DS);
        int ofs1 = this->debug_getregistervalue(X86Registers::REGI_DX);

        auto index = this->debug_getregistervalue(X86Registers::REGI_BX);
        auto strIndex = getAddressLabel(index);

        debugHook((LuaEngine::AddressLabel)index);


    }

    //loop_coroutine();

    // Past here, the nes actually runs, so any Lua code is called mid-frame. We must
    // not do anything too stupid, so let ourselves know.
    frameBoundary = false;

    numTries = 1000;

}


