#pragma once

#include <lua.hpp>
#include <cstdint>
#include <string>
#define SOL_ALL_SAFETIES_ON  1
#include <sol/sol.hpp>

struct LuaEngine
{

    struct LogPoint {
        uint16_t cs;
        uint16_t ip;
        std::string name;
    };


    LuaEngine() {
    };

    sol::state lua;
    int luaexiterrorcount = 8;

    // Are we running any code right now?
    char* luaScriptName = NULL;

    // Are we running any code right now?
    int luaRunning = 0;

    // True at the frame boundary, false otherwise.
    int frameBoundary = 0;

    std::vector<LogPoint> logpoints;
   

    // The execution speed we're running at.
    enum { SPEED_NORMAL, SPEED_NOTHROTTLE, SPEED_TURBO, SPEED_MAXIMUM } speedmode = SPEED_NORMAL;

    // Rerecord count skip mode
    int skipRerecords = 0;

    // Used by the registry to find our functions
    const char* frameAdvanceThread = "emu_frameadvance";
    const char* guiCallbackTable = "FCEU.GUI";

    // True if there's a thread waiting to run after a run of frame-advance.
    int frameAdvanceWaiting = 0;

    // We save our pause status in the case of a natural death.
    // int wasPaused = FALSE;

    // Transparency strength. 255=opaque, 0=so transparent it's invisible
    int transparencyModifier = 255;

    // Our zapper.
    int luazapperx = -1;
    int luazappery = -1;
    int luazapperfire = -1;

    // Our joypads.
    uint8_t luajoypads1[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; //x1
    uint8_t luajoypads2[4] = { 0x00, 0x00, 0x00, 0x00 }; //0x
    /* Crazy logic stuff.
        11 - true		01 - pass-through (default)
        00 - false		10 - invert					*/

    enum { GUI_USED_SINCE_LAST_DISPLAY, GUI_USED_SINCE_LAST_FRAME, GUI_CLEAR } gui_used = GUI_CLEAR;
    uint8_t* gui_data = NULL;
    int gui_saw_current_palette = 0;

    int numTries;
    std::shared_ptr<sol::state_view> sol_state;
    // number of registered memory functions (1 per hooked byte)
    unsigned int numMemHooks;

    char* rawToCString(lua_State* L, int idx = 0);
    const char* toCString(lua_State* L, int idx = 0);

    int exitScheduled = 0;


    int LoadCode(const char* filename, const char* arg);


    enum class X86Registers {
        REGI_AX, REGI_CX, REGI_DX, REGI_BX,
        REGI_SP, REGI_BP, REGI_SI, REGI_DI,
        REGI_AL, REGI_CL, REGI_DL, REGI_BL,
        REGI_AH, REGI_CH, REGI_DH, REGI_BH,
        REGI_EAX, REGI_ECX, REGI_EDX, REGI_EBX,
        REGI_ESP, REGI_EBP, REGI_ESI, REGI_EDI,
        REGI_IP, REGI_EIP, REGI_FLAGS, REGI_CS, REGI_DS,REGI_SS,REGI_ES

    };

    enum AddressLabel {
        BZ_EXEC_LOAD,
        FUN_3000_1396,
        FUN_3000_1401,
        LAB_3000_1418,
        BZ_LOAD_LIB,
        NORMAL_LOAD_LIB,
        UNPACK_CALL,
        NORMAL_LOAD,
        NORMAL_SAVE,
        LAB_3000_03d5,
        LAB_3000_03f8,
        CHECK_DISK_NUMBER2,
        BZ_LOAD_LIB2,
        LAB_3000_07e7,
        LAB_3000_0ba1,
        LAB_3000_0c3d,
        LAB_3000_0cda,
        FUN_3000_0f8f,
        LAB_3000_0d30,
        LAB_3000_0047
    };


    void debugHook(AddressLabel index);

    void KLBHook();

    void KHDHook();

    void LuaFrameBoundary();
    int emu_frameadvance();

    uint32_t debug_getregistervalue(const X86Registers& reg);
    sol::function loop_coroutine;
    void debug_addlogpoint(uint16_t seg, uint32_t off, const std::string& name);
    void debug_addbreakpoint(uint16_t seg, uint32_t off, bool once);
    void debug_removebreakpoint(uint16_t seg, uint32_t off, bool once);
    void debug_addmembreakpoint(uint16_t seg, uint32_t off);
    void debug_removemembreakpoint(uint16_t seg, uint32_t off);
    void debug_enabledebugger();

};
