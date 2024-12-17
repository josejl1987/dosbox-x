#include "luaengine.h"
#include <debug.h>
#include <regs.h>
#include <paging.h>
#include <imgui/backends/imgui_impl_opengl3_loader.h>
#include <sdl2/include/SDL.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"




void WriteBytes(int seg, int ofs1, std::vector<uint8_t>& data) {

    std::vector<char> buffer;
    for(uint16_t x = 0; x < data.size(); x++) {
        uint8_t value;

        if(!mem_writeb_checked((PhysPt)GetAddress(seg, ofs1 + x), data[x]))
        {

        }
    }
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
#pragma pack(push, 1)  // Set the alignment to 1 byte
struct GameData {
    uint8_t CUR_PLAYER_CHR;           // 0x6741
    uint8_t CUR_MON1_CHR;             // 0x6742
    uint8_t CUR_MON2_CHR;             // 0x6743
    uint8_t CUR_MON3_CHR;             // 0x6744
    uint8_t CUR_BACK_CHR;             // 0x6745
    uint8_t CUR_TRAP_CHR;             // 0x6746
    uint8_t CUR_MAP;                  // 0x6747
    uint8_t CUR_MONS_ALG;             // 0x6748
    uint8_t CUR_PAL_BACK;             // 0x6749
    uint16_t CUR_PAL_MONS;            // 0x674A
    uint16_t VIEW_FLAG;               // 0x674C
    uint16_t CUR_VIEW_POINT;          // 0x674E
    uint16_t CUR_OVL_CHR_NO;          // 0x6750
    uint8_t TURN_FLAG;                // 0x6752
    uint8_t FL_FLAG;                  // 0x6753
    uint16_t BANK_CHG_FLAG;           // 0x6754
    uint8_t MZX_OVL_TBL[32];          // 0x6756 (32 bytes)
    uint16_t MZX_ATT_TBL;             // 0x6766
    uint8_t reserved_6768;            // 0x6768
    uint8_t reserved_6769;            // 0x6769
    uint8_t reserved_676A;            // 0x676A
    uint8_t reserved_676B;            // 0x676B
    uint8_t reserved_676C;            // 0x676C
    uint8_t reserved_676D;            // 0x676D
    uint16_t MZX_QUAIK_TBL;           // 0x676E
    uint8_t reserved_6770;            // 0x6770
    uint8_t reserved_6771;            // 0x6771
    uint8_t reserved_6772;            // 0x6772
    uint8_t reserved_6773;            // 0x6773
    uint8_t reserved_6774;            // 0x6774
    uint8_t reserved_6775;            // 0x6775
    uint16_t UNPACK_BX;               // 0x6776
    uint8_t UNPACK_DX;               // 0x6778
    uint8_t reserved_6779;            // 0x6779
    uint16_t CHECK_VIEW_VEC;          // 0x677A
    uint8_t reserved_677C[5];         // 0x677C (5 bytes)
    uint8_t MZX_VEC_TBL[56];          // 0x6782 (56 bytes)
    uint16_t WINDOW_WORK;             // 0x67C2
};
#pragma pack(pop)


// Function to convert GameData to a vector of uint8_t in a single call
std::vector<uint8_t> GameDataToVector(const GameData& data) {
    std::vector<uint8_t> byte_vector(sizeof(data));

    // Copy the entire struct to the vector in a single call
    std::memcpy(byte_vector.data(), &data, sizeof(data));

    return byte_vector;
}

// Function to create the window and run ImGui
void LuaEngine::windowCreationThread(const std::string& title, int width, int height) {
    // Setup SDL
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_ALLOW_HIGHDPI |
        SDL_WINDOW_INPUT_FOCUS
        );
    SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    if(window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if(gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);
    std::vector<uint8_t> buffer(0x10000);
    // Main loop for rendering
    bool running = true;
    while(running) {
        // Handle SDL events
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) {
                running = false;
            }
            ImGui_ImplSDL2_ProcessEvent(&event); // Process events for ImGui
        }




        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        // Here you can add ImGui UI elements
        ImGui::Begin("Hello, World!");

        GameData data;

        auto d = GetBytes(0x1BA3, 0x6741);
        memcpy(&data, d.data(), sizeof(GameData));

        //ImGui::InputScalar("CUR_PLAYER_CHR", ImGuiDataType_U8, &data.CUR_PLAYER_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_MON1_CHR", ImGuiDataType_U8, &data.CUR_MON1_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_MON2_CHR", ImGuiDataType_U8, &data.CUR_MON2_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_MON3_CHR", ImGuiDataType_U8, &data.CUR_MON3_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_BACK_CHR", ImGuiDataType_U8, &data.CUR_BACK_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_TRAP_CHR", ImGuiDataType_U8, &data.CUR_TRAP_CHR, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_MAP", ImGuiDataType_U8, &data.CUR_MAP, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_MONS_ALG", ImGuiDataType_U8, &data.CUR_MONS_ALG, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_PAL_BACK", ImGuiDataType_U8, &data.CUR_PAL_BACK, NULL, NULL, "%02X");
        //ImGui::InputScalar("CUR_PAL_MONS", ImGuiDataType_U16, &data.CUR_PAL_MONS, NULL, NULL, "%04X");
        //ImGui::InputScalar("VIEW_FLAG", ImGuiDataType_U16, &data.VIEW_FLAG, NULL, NULL, "%04X");
        //ImGui::InputScalar("CUR_VIEW_POINT", ImGuiDataType_U16, &data.CUR_VIEW_POINT, NULL, NULL, "%04X");
        //ImGui::InputScalar("CUR_OVL_CHR_NO", ImGuiDataType_U16, &data.CUR_OVL_CHR_NO, NULL, NULL, "%04X");
        //ImGui::InputScalar("TURN_FLAG", ImGuiDataType_U8, &data.TURN_FLAG, NULL, NULL, "%02X");
        //ImGui::InputScalar("FL_FLAG", ImGuiDataType_U8, &data.FL_FLAG, NULL, NULL, "%02X");
        //ImGui::InputScalar("BANK_CHG_FLAG", ImGuiDataType_U16, &data.BANK_CHG_FLAG, NULL, NULL, "%04X");

        ImGui::Text("FPS: %f", ImGui::GetIO().DeltaTime);

        std::vector<uint8_t> serialized_data = GameDataToVector(data);

        ImGui::InputScalar("Map ID", ImGuiDataType_U8, &this->mc.map, NULL, NULL, "%04X");
        ImGui::InputScalar("Map flag", ImGuiDataType_U8, &this->mc.unk, NULL, NULL, "%04X");
        ImGui::InputScalar("X", ImGuiDataType_U8, &this->mc.x, NULL, NULL, "%04X");
        ImGui::InputScalar("Y", ImGuiDataType_U8, &this->mc.y, NULL, NULL, "%04X");

        //    WriteBytes(0x1BA3, 0x6741, serialized_data);
        if(ImGui::Button("Change Map")) {

            forceChangeMao = true;

        }


        ImGui::Text("This is a simple ImGui window.");
        ImGui::End();

        // Rendering
        ImGui::Render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}


void LuaEngine::createWindow(const std::string& title, int width, int height) {
    // Start a new thread for window creation
    std::thread windowThread(&LuaEngine::windowCreationThread, this, title, width, height);
    windowThread.detach(); // Detach the thread to allow it to run independently
}

void LuaEngine::loadSymbols(char* filename) {

    std::ifstream file(filename, std::ios::binary);
    if(!file.is_open()) {
        std::cerr << "Error opening file\n";

    }

    int numSymbols;
    file.read(reinterpret_cast<char*>(&numSymbols), sizeof(int));
    if(file.fail()) {
        std::cerr << "Error reading number of symbols\n";
        file.close();
    }

    std::vector<Symbol> symbols(numSymbols);
    for(int i = 0; i < numSymbols; ++i) {
        // Read type
        file.read(reinterpret_cast<char*>(&symbols[i].type), sizeof(char));
        if(file.fail()) {
            std::cerr << "Error reading type\n";
            file.close();
        }

        // Read offset
        file.read(reinterpret_cast<char*>(&symbols[i].offset), sizeof(int));
        if(file.fail()) {
            std::cerr << "Error reading offset\n";
            file.close();
        }

        // Read total_size
        file.read(reinterpret_cast<char*>(&symbols[i].total_size), sizeof(int));
        if(file.fail()) {
            std::cerr << "Error reading total_size\n";
            file.close();
        }

        // Read mem_type
        file.read(reinterpret_cast<char*>(&symbols[i].mem_type), sizeof(int));
        if(file.fail()) {
            std::cerr << "Error reading mem_type\n";
            file.close();
        }

        // Read name
        char ch;
        while(file.get(ch) && ch != '\0') {
            symbols[i].name += ch;
        }
        if(file.fail()) {
            std::cerr << "Error reading name\n";
            file.close();
        }
    }


    // Process symbols as needed
    this->symbols = std::move(symbols);
    file.close();


}




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
    lua.new_usertype<LuaEngine>("LuaEngine",
        "createWindow", &LuaEngine::createWindow // Binding the createWindow function
    );

    createWindow("Hello", 640, 480);

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
    static int lastcs = 0;
    static int lastip = 0;
    int cs = this->debug_getregistervalue(X86Registers::REGI_CS);
    int ds = this->debug_getregistervalue(X86Registers::REGI_DS);
    int es = this->debug_getregistervalue(X86Registers::REGI_ES);

    // Initialize spdlog

    switch(index)
    {
    case BZ_EXEC_LOAD:
    {
        logger->info("BZ EXEC Load Lib Call BX {:02x} SI {:02x} AX {:02x} DI {:02x} DX {:02x}", bx, si, ax, di, dx);

        auto buf = GetBytes(ds, di);
    }
    break;
    case BZ_LOAD_LIB:
    {
        auto buf = GetBytes(ds, si);
        logger->info("BZ Load Lib {}", buf.data());
    }
    break;
    case NORMAL_LOAD_LIB:
    {
        logger->info("Normal Load Lib Call BX {:02x} SI {:02x} AX {:02x} DI {:02x} DX {:02x}", bx, si, ax, di, dx);

        auto buf = GetBytes(ax, dx + 1);
    }
    break;
    case UNPACK_CALL:
        logger->info("Unpack Call at {:04x}:{:04x}", ax, dx);
        break;
    case NORMAL_LOAD:
    {
        auto buf = GetBytes(ax, dx + 1);
        logger->info("Normal Load {:02x}, {}", dx, buf.data());
    }
    break;
    default:
        break;
    }
}

void LuaEngine::KHDHook() {

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

    auto logger = spdlog::basic_logger_mt("file_logger", "debug_output.log");

    auto khd = GetBytes(ds, dx + 1);
    logger->info("Load KHD {}", khd.data());

    auto name = GetBytes(ds, si);
    logger->info("Load file {} at {:04x}:{:04x}", name.data(), ds, dest);
}

void LuaEngine::KLBHook() {

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

    auto logger = spdlog::basic_logger_mt("file_logger", "debug_output.log");

}

int lastip, lastcs;

#pragma pack(1)
struct MusicChannel
{
    char field_0;
    char field_1;
    short ptr;
    char volume2;
    char program;
    char volume;
    char field_7;
    char detune;
    char GateTime;
    char field_A;
    char field_B;
    char field_C;
    char field_D;
    char field_E;
    char field_F;
    char field_10;
    char lfoDelay;
    char lfoPeriod;
    short lfoAmplitude;
    char lfoRepeat;
    char currentLfoPeriod;
    short currentLfoAmplitude;
    char currentlfoRepeat;
    short fnum;
    char field_1C;
    char volumeEnvelope;
    char channel;
    char flag;
};


enum class Game {
    Brandish1, Brandish2
};

const char* getFunctionName2(int index) {
    switch(index) {
    case 0: return "BZ_EXEC_LOAD";
    case 1: return "nullsub_1";
    case 2: return "nullsub_2";
    case 3: return "sub_BC020";
    case 4: return "BZ_LOAD_LIB";
    case 5: return "NORMAL_LOAD_LIB";
    case 6: return "UNPACK_CALL";
    case 7: return "NORMAL_LOAD";
    case 8: return "NORMAL_SAVE";
    case 9: return "NORMAL_SAVE_LIB";
    case 10: return "DELETE_FILE";
    case 11: return "CHECK_DISK_NUMBER2";
    case 12: return "BZ_LOAD_LIB2";
    case 13: return "DATA_FILE_COPY";
    case 14: return "NORMAL_LOAD2";
    case 15: return "NORMAL_SAVE2";
    case 16: return "DELETE_FILE2";
    case 17: return "DISK_ERROR";
    case 18: return "DATA_FILE_COPY2";
    case 19: return "locret_BC052";
    default: return "Invalid index";
    }
}


void LuaEngine::LuaFrameBoundary()
{

    Game currentGame = Game::Brandish2;

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
    CodePos pos = { cs, ip };


    auto opcode = GetByte(cs, ip);


    if(currentGame == Game::Brandish1) {


        if(ip == 0x56df && cs == 0xba3) {

            cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0] = 1;
            cpu_regs.ip.word[0] = 0x56e3;

        }

    }


    if(ip == 0x40 && cs == 0x923) {

        logger->info("{:04x}:{:04x}  BX {:02x} {} SI {:02x} AX {:02x} DI {:02x} DX {:02x} DS {:04x} ES \n", cs, ip, bx, getFunctionName2(bx), si, ax, di, dx, ds, es);

    }

    if(ip == 0xB809 && cs == 0xBA3) {

        if(forceChangeMao) {
            forceChangeMao = false;
            cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[0] = mc.map;
            cpu_regs.regs[static_cast<int>(X86Registers::REGI_AX)].byte[1] = mc.unk;
            cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[0] = mc.x;
            cpu_regs.regs[static_cast<int>(X86Registers::REGI_DX)].byte[1] = mc.y;
            cpu_regs.ip.word[0] = 0x213;

        }

    }

    if(opcode == 0xE8) {
        logger->info("{:04x}:{:04x}  BX {:02x} SI {:02x} AX {:02x} DI {:02x} DX {:02x} DS {:04x} ES \n", cs, ip, bx, si, ax, di, dx, ds, es);

    }


    for(auto& p : this->logpoints) {

        if(cs == p.cs && ip == p.ip) {

            logger->info("Logpoint {} BX {:02x} SI {:02x} AX {:02x} DI {:02x} DX {:02x}\n", p.name.c_str(), bx, si, ax, di, dx);
            auto buf = GetBytes(ds, di);

        }

    }

    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x1507 && currentGame == Game::Brandish2)
    {
        // char out[1024];
        // char str[1024];
        // char temp[1024];


        //// std::ifstream file("c:/dino98/070", std::ios::binary);

        // // Check if the file opened successfully
        // if(!file) {
        //     std::cerr << "Unable to open file!" << std::endl;
        //     return;
        // }

        // // Seek to the end to determine the size of the file
        // file.seekg(0, std::ios::end);
        // std::streamsize size = file.tellg();
        // file.seekg(0, std::ios::beg);

        // // Create a vector large enough to hold the file's content
        // std::vector<unsigned char> buffer(size);

        // if(file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        //     std::cout << "File read successfully!" << std::endl;
        //     // You can now use the 'buffer' vector which contains the file's binary data
        // }
        // else {
        //     std::cerr << "Error reading file!" << std::endl;
        // }

        // for(int i = 0; i < size; i++) {
        //     real_writeb(0x1BA3, 0x5D54 + i, buffer[i]);

        // }

        // int seg = this->debug_getregistervalue(X86Registers::REGI_DS);
        // int ofs1 = this->debug_getregistervalue(X86Registers::REGI_DX);

        // auto index = this->debug_getregistervalue(X86Registers::REGI_BX);


        // DEBUG_ShowMsg("Unpacking at {:04x} {:04x}\n", ax, di);


    }


    // Define named constants for the register values
    uint16_t CS_VALUE = 0x923;
    uint16_t IP_VALUE = 0x1163;

    currentGame = Game::Brandish1;
    if(currentGame == Game::Brandish1) {
        IP_VALUE = 0x1163;
    }



    if(this->debug_getregistervalue(X86Registers::REGI_CS) == CS_VALUE &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == IP_VALUE)
    {
        char out[1024];
        char str[1024];
        char temp[1024];

        int seg = this->debug_getregistervalue(X86Registers::REGI_DS);
        int ofs1 = this->debug_getregistervalue(X86Registers::REGI_DX);

        auto index = this->debug_getregistervalue(X86Registers::REGI_BX);

        logger->info("Unpacking at {:04x} {:04x}", ax, di);
    }




    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x1187 && currentGame == Game::Brandish2)
    {
        char out[1024];
        char str[1024];
        char temp[1024];

        int seg = this->debug_getregistervalue(X86Registers::REGI_DS);
        int ofs1 = this->debug_getregistervalue(X86Registers::REGI_DX);

        auto index = this->debug_getregistervalue(X86Registers::REGI_BX);


        logger->info("Unpacking at {:04x} {:04x}", ax, di);


    }




    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x1C24
        )
    {
        char out[1024];
        char str[1024];
        char temp[1024];

        MusicChannel* data = (MusicChannel*)GetBytes(0x1BA3, 0x10C2).data();



    }

    if(this->debug_getregistervalue(X86Registers::REGI_CS) == 0x923 &&
        this->debug_getregistervalue(X86Registers::REGI_IP) == 0x1C24
        )
    {
        char out[1024];
        char str[1024];
        char temp[1024];
        auto bgmClk = *(uint16_t*)GetBytes(0x1BA3, 0x1233).data();
        MusicChannel* data = (MusicChannel*)
            (MusicChannel*)GetBytes(0x1BA3, 0x10A2).data();





        //DEBUG_ShowMsg("BGM_CLK %02i Flag {:02x}, FNUM {:02x}, Env counter {:02x}, Volume %02i ",  bgmClk, data[8].flag,  data[8].fnum>>data[8].field_1C, (unsigned char)data[8].field_E, data[8].volumeEnvelope);

        data = (MusicChannel*)
            (MusicChannel*)GetBytes(0x1BA3, 0x10A2).data();
        auto octave = data[0].fnum >> 11;
        auto fnum = ((data[0].fnum) >> data[0].field_1C) & 0x7FF;
        //      DEBUG_ShowMsg("CH4 BGM_CLK %02i Flag {:02x}, Octave {:02x} FNUM {:02x}, Env counter {:02x}, Volume %02i Ptr {:02x}", bgmClk, data[0].flag, octave, fnum, (unsigned char)data[0].field_E, data[0].volumeEnvelope, data[0].ptr);


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
    lastip = ip;
    lastcs = cs;
    numTries = 1000;

}


