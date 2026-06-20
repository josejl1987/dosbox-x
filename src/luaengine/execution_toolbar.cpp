#include "execution_toolbar.h"
#include <imgui/imgui.h>

namespace LuaEngineDebugger {

ExecutionToolbar::ExecutionToolbar(LuaEngineDebug::CoreDebugInterface* debug)
    : debug_(debug) {}

void ExecutionToolbar::render() {
    if (!debug_) return;

    bool paused = debug_->isPaused();

    ImGui::PushID("ExecutionToolbar");
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Execution Toolbar", nullptr, flags)) {
        ImGui::End();
        ImGui::PopID();
        return;
    }

    // Status indicator
    ImVec4 status_color = paused ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImGui::TextColored(status_color, paused ? "[PAUSED]" : "[RUNNING]");
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    // Resume/Pause toggle
    if (paused) {
        if (ImGui::Button("Resume (F5)")) {
            debug_->resume();
        }
    } else {
        if (ImGui::Button("Pause")) {
            debug_->pause();
        }
    }
    ImGui::SameLine();

    // Step controls — disabled when CPU is running
    if (!paused) ImGui::BeginDisabled();
    if (ImGui::Button("Step Into (F11)")) {
        debug_->stepInto();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Over (F10)")) {
        debug_->stepOver();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Out (F12)")) {
        debug_->stepOut();
    }
    if (!paused) ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopID();
}

} // namespace LuaEngineDebugger
