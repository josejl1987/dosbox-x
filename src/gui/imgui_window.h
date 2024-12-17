#ifndef IMGUI_WINDOW_H
#define IMGUI_WINDOW_H

struct SDLWindow;

// Initialize ImGui context and SDL2 backend

bool InitImGui(SDL_Window* window);

// Cleanup ImGui
void CleanupImGui();

// Process SDL events for ImGui 
void ProcessImGuiEvents(SDL_Event& event);

// Function to render the ImGui window
void RenderImGuiWindow();

// Render ImGui frame
void RenderImGuiFrame();

#endif // IMGUI_WINDOW_H
