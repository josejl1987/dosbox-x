#ifndef IMGUI_WINDOW_H
#define IMGUI_WINDOW_H

struct SDLWindow;

// Initialize ImGui context and SDL2 backend
bool InitImGui(SDL_Window* window);

// Cleanup ImGui
void CleanupImGui();

// Process SDL events for ImGui
void ProcessImGuiEvents(SDL_Event& event);

// Render ImGui frame
void RenderImGuiFrame();

#ifdef _WIN32
// Present D3D11 frame (called by DOSBox-X rendering loop)
void PresentDX11();
#endif

#endif // IMGUI_WINDOW_H
