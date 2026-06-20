#ifndef DEBUG_WINDOW_BASE_H
#define DEBUG_WINDOW_BASE_H

#include "imgui.h"
#include <string>
#include <map>

namespace LuaEngineDebugWindows {

/**
 * Lightweight base class for debug windows
 * Provides common show/hide/visibility functionality without forcing
 * a specific architecture. All debug windows follow this pattern:
 * - Private show_window_ bool
 * - Public show(), hide(), isVisible() methods
 * - render() method that checks visibility
 */
class DebugWindowBase {
protected:
    bool show_window_;

public:
    DebugWindowBase() : show_window_(false) {}
    virtual ~DebugWindowBase() = default;

    // Standard window control interface
    virtual void show() { show_window_ = true; }
    virtual void hide() { show_window_ = false; }
    virtual bool isVisible() const { return show_window_; }

    // Main render method - must be implemented by derived classes
    virtual void render() = 0;
};

/**
 * Dialog management helper
 * Reduces boilerplate for windows with multiple dialogs
 */
class DialogManager {
private:
    std::map<std::string, bool> dialog_states_;

public:
    DialogManager() = default;

    bool isDialogOpen(const std::string& dialog_name) const {
        auto it = dialog_states_.find(dialog_name);
        return it != dialog_states_.end() && it->second;
    }

    void openDialog(const std::string& dialog_name) {
        dialog_states_[dialog_name] = true;
        ImGui::OpenPopup(dialog_name.c_str());
    }

    void closeDialog(const std::string& dialog_name) {
        dialog_states_[dialog_name] = false;
    }

    void closeAllDialogs() {
        dialog_states_.clear();
    }

    // Helper for rendering modal dialogs with standard pattern
    template<typename RenderFunc>
    void renderModal(const std::string& dialog_name, const char* title,
                     ImGuiWindowFlags flags, RenderFunc render_func) {
        if (isDialogOpen(dialog_name)) {
            if (ImGui::BeginPopupModal(title, nullptr, flags)) {
                render_func();
                ImGui::EndPopup();
            } else {
                // Dialog was closed
                closeDialog(dialog_name);
            }
        }
    }

    // Helper for rendering simple dialogs
    template<typename RenderFunc>
    void renderDialog(const std::string& dialog_name, const char* title,
                      RenderFunc render_func) {
        renderModal(dialog_name, title, ImGuiWindowFlags_AlwaysAutoResize, render_func);
    }
};

/**
 * Menu bar helper - reduces boilerplate for standard menu patterns
 */
class MenuBarHelper {
public:
    static bool beginMenuBar() {
        return ImGui::BeginMenuBar();
    }

    static void endMenuBar() {
        ImGui::EndMenuBar();
    }

    // Helper for standard File menu
    template<typename Func>
    static void renderFileMenu(Func menu_func) {
        if (ImGui::BeginMenu("File")) {
            menu_func();
            ImGui::EndMenu();
        }
    }

    // Helper for standard Edit menu
    template<typename Func>
    static void renderEditMenu(Func menu_func) {
        if (ImGui::BeginMenu("Edit")) {
            menu_func();
            ImGui::EndMenu();
        }
    }

    // Helper for standard View menu
    template<typename Func>
    static void renderViewMenu(Func menu_func) {
        if (ImGui::BeginMenu("View")) {
            menu_func();
            ImGui::EndMenu();
        }
    }

    // Helper for standard Tools menu
    template<typename Func>
    static void renderToolsMenu(Func menu_func) {
        if (ImGui::BeginMenu("Tools")) {
            menu_func();
            ImGui::EndMenu();
        }
    }

    // Helper for standard Help menu
    template<typename Func>
    static void renderHelpMenu(Func menu_func) {
        if (ImGui::BeginMenu("Help")) {
            menu_func();
            ImGui::EndMenu();
        }
    }

    // Menu item with optional shortcut and enabled state
    static bool menuItem(const char* label, const char* shortcut = nullptr,
                         bool selected = false, bool enabled = true) {
        return ImGui::MenuItem(label, shortcut, selected, enabled);
    }

    // Separator for menu sections
    static void separator() {
        ImGui::Separator();
    }
};

/**
 * Common input buffer helper
 * Reduces boilerplate for dialog input fields
 */
struct InputBuffer {
    char* buffer;
    size_t size;

    InputBuffer(char* buf, size_t sz) : buffer(buf), size(sz) {}

    void clear() {
        if (buffer && size > 0) {
            buffer[0] = '\0';
        }
    }

    void set(const std::string& value) {
        if (buffer && size > 0) {
            size_t copy_len = (value.length() < size - 1) ? value.length() : (size - 1);
            memcpy(buffer, value.c_str(), copy_len);
            buffer[copy_len] = '\0';
        }
    }

    std::string get() const {
        return buffer ? std::string(buffer) : std::string();
    }

    bool isEmpty() const {
        return !buffer || buffer[0] == '\0';
    }
};

/**
 * Combo box helper - reduces boilerplate for dropdown selections
 */
class ComboHelper {
public:
    template<typename T>
    static bool renderCombo(const char* label, int* current_item,
                           const T& items, const char* preview = nullptr) {
        if (!preview && *current_item >= 0 && *current_item < static_cast<int>(items.size())) {
            preview = items[*current_item].c_str();
        }

        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                bool is_selected = (*current_item == i);
                if (ImGui::Selectable(items[i].c_str(), is_selected)) {
                    *current_item = i;
                    changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // Helper for enum-based combos
    template<typename EnumType, typename GetNameFunc>
    static bool renderEnumCombo(const char* label, EnumType* current_value,
                               const EnumType* enum_values, size_t count,
                               GetNameFunc get_name) {
        int current_index = -1;
        for (size_t i = 0; i < count; ++i) {
            if (enum_values[i] == *current_value) {
                current_index = static_cast<int>(i);
                break;
            }
        }

        const char* preview = (current_index >= 0) ? get_name(enum_values[current_index]) : nullptr;

        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            for (size_t i = 0; i < count; ++i) {
                bool is_selected = (enum_values[i] == *current_value);
                if (ImGui::Selectable(get_name(enum_values[i]), is_selected)) {
                    *current_value = enum_values[i];
                    changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
};

/**
 * Table helper - reduces boilerplate for ImGui tables
 */
class TableHelper {
public:
    static bool beginTable(const char* str_id, int column_count,
                          ImGuiTableFlags flags = 0) {
        return ImGui::BeginTable(str_id, column_count, flags);
    }

    static void endTable() {
        ImGui::EndTable();
    }

    static void setupColumn(const char* label, ImGuiTableColumnFlags flags = 0,
                           float init_width_or_weight = 0.0f) {
        ImGui::TableSetupColumn(label, flags, init_width_or_weight);
    }

    static void headersRow() {
        ImGui::TableHeadersRow();
    }

    static void nextRow() {
        ImGui::TableNextRow();
    }

    static void nextColumn() {
        ImGui::TableNextColumn();
    }

    static void setColumnIndex(int column_n) {
        ImGui::TableSetColumnIndex(column_n);
    }
};

} // namespace LuaEngineDebugWindows

#endif // DEBUG_WINDOW_BASE_H
