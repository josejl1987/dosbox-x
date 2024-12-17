
#include "SDL.h"
#include "SDL_opengl.h"
#include "sdlmain.h"
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include "imgui_window.h"


bool InitImGui(SDL_Window * window) {
    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Setup Platform/Renderer backends
    SDL_GLContext gl_context = SDL_GL_GetCurrentContext();
    
    if (!window || !gl_context) {
        return false;
    }
    
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    
    return true;
}

void CleanupImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void ProcessImGuiEvents(SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void RenderImGuiWindow() {
    // Create a window called "My Window"
    ImGui::Begin("My Window");

    // Add your window content here
    ImGui::Text("Welcome to my ImGui window!");
    
    // Add a button
    if (ImGui::Button("Click me!")) {
        // Button click handling code here
    }

    // End the window
    ImGui::End();
}

void RenderImGuiFrame() {
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Render your ImGui windows
    RenderImGuiWindow();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
