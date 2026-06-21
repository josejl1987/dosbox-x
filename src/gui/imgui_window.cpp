#include "dosbox.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include "sdlmain.h"
extern SDL_Block sdl;
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>
#include "imgui_window.h"
#include "imgui_performance_config.h"
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#include <imgui/backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <dxgi.h>
#include <SDL_syswm.h>

// Data for D3D11 Backend
struct DX11BackendData {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIFactory* factory = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* main_render_target_view = nullptr;
    bool initialized = false;
};
static DX11BackendData g_dx11_backend;

// Forward declarations
static bool InitImGuiDX11(SDL_Window* window);
static void CleanupImGuiDX11();
static void RenderFrameDX11();
static void PresentDX11Frame();
static void NewFrameDX11();
static void CreateRenderTarget();
static void CleanupRenderTarget();
#endif

#if C_LUA
#include "../luaengine/gui_windows.h"
#endif

#if C_DEBUG
#include "../luaengine/debugger_session.h"
#endif

bool InitImGui(SDL_Window * window) {
    if (ImGui::GetCurrentContext() != nullptr) {
        // Already initialized (e.g., OpenGL setup path + WindowManager::initialize).
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.LogFilename = nullptr; // Disable file logging for performance

    // Enable Keyboard/Gamepad
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Enable Docking for debugger workspace
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Platform/Renderer backends
    bool use_dx11_backend = false;

#ifdef _WIN32
    // Force D3D11 if available, as requested for performance/multi-window support
    if (InitImGuiDX11(window)) {
        use_dx11_backend = true;
        // FORCE Viewports for D3D11 to allow "rendering to other windows"
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        printf("DirectX 11 backend initialized with Multi-Viewports enabled.\n");
    } else {
        printf("DirectX 11 backend failed, falling back to OpenGL.\n");
    }
#endif

    if (!use_dx11_backend) {
        // Fallback to OpenGL
        SDL_GLContext gl_context = SDL_GL_GetCurrentContext();
        if (!window || !gl_context) return false;
        ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
        ImGui_ImplOpenGL3_Init("#version 130");

        // Disable viewports on OpenGL as it requires complex context sharing
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

    // Styling
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    return true;
}

void CleanupImGui() {
#ifdef _WIN32
    if (g_dx11_backend.initialized) {
        CleanupImGuiDX11();
    } else {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
    }
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
#endif
    ImGui::DestroyContext();
}

void ProcessImGuiEvents(SDL_Event& event) {
#if C_DEBUG
    // Handle debug tools keyboard shortcuts via HotkeyConfig
    if (event.type == SDL_KEYDOWN) {
        auto* session = ::GetDebuggerSession();
        if (session && session->isInitialized()) {
            // Skip hotkey processing when ImGui wants keyboard input (e.g., text fields)
            ImGuiIO& io = ImGui::GetIO();
            if (!io.WantCaptureKeyboard) {
                // Register Ctrl+Shift shortcuts as one-shot dispatches
                if ((event.key.keysym.mod & KMOD_CTRL) && (event.key.keysym.mod & KMOD_SHIFT)) {
                    auto* config = session->config();
                    if (config) {
                        switch (event.key.keysym.sym) {
                            case SDLK_s:
#if C_LUA
                                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                                    if (window_manager->isMemorySearchVisible()) {
                                        window_manager->hideMemorySearch();
                                    } else {
                                        window_manager->showMemorySearch();
                                    }
                                }
#endif
                                return;
                            case SDLK_w:
#if C_LUA
                                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                                    if (window_manager->isWatchListVisible()) {
                                        window_manager->hideWatchList();
                                    } else {
                                        window_manager->showWatchList();
                                    }
                                }
#endif
                                return;
                            case SDLK_h:
#if C_LUA
                                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                                    if (window_manager->isHexEditorVisible()) {
                                        window_manager->hideHexEditor();
                                    } else {
                                        window_manager->showHexEditor();
                                    }
                                }
#endif
                                return;
                            case SDLK_t:
#if C_LUA
                                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                                    if (window_manager->isTraceLoggerVisible()) {
                                        window_manager->hideTraceLogger();
                                    } else {
                                        window_manager->showTraceLogger();
                                    }
                                }
#endif
                                return;
                            case SDLK_d:
#if C_LUA
                                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                                    if (window_manager->isDisassemblyVisible()) {
                                        window_manager->hideDisassembly();
                                    } else {
                                        window_manager->showDisassembly();
                                    }
                                }
                                return;
#else
                                break;
#endif
                        }
                    }
                }
                // F-key hotkeys are dispatched by DebugConfigManager::processHotkeys()
                // which runs in the render loop via DebuggerSession::update()
            }
        }
    }
#endif // C_DEBUG

    // Always process mouse events for ImGui - ImGui operates in screen space
    // and should work regardless of DOSBox-X mouse lock status
    if (event.type == SDL_MOUSEMOTION ||
        event.type == SDL_MOUSEBUTTONDOWN ||
        event.type == SDL_MOUSEBUTTONUP) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    } else {
        // Only process keyboard and other events if they're not debug shortcuts
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}

void RenderImGuiFrame() {
    if (!ImGui::GetCurrentContext()) return;

#if IMGUI_ENABLE_PERFORMANCE_LOGGING
    // Performance timing for ImGui render path
    static int perf_frame_count = 0;
    static double perf_accum_ms = 0.0;
    auto perf_start = std::chrono::high_resolution_clock::now();
#endif

#ifdef _WIN32
    if (g_dx11_backend.initialized) {
        NewFrameDX11();
    } else {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }
#else
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
#endif

#if C_LUA
    // Render main menu bar for window management
    if (ImGui::BeginMainMenuBar()) {

#if C_DEBUG
        // Debug tools menu integration
        auto* session_menu = ::GetDebuggerSession();
        if (session_menu && session_menu->isInitialized()) {
            // ponytail: simplified menu — direct window toggles via WindowManager
            if (ImGui::BeginMenu("Debug Tools")) {
#if C_LUA
                if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                    if (ImGui::MenuItem("RAM Search", "Ctrl+Shift+S")) {
                        window_manager->showMemorySearch();
                    }
                    if (ImGui::MenuItem("Watch List", "Ctrl+Shift+W")) {
                        window_manager->showWatchList();
                    }
                    if (ImGui::MenuItem("Hex Editor", "Ctrl+Shift+H")) {
                        window_manager->showHexEditor();
                    }
                    if (ImGui::MenuItem("Disassembly", "Ctrl+Shift+D")) {
                        window_manager->showDisassembly();
                    }
                    if (ImGui::MenuItem("Trace Logger", "Ctrl+Shift+T")) {
                        window_manager->showTraceLogger();
                    }
                    if (ImGui::MenuItem("Cheat Engine")) {
                        window_manager->showCheatEngine();
                    }
                }
#endif
                ImGui::EndMenu();
            }
        }
#endif // C_DEBUG

        if (ImGui::BeginMenu("Help")) {
            ImGui::Text("Debug Tools Keyboard Shortcuts:");
            ImGui::Separator();
            ImGui::Text("Ctrl+Shift+S - Toggle RAM Search");
            ImGui::Text("Ctrl+Shift+W - Toggle Watch List");
            ImGui::Text("Ctrl+Shift+H - Toggle Hex Editor");
            ImGui::Text("Ctrl+Shift+T - Toggle Trace Logger");
            ImGui::Text("Ctrl+Shift+D - Toggle Disassembly Debugger");

#if C_DEBUG
            // Add debug tools help
            auto* session_help = ::GetDebuggerSession();
            if (session_help && session_help->isInitialized()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Debug tools are hidden by default");
            }
#endif // C_DEBUG
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
#endif // C_LUA

#if C_DEBUG
    // Ensure debug session is initialized
    static bool debug_session_initialized = false;
    if (!debug_session_initialized) {
        ::InitializeDebugSession();
        debug_session_initialized = true;
    }

    // Update session (watch values, hotkeys)
    if (auto* session = ::GetDebuggerSession()) {
        if (session->isInitialized()) {
            session->update();
        }
    }
#endif

#if C_LUA
    // Render all WindowManager-owned tool windows (Hex/Watch/RAM/Trace/Cheat/Disasm)
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        // Global hotkey fallback: Ctrl+Shift+D toggles Disassembly window
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            if (window_manager->isDisassemblyVisible()) {
                window_manager->hideDisassembly();
            } else {
                window_manager->showDisassembly();
            }
        }

        window_manager->renderAllWindows();
    }
#endif

    // End Frame and Render
#ifdef _WIN32
    if (g_dx11_backend.initialized) {
        RenderFrameDX11();
    } else {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }
    }
#else
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }
#endif

#if IMGUI_ENABLE_PERFORMANCE_LOGGING
    auto perf_end = std::chrono::high_resolution_clock::now();
    perf_accum_ms += std::chrono::duration<double, std::milli>(perf_end - perf_start).count();
    if (++perf_frame_count >= IMGUI_PERF_LOG_INTERVAL_FRAMES) {
        printf("[ImGuiPerf] avg frame render time: %.3f ms over %d frames\n",
               perf_accum_ms / perf_frame_count, perf_frame_count);
        perf_frame_count = 0;
        perf_accum_ms = 0.0;
    }
#endif
}

// ============================================================================
// D3D11 Backend Implementation
// ============================================================================
#ifdef _WIN32

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_dx11_backend.swap_chain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_dx11_backend.device->CreateRenderTargetView(pBackBuffer, NULL, &g_dx11_backend.main_render_target_view);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_dx11_backend.main_render_target_view) {
        g_dx11_backend.main_render_target_view->Release();
        g_dx11_backend.main_render_target_view = NULL;
    }
}

static bool InitImGuiDX11(SDL_Window* window) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) return false;
    HWND hwnd = wmInfo.info.win.window;

    // Setup Swap Chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, NULL, 0, D3D11_SDK_VERSION, &sd, &g_dx11_backend.swap_chain, &g_dx11_backend.device, &featureLevel, &g_dx11_backend.context) != S_OK)
        return false;

    CreateRenderTarget();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForD3D(window);
    ImGui_ImplDX11_Init(g_dx11_backend.device, g_dx11_backend.context);

    g_dx11_backend.initialized = true;
    return true;
}

static void CleanupImGuiDX11() {
    if (g_dx11_backend.initialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        CleanupRenderTarget();
        if (g_dx11_backend.swap_chain) { g_dx11_backend.swap_chain->Release(); g_dx11_backend.swap_chain = NULL; }
        if (g_dx11_backend.context) { g_dx11_backend.context->Release(); g_dx11_backend.context = NULL; }
        if (g_dx11_backend.device) { g_dx11_backend.device->Release(); g_dx11_backend.device = NULL; }
        g_dx11_backend.initialized = false;
    }
}

static void NewFrameDX11() {
    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
}

static void RenderFrameDX11() {
    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();

    // Set render target
    g_dx11_backend.context->OMSetRenderTargets(1, &g_dx11_backend.main_render_target_view, NULL);

    // Render
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Note: Present should be called by DOSBox-X's main rendering loop, not here
    // This prevents timing issues and frame sync problems
}

static void PresentDX11Frame() {
    if (g_dx11_backend.initialized && g_dx11_backend.swap_chain) {
        g_dx11_backend.swap_chain->Present(1, 0);
    }
}

// Exported function for DOSBox-X to call
void PresentDX11() {
    PresentDX11Frame();
}

#endif // _WIN32