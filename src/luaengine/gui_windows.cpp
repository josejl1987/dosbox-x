#include "gui_windows.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>
#include <iomanip>
#include <cstring>

#include "disassembly_window.h"
#include "core_debug_interface.h"
#include "luaengine.h" // Access LuaEngine for console messages/commands
#include "symbol_manager.h"
#include "trace_logger_window.h"
#include "trace_logger.h"
#include "cheat_window.h"
#include "lua_memory_domains.h"
#include "../gui/imgui_window.h"
#include <imgui/imgui.h>


// DOSBox-X includes
#include "sdlmain.h"
extern SDL_Block sdl;
#include "mem.h"
#include "paging.h"

#ifdef HAVE_SDL
#include <SDL.h>
#endif

// Global Lua engine instance (declared in LuaEngine.cpp)
extern LuaEngine luaEngine;

// Add this function to read a byte from memory
uint8_t readMemoryByte(uint32_t address) {
    // DOSBox-X mem_readb function with bounds checking
    if (address < 1024 * 1024) { // Check if address is within 1MB limit
        return mem_readb(address);
    }
    return 0;
}

// HexEditorWindow is a class, not a namespace - removed using directive

namespace LuaEngineGUIWindows {

// Helper for auto-resizing strings in ImGui
struct InputTextCallback_UserData {
    std::string* Str;
    ImGuiInputTextCallback ChainCallback;
    void* ChainCallbackUserData;
};

static int InputTextCallback(ImGuiInputTextCallbackData* data) {
    InputTextCallback_UserData* user_data = (InputTextCallback_UserData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* str = user_data->Str;
        str->resize(data->BufTextLen);
        data->Buf = (char*)str->c_str();
    }
    if (user_data->ChainCallback) {
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}

// Global window manager instance - now managed by LuaEngine
// This is initialized in LuaEngine::LUAENGINE_Init()
WindowManager* g_window_manager = nullptr;

// ConsoleWindow declaration
class ConsoleWindow : public ToolWindow {
public:
    ConsoleWindow(const std::string& id, const WindowProperties& props);
    void renderContent() override;
private:
    char input_buffer_[512];
    bool scroll_to_bottom_;
    void runCommand();
};

// ToolWindow implementation
ToolWindow::ToolWindow(const std::string& id, const WindowProperties& props)
    : id_(id), properties_(props), is_open_(true), needs_focus_(false), 
      dirty_(false), lua_state_(nullptr) {
    
    // Set default ImGui flags based on properties
    applyWindowFlags();
}

ToolWindow::~ToolWindow() {
    // Cleanup
}

void ToolWindow::render() {
    if (!isVisible()) return;
    
    bool pushed_id = false;
    if (!id_.empty()) {
        ImGui::PushID(id_.c_str());
        pushed_id = true;
    }
    
    // Apply window flags
    applyWindowFlags();
    
    // Set window properties
    if (properties_.pos_x >= 0 && properties_.pos_y >= 0) {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(properties_.pos_x), 
                                      static_cast<float>(properties_.pos_y)), ImGuiCond_FirstUseEver);
    }
    
    if (properties_.default_width > 0 && properties_.default_height > 0) {
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(properties_.default_width), 
                                       static_cast<float>(properties_.default_height)), ImGuiCond_FirstUseEver);
    }
    
    if (properties_.min_width > 0 || properties_.min_height > 0) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(static_cast<float>(properties_.min_width), static_cast<float>(properties_.min_height)),
            ImVec2(static_cast<float>(properties_.max_width), static_cast<float>(properties_.max_height))
        );
    }
    
    if (needs_focus_) {
        ImGui::SetNextWindowFocus();
        needs_focus_ = false;
    }
    
    // Begin window
    if (ImGui::Begin(properties_.title.c_str(), &is_open_, properties_.imgui_flags)) {
        // Handle window events
        handleWindowEvents();
        
        // Render window content
        renderContent();
        
        // Call Lua draw function if available
        if (lua_draw_func_.valid()) {
            try {
                lua_draw_func_();
            } catch (const sol::error& e) {
                std::cerr << "[WindowSystem] Lua draw function error: " << e.what() << std::endl;
            }
        }
        
        // Call draw callback if available
        if (draw_callback_) {
            draw_callback_();
        }
    }
    
    ImGui::End();
    
    // Check if window was closed
    if (!is_open_ && close_callback_) {
        bool allow_close = close_callback_();
        if (!allow_close) {
            is_open_ = true; // Prevent closing
        }
    }

    if (pushed_id) ImGui::PopID();
}

void ToolWindow::update() {
    // Call Lua update function if available
    if (lua_update_func_.valid()) {
        try {
            lua_update_func_();
        } catch (const sol::error& e) {
            std::cerr << "[WindowSystem] Lua update function error: " << e.what() << std::endl;
        }
    }
}

bool ToolWindow::shouldClose() {
    return !is_open_;
}

void ToolWindow::onClose() {
    if (close_callback_) {
        close_callback_();
    }
}

void ToolWindow::onFocus(bool focused) {
    if (focus_callback_) {
        focus_callback_(focused);
    }
}

void ToolWindow::onResize(int width, int height) {
    if (resize_callback_) {
        resize_callback_(width, height);
    }
}

void ToolWindow::onMove(int x, int y) {
    if (move_callback_) {
        move_callback_(x, y);
    }
}

void ToolWindow::applyWindowFlags() {
    properties_.imgui_flags = 0;
    
    if (!properties_.can_resize) properties_.imgui_flags |= ImGuiWindowFlags_NoResize;
    if (!properties_.can_move) properties_.imgui_flags |= ImGuiWindowFlags_NoMove;
    if (!properties_.can_collapse) properties_.imgui_flags |= ImGuiWindowFlags_NoCollapse;
    if (properties_.no_title_bar) properties_.imgui_flags |= ImGuiWindowFlags_NoTitleBar;
    if (properties_.no_scrollbar) properties_.imgui_flags |= ImGuiWindowFlags_NoScrollbar;
    // ImGuiWindowFlags_NoMenuBar not available in this ImGui version
    // if (properties_.no_menu) properties_.imgui_flags |= ImGuiWindowFlags_NoMenuBar;
    if (properties_.always_on_top) properties_.imgui_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (properties_.auto_resize) properties_.imgui_flags |= ImGuiWindowFlags_AlwaysAutoResize;
}

void ToolWindow::handleWindowEvents() {
    // Check for focus changes - simplified due to ImGui version compatibility
    static bool previous_focus = false;
    bool current_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (current_focus != previous_focus) {
        onFocus(current_focus);
        previous_focus = current_focus;
    }
    
    // Check for size changes
    ImVec2 current_size = ImGui::GetWindowSize();
    static ImVec2 previous_size = current_size;
    if (current_size.x != previous_size.x || current_size.y != previous_size.y) {
        onResize(static_cast<int>(current_size.x), static_cast<int>(current_size.y));
        previous_size = current_size;
    }
    
    // Check for position changes
    ImVec2 current_pos = ImGui::GetWindowPos();
    static ImVec2 previous_pos = current_pos;
    if (current_pos.x != previous_pos.x || current_pos.y != previous_pos.y) {
        onMove(static_cast<int>(current_pos.x), static_cast<int>(current_pos.y));
        previous_pos = current_pos;
    }
}

// CustomLuaWindow implementation
CustomLuaWindow::CustomLuaWindow(const std::string& id, const WindowProperties& props, sol::table lua_table)
    : ToolWindow(id, props), lua_window_table_(lua_table) {
}

void CustomLuaWindow::renderContent() {
    // Content is rendered through Lua callbacks
}

void CustomLuaWindow::update() {
    ToolWindow::update();
}

void CustomLuaWindow::setTitle(const std::string& title) {
    properties_.title = title;
}

void CustomLuaWindow::setSize(int width, int height) {
    ImGui::SetWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
}

void CustomLuaWindow::setPosition(int x, int y) {
    ImGui::SetWindowPos(ImVec2(static_cast<float>(x), static_cast<float>(y)));
}

void CustomLuaWindow::addButton(const std::string& label, sol::function callback) {
    if (ImGui::Button(label.c_str())) {
        if (callback.valid()) {
            try {
                callback();
            } catch (const sol::error& e) {
                std::cerr << "[WindowSystem] Button callback error: " << e.what() << std::endl;
            }
        }
    }
}

void CustomLuaWindow::addText(const std::string& text) {
    ImGui::Text("%s", text.c_str());
}

void CustomLuaWindow::addInputText(const std::string& label, std::string& buffer) {
    // Set up user data for the callback
    InputTextCallback_UserData user_data;
    user_data.Str = &buffer;
    user_data.ChainCallback = nullptr;
    user_data.ChainCallbackUserData = nullptr;
    
    // Use the resize callback to allow unlimited input length
    ImGui::InputText(label.c_str(), (char*)buffer.c_str(),
                     buffer.capacity() + 1,
                     ImGuiInputTextFlags_CallbackResize,
                     InputTextCallback, &user_data);
}

void CustomLuaWindow::addSlider(const std::string& label, float& value, float min_val, float max_val) {
    ImGui::SliderFloat(label.c_str(), &value, min_val, max_val);
}

void CustomLuaWindow::addCheckbox(const std::string& label, bool& value) {
    ImGui::Checkbox(label.c_str(), &value);
}

void CustomLuaWindow::addCombo(const std::string& label, int& current_item, const std::vector<std::string>& items) {
    if (ImGui::BeginCombo(label.c_str(), current_item >= 0 && current_item < items.size() ? items[current_item].c_str() : "")) {
        for (int i = 0; i < items.size(); i++) {
            bool is_selected = (current_item == i);
            if (ImGui::Selectable(items[i].c_str(), is_selected)) {
                current_item = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void CustomLuaWindow::addSeparator() {
    ImGui::Separator();
}

void CustomLuaWindow::addSpacing() {
    ImGui::Spacing();
}

bool CustomLuaWindow::isItemHovered() const {
    return ImGui::IsItemHovered();
}

bool CustomLuaWindow::isItemActive() const {
    return ImGui::IsItemActive();
}

void CustomLuaWindow::setTooltip(const std::string& text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.c_str());
    }
}

// ConsoleWindow implementation
ConsoleWindow::ConsoleWindow(const std::string& id, const WindowProperties& props)
    : ToolWindow(id, props), scroll_to_bottom_(false) {
    properties_.title = "Lua Console";
    properties_.type = WindowType::SCRIPT_CONSOLE;
    properties_.default_width = 600;
    properties_.default_height = 400;
    memset(input_buffer_, 0, sizeof(input_buffer_));
}

void ConsoleWindow::runCommand() {
    if (input_buffer_[0] == '\0') return;
    std::string cmd = input_buffer_;
    try {
        luaEngine.log_info("Console command: " + cmd);
        luaEngine.lua.script(cmd);
    } catch (const sol::error& e) {
        luaEngine.log_error("Lua error: " + std::string(e.what()));
    }
    memset(input_buffer_, 0, sizeof(input_buffer_));
    scroll_to_bottom_ = true;
}

void ConsoleWindow::renderContent() {
    // Output area
    ImGui::BeginChild("ConsoleOutput", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
    {
        std::lock_guard<std::mutex> lock(luaEngine.uiState.console_mutex);
        for (const auto& msg : luaEngine.uiState.console_messages) {
            ImGui::TextUnformatted(msg.c_str());
        }
        if (scroll_to_bottom_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom_ = false;
        }
    }
    ImGui::EndChild();

    // Input line
    ImGui::PushID(this);
    ImGui::PushItemWidth(-70);
    if (ImGui::InputText("##console_input", input_buffer_, sizeof(input_buffer_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        runCommand();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Run")) {
        runCommand();
    }
    ImGui::PopID();
}

// HexEditorWindow implementation
HexEditorWindow::HexEditorWindow(const std::string& id, uint32_t base_addr)
    : ToolWindow(id, WindowProperties{}), base_address_(base_addr), current_address_(base_addr),
      bytes_per_row_(16), show_ascii_(true), show_addresses_(true), cache_size_(1024),
      follow_pointer_(false), follow_address_(0) {
    
    properties_.title = "Hex Editor";
    properties_.type = WindowType::HEX_EDITOR;
    properties_.default_width = 600;
    properties_.default_height = 400;
    
    memory_cache_.resize(cache_size_);
    refreshMemory();
}

void HexEditorWindow::renderContent() {
    // Address input
    ImGui::Text("Address:");
    ImGui::SameLine();
    
    char addr_buffer[16];
    snprintf(addr_buffer, sizeof(addr_buffer), "%08X", current_address_);
    if (ImGui::InputText("##address", addr_buffer, sizeof(addr_buffer), ImGuiInputTextFlags_CharsHexadecimal)) {
        uint32_t new_addr = static_cast<uint32_t>(strtoul(addr_buffer, nullptr, 16));
        gotoAddress(new_addr);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        refreshMemory();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        refreshMemory();
    }
    
    // Options
    ImGui::Checkbox("Show ASCII", &show_ascii_);
    ImGui::SameLine();
    ImGui::Checkbox("Show Addresses", &show_addresses_);
    
    ImGui::Separator();
    
    // Memory display
    ImGui::BeginChild("MemoryView", ImVec2(0, 0), true);
    
    ImGuiListClipper clipper;
    int total_rows = static_cast<int>(cache_size_ / bytes_per_row_);
    clipper.Begin(total_rows);
    
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            ImGui::PushID(row);
            
            if (show_addresses_) {
                renderAddressColumn(row);
                ImGui::SameLine();
            }
            
            renderHexColumn(row);
            
            if (show_ascii_) {
                ImGui::SameLine();
                renderAsciiColumn(row);
            }
            
            ImGui::PopID();
        }
    }
    
    ImGui::EndChild();
}

void HexEditorWindow::update() {
    ToolWindow::update();
    
    // Update memory if following pointer
    if (follow_pointer_) {
        uint32_t new_addr = readMemoryByte(follow_address_) | 
                           (readMemoryByte(follow_address_ + 1) << 8) |
                           (readMemoryByte(follow_address_ + 2) << 16) |
                           (readMemoryByte(follow_address_ + 3) << 24);
        if (new_addr != current_address_) {
            gotoAddress(new_addr);
        }
    }
}

void HexEditorWindow::setBaseAddress(uint32_t address) {
    base_address_ = address;
    gotoAddress(address);
}

void HexEditorWindow::gotoAddress(uint32_t address) {
    current_address_ = address;
    refreshMemory();
}

void HexEditorWindow::setBytesPerRow(int bytes) {
#undef max
#undef min
    bytes_per_row_ = std::max(1, std::min(32, bytes));
}

void HexEditorWindow::setFollowPointer(uint32_t address) {
    follow_pointer_ = true;
    follow_address_ = address;
}

void HexEditorWindow::refreshMemory() {
    for (size_t i = 0; i < cache_size_; i++) {
        memory_cache_[i] = readMemoryByte(current_address_ + static_cast<uint32_t>(i));
    }
    markDirty();
}

void HexEditorWindow::renderAddressColumn(int row) {
    uint32_t row_address = current_address_ + (row * bytes_per_row_);
    ImGui::Text("%08X:", row_address);
}

void HexEditorWindow::renderHexColumn(int row) {
    for (int col = 0; col < bytes_per_row_; col++) {
        int index = row * bytes_per_row_ + col;
        if (index >= cache_size_) break;
        
        ImGui::SameLine();
        
        uint8_t byte_value = memory_cache_[index];
        char hex_buffer[4];
        snprintf(hex_buffer, sizeof(hex_buffer), "%02X", byte_value);
        
        ImGui::PushID(index);
        if (ImGui::Selectable(hex_buffer, false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(20, 0))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                // Open edit dialog for this byte
            }
        }
        ImGui::PopID();
        
        // Add spacing between byte groups
        if ((col + 1) % 4 == 0 && col + 1 < bytes_per_row_) {
            ImGui::SameLine();
            ImGui::Text(" ");
        }
    }
}

void HexEditorWindow::renderAsciiColumn(int row) {
    ImGui::Text("|");
    ImGui::SameLine();
    
    for (int col = 0; col < bytes_per_row_; col++) {
        int index = row * bytes_per_row_ + col;
        if (index >= cache_size_) break;
        
        uint8_t byte_value = memory_cache_[index];
        char ascii_char = (byte_value >= 32 && byte_value <= 126) ? static_cast<char>(byte_value) : '.';
        
        ImGui::SameLine();
        ImGui::Text("%c", ascii_char);
    }
    
    ImGui::SameLine();
    ImGui::Text("|");
}

uint8_t HexEditorWindow::readMemoryByte(uint32_t address) {
    uint8_t value;
    if (mem_readb_checked(address, &value)) {
        return value;
    }
    return 0;
}

void HexEditorWindow::writeMemoryByte(uint32_t address, uint8_t value) {
    mem_writeb(address, value);
}

// WatchListWindow implementation
WatchListWindow::WatchListWindow(const std::string& id)
    : ToolWindow(id, WindowProperties{}), add_address_(0), add_type_index_(0) {
    
    properties_.title = "Watch List";
    properties_.type = WindowType::WATCH_LIST;
    properties_.default_width = 500;
    properties_.default_height = 300;
    
    data_types_ = {"uint8", "int8", "uint16", "int16", "uint32", "int32", "float", "double"};
    add_name_ = "New Watch";
}

void WatchListWindow::renderContent() {
    renderAddWatchSection();
    ImGui::Separator();
    renderWatchTable();
}

void WatchListWindow::update() {
    ToolWindow::update();
    updateValues();
}

void WatchListWindow::addWatch(const std::string& name, uint32_t address, const std::string& data_type) {
    watch_entries_.emplace_back(name, address, data_type);
}

void WatchListWindow::removeWatch(int index) {
    if (index >= 0 && index < watch_entries_.size()) {
        watch_entries_.erase(watch_entries_.begin() + index);
    }
}

void WatchListWindow::clearWatches() {
    watch_entries_.clear();
}

void WatchListWindow::updateValues() {
    for (auto& entry : watch_entries_) {
        if (entry.enabled) {
            entry.previous_value = entry.current_value;
            entry.current_value = formatValue(entry);
            entry.changed = (entry.current_value != entry.previous_value);
        }
    }
}

std::string WatchListWindow::formatValue(const WatchEntry& entry) {
    std::ostringstream oss;
    
    if (entry.data_type == "uint8") {
        uint8_t value = readMemoryByte(entry.address);
        oss << static_cast<unsigned>(value);
    } else if (entry.data_type == "int8") {
        int8_t value = static_cast<int8_t>(readMemoryByte(entry.address));
        oss << static_cast<int>(value);
    } else if (entry.data_type == "uint16") {
        uint16_t value = readMemoryByte(entry.address) | (readMemoryByte(entry.address + 1) << 8);
        oss << value;
    } else if (entry.data_type == "int16") {
        int16_t value = static_cast<int16_t>(readMemoryByte(entry.address) | (readMemoryByte(entry.address + 1) << 8));
        oss << value;
    } else if (entry.data_type == "uint32") {
        uint32_t value = readMemoryByte(entry.address) | 
                        (readMemoryByte(entry.address + 1) << 8) |
                        (readMemoryByte(entry.address + 2) << 16) |
                        (readMemoryByte(entry.address + 3) << 24);
        oss << value;
    } else if (entry.data_type == "int32") {
        int32_t value = static_cast<int32_t>(readMemoryByte(entry.address) | 
                                           (readMemoryByte(entry.address + 1) << 8) |
                                           (readMemoryByte(entry.address + 2) << 16) |
                                           (readMemoryByte(entry.address + 3) << 24));
        oss << value;
    } else {
        oss << "Unknown type";
    }
    
    return oss.str();
}

void WatchListWindow::renderAddWatchSection() {
    ImGui::Text("Add Watch:");
    
    // Set up user data for the callback
    InputTextCallback_UserData name_user_data;
    name_user_data.Str = &add_name_;
    name_user_data.ChainCallback = nullptr;
    name_user_data.ChainCallbackUserData = nullptr;
    
    // Use the resize callback to allow unlimited input length
    ImGui::InputText("Name", (char*)add_name_.c_str(),
                     add_name_.capacity() + 1,
                     ImGuiInputTextFlags_CallbackResize,
                     InputTextCallback, &name_user_data);
    
    char addr_buffer[16];
    snprintf(addr_buffer, sizeof(addr_buffer), "%08X", add_address_);
    if (ImGui::InputText("Address", addr_buffer, sizeof(addr_buffer), ImGuiInputTextFlags_CharsHexadecimal)) {
        add_address_ = static_cast<uint32_t>(strtoul(addr_buffer, nullptr, 16));
    }
    
    if (ImGui::BeginCombo("Type", data_types_[add_type_index_].c_str())) {
        for (int i = 0; i < data_types_.size(); i++) {
            bool is_selected = (add_type_index_ == i);
            if (ImGui::Selectable(data_types_[i].c_str(), is_selected)) {
                add_type_index_ = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    if (ImGui::Button("Add Watch")) {
        addWatch(add_name_, add_address_, data_types_[add_type_index_]);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        clearWatches();
    }
}

void WatchListWindow::renderWatchTable() {
    if (ImGui::BeginTable("WatchTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();
        
        for (int i = 0; i < watch_entries_.size(); i++) {
            auto& entry = watch_entries_[i];
            ImGui::TableNextRow();
            
            // Scope IDs per row to avoid collisions in interactive elements
            ImGui::PushID(i);
            
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox(("##enabled" + std::to_string(i)).c_str(), &entry.enabled);
            
            ImGui::TableSetColumnIndex(1);
            if (entry.changed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow for changed
            }
            ImGui::Text("%s", entry.name.c_str());
            if (entry.changed) {
                ImGui::PopStyleColor();
            }
            
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%08X", entry.address);
            
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", entry.data_type.c_str());
            
            ImGui::TableSetColumnIndex(4);
            if (entry.changed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow for changed
            }
            ImGui::Text("%s", entry.current_value.c_str());
            if (entry.changed) {
                ImGui::PopStyleColor();
            }
            
            // Context menu for removing entries
            if (ImGui::BeginPopupContextItem("GuiWindowsContextMenu")) {
                if (ImGui::MenuItem("Remove")) {
                    removeWatch(i);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

// MemorySearchWindow implementation
MemorySearchWindow::MemorySearchWindow(const std::string& id)
    : ToolWindow(id, WindowProperties{}), search_type_index_(0), search_comparison_index_(0),
      first_search_(true), search_start_address_(0), search_end_address_(0x100000) {
    
    properties_.title = "Memory Search";
    properties_.type = WindowType::RAM_SEARCH;
    properties_.default_width = 600;
    properties_.default_height = 400;
    
    search_types_ = {"uint8", "int8", "uint16", "int16", "uint32", "int32"};
    search_comparisons_ = {"Equal to", "Not equal to", "Greater than", "Less than", "Changed", "Unchanged"};
    search_value_ = "0";
}

void MemorySearchWindow::renderContent() {
    renderSearchControls();
    ImGui::Separator();
    renderSearchResults();
}

void MemorySearchWindow::update() {
    ToolWindow::update();
}

void MemorySearchWindow::performSearch() {
    search_results_.clear();
    
    // Perform initial search across entire memory range
    for (uint32_t addr = search_start_address_; addr < search_end_address_; addr++) {
        std::string current_value = "0"; // Would read actual memory value
        
        if (matchesSearch(addr, current_value)) {
            SearchResult result;
            result.address = addr;
            result.value = current_value;
            result.previous_value = current_value;
            result.changed = false;
            search_results_.push_back(result);
        }
        
        // Limit results to prevent UI slowdown
        if (search_results_.size() > 10000) break;
    }
    
    first_search_ = false;
}

void MemorySearchWindow::performNextSearch() {
    if (first_search_) {
        performSearch();
        return;
    }
    
    // Filter existing results
    auto it = search_results_.begin();
    while (it != search_results_.end()) {
        std::string current_value = "0"; // Would read actual memory value
        
        if (matchesSearch(it->address, current_value)) {
            it->previous_value = it->value;
            it->value = current_value;
            it->changed = (it->value != it->previous_value);
            ++it;
        } else {
            it = search_results_.erase(it);
        }
    }
}

void MemorySearchWindow::resetSearch() {
    search_results_.clear();
    first_search_ = true;
}

void MemorySearchWindow::setSearchRange(uint32_t start, uint32_t end) {
    search_start_address_ = start;
    search_end_address_ = end;
}

void MemorySearchWindow::renderSearchControls() {
    ImGui::Text("Search Parameters:");
    
    // Set up user data for the callback
    InputTextCallback_UserData value_user_data;
    value_user_data.Str = &search_value_;
    value_user_data.ChainCallback = nullptr;
    value_user_data.ChainCallbackUserData = nullptr;
    
    // Use the resize callback to allow unlimited input length
    ImGui::InputText("Value", (char*)search_value_.c_str(),
                     search_value_.capacity() + 1,
                     ImGuiInputTextFlags_CallbackResize,
                     InputTextCallback, &value_user_data);
    
    if (ImGui::BeginCombo("Type", search_types_[search_type_index_].c_str())) {
        for (int i = 0; i < search_types_.size(); i++) {
            bool is_selected = (search_type_index_ == i);
            if (ImGui::Selectable(search_types_[i].c_str(), is_selected)) {
                search_type_index_ = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    if (ImGui::BeginCombo("Comparison", search_comparisons_[search_comparison_index_].c_str())) {
        for (int i = 0; i < search_comparisons_.size(); i++) {
            bool is_selected = (search_comparison_index_ == i);
            if (ImGui::Selectable(search_comparisons_[i].c_str(), is_selected)) {
                search_comparison_index_ = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    char start_buffer[16], end_buffer[16];
    snprintf(start_buffer, sizeof(start_buffer), "%08X", search_start_address_);
    snprintf(end_buffer, sizeof(end_buffer), "%08X", search_end_address_);
    
    if (ImGui::InputText("Start Address", start_buffer, sizeof(start_buffer), ImGuiInputTextFlags_CharsHexadecimal)) {
        search_start_address_ = static_cast<uint32_t>(strtoul(start_buffer, nullptr, 16));
    }
    
    if (ImGui::InputText("End Address", end_buffer, sizeof(end_buffer), ImGuiInputTextFlags_CharsHexadecimal)) {
        search_end_address_ = static_cast<uint32_t>(strtoul(end_buffer, nullptr, 16));
    }
    
    if (ImGui::Button("New Search")) {
        resetSearch();
        performSearch();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Next Search")) {
        performNextSearch();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        resetSearch();
    }
    
    ImGui::Text("Results: %zu", search_results_.size());
}

void MemorySearchWindow::renderSearchResults() {
    if (ImGui::BeginTable("SearchResults", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Changed", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(search_results_.size()));
        
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& result = search_results_[i];
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%08X", result.address);
                
                ImGui::TableSetColumnIndex(1);
                if (result.changed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                }
                ImGui::Text("%s", result.value.c_str());
                if (result.changed) {
                    ImGui::PopStyleColor();
                }
                
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", result.previous_value.c_str());
                
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", result.changed ? "Yes" : "No");
                
                // Double-click to add to watch list
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    // Add to watch list if available
                    if (g_window_manager) {
                        auto* watch_window = dynamic_cast<WatchListWindow*>(g_window_manager->getWindow("watch_list"));
                        if (watch_window) {
                            std::string name = "Addr_" + std::to_string(result.address);
                            watch_window->addWatch(name, result.address, search_types_[search_type_index_]);
                            g_window_manager->showWatchList();
                        }
                    }
                }
            }
        }
        
        ImGui::EndTable();
    }
}

bool MemorySearchWindow::matchesSearch(uint32_t address, const std::string& value) {
    // Simplified matching logic - would need actual implementation
    // based on search type and comparison
    return true;
}

// DockingManager implementation
DockingManager::DockingManager() : docking_enabled_(true), dockspace_visible_(true) {
}

void DockingManager::renderDockspace() {
    if (!dockspace_visible_ || !docking_enabled_) return;
    
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar ;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", &dockspace_visible_, window_flags);
    ImGui::PopStyleVar(3);
    
    // DockSpace functionality not available in this ImGui version
    // Simplified to just create a main window container
    ImGui::Text("Tool Windows Container");
    ImGui::Text("(Docking not available in this ImGui version)");
    
    ImGui::End();
}

void DockingManager::dockWindow(const std::string& window_id, const std::string& dock_name) {
    // Implementation would require storing dock relationships
}

void DockingManager::createDockLayout() {
    // Create default dock layout
}

void DockingManager::saveDockLayout(const std::string& filename) {
    // Save docking layout to file
}

void DockingManager::loadDockLayout(const std::string& filename) {
    // Load docking layout from file
}

// WindowManager implementation
WindowManager::WindowManager() : initialized_(false), lua_state_(nullptr) {
}

WindowManager::~WindowManager() {
    shutdown();
}

bool WindowManager::initialize(sol::state* lua_state) {
    if (initialized_.load()) return true;

    if (!sdl.window) {
        std::cerr << "[WindowSystem] Cannot initialize: no SDL window" << std::endl;
        return false;
    }

    if (!ImGui::GetCurrentContext()) {
        if (!InitImGui(sdl.window)) {
            std::cerr << "[WindowSystem] ImGui initialization failed" << std::endl;
            return false;
        }
    } else {
        std::cout << "[WindowSystem] ImGui already initialized, reusing context" << std::endl;
    }

    lua_state_ = lua_state;

    // Setup built-in windows
    setupBuiltinWindows();

    // Register Lua API
    registerLuaAPI();

    initialized_ = true;
    std::cout << "[WindowSystem] Window manager initialized" << std::endl;

    return true;
}

void WindowManager::shutdown() {
    if (!initialized_.load()) return;

    std::lock_guard<std::mutex> lock(windows_mutex_);
    windows_.clear();
    hex_editor_.reset();
    watch_list_.reset();
    memory_search_.reset();
    disassembly_window_.reset();
    trace_logger_window_.reset();
    trace_logger_.reset();
    cheat_window_.reset();

    CleanupImGui();

    initialized_ = false;
    std::cout << "[WindowSystem] Window manager shutdown" << std::endl;
}

std::string WindowManager::createWindow(const WindowProperties& props) {
    std::string window_id = generateWindowId();
    
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto window = std::make_unique<CustomLuaWindow>(window_id, props, sol::table{});
    windows_[window_id] = std::move(window);
    
    return window_id;
}

std::string WindowManager::createLuaWindow(const std::string& title, sol::table lua_table) {
    WindowProperties props;
    props.title = title;
    
    std::string window_id = generateWindowId();
    
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto window = std::make_unique<CustomLuaWindow>(window_id, props, lua_table);
    window->setLuaState(lua_state_);
    windows_[window_id] = std::move(window);
    
    return window_id;
}

bool WindowManager::destroyWindow(const std::string& window_id) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        windows_.erase(it);
        return true;
    }
    return false;
}

ToolWindow* WindowManager::getWindow(const std::string& window_id) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto it = windows_.find(window_id);
    return (it != windows_.end()) ? it->second.get() : nullptr;
}

void WindowManager::showHexEditor(uint32_t base_address) {
    if (!hex_editor_) {
        hex_editor_ = std::make_unique<HexEditorWindow>("hex_editor", base_address);
    } else {
        hex_editor_->setBaseAddress(base_address);
    }
    hex_editor_->show();
}

void WindowManager::showWatchList() {
    if (!watch_list_) {
        watch_list_ = std::make_unique<WatchListWindow>("watch_list");
    }
    watch_list_->show();
}

void WindowManager::showMemorySearch() {
    if (!memory_search_) {
        memory_search_ = std::make_unique<MemorySearchWindow>("memory_search");
    }
    memory_search_->show();
}

void WindowManager::showDisassembly() {
    ensureDisassemblyWindow();
    if (disassembly_window_) {
        disassembly_window_->show();
    }
}

void WindowManager::showTraceLogger() {
    ensureTraceLoggerWindow();
    if (trace_logger_window_) trace_logger_window_->show();
}

void WindowManager::showCheatEngine() {
    ensureCheatWindow();
    if (cheat_window_) cheat_window_->show();
}

void WindowManager::showConsole() {
    ensureConsoleWindow();
    if (console_window_) console_window_->show();
}

void WindowManager::hideHexEditor() {
    if (hex_editor_) hex_editor_->hide();
}

void WindowManager::hideWatchList() {
    if (watch_list_) watch_list_->hide();
}

void WindowManager::hideMemorySearch() {
    if (memory_search_) memory_search_->hide();
}

void WindowManager::hideDisassembly() {
    if (disassembly_window_) disassembly_window_->hide();
}

bool WindowManager::isDisassemblyVisible() const {
    return disassembly_window_ && disassembly_window_->isVisible();
}

bool WindowManager::isHexEditorVisible() const {
    return hex_editor_ && hex_editor_->isVisible();
}

bool WindowManager::isWatchListVisible() const {
    return watch_list_ && watch_list_->isVisible();
}

bool WindowManager::isMemorySearchVisible() const {
    return memory_search_ && memory_search_->isVisible();
}

void WindowManager::hideTraceLogger() {
    if (trace_logger_window_) trace_logger_window_->hide();
}

void WindowManager::hideCheatEngine() {
    if (cheat_window_) cheat_window_->hide();
}

void WindowManager::hideConsole() {
    if (console_window_) console_window_->hide();
}

bool WindowManager::isTraceLoggerVisible() const {
    return trace_logger_window_ && trace_logger_window_->isVisible();
}

bool WindowManager::isCheatEngineVisible() const {
    return cheat_window_ && cheat_window_->isVisible();
}

bool WindowManager::isConsoleVisible() const {
    return console_window_ && console_window_->isVisible();
}

LuaEngineTraceLogger::TraceLogger* WindowManager::getTraceLogger() {
    if (!trace_logger_) {
        trace_logger_ = std::make_unique<LuaEngineTraceLogger::TraceLogger>();
        trace_logger_->initialize(LuaEngineDebug::g_core_debugger, LuaEngineSymbols::g_symbol_manager);
    }
    return trace_logger_.get();
}

void WindowManager::ensureDisassemblyWindow() {
    if (disassembly_window_) return;

    // Symbol manager is now initialized by LuaEngine
    // No need to create it here
    ensureTraceLoggerWindow();

    disassembly_window_ = std::make_unique<LuaEngineDebugger::DisassemblyWindow>();
    // Pass the debugger even if it is not initialized yet; the window handles a null interface
    disassembly_window_->initialize(LuaEngineDebug::g_core_debugger,
                                    getTraceLogger(),
                                    trace_logger_window_.get(),
                                    LuaEngineSymbols::g_symbol_manager);
}

void WindowManager::ensureTraceLoggerWindow() {
    if (trace_logger_window_) return;

    trace_logger_window_ = std::make_unique<LuaEngineTraceLogger::TraceLoggerWindow>();
    trace_logger_window_->initialize(getTraceLogger());
}

void WindowManager::ensureCheatWindow() {
    if (cheat_window_) return;

    cheat_window_ = std::make_unique<LuaEngineCheatEngine::CheatWindow>();
    cheat_window_->initialize(LuaEngineMemoryDomains::GetGlobalMemoryDomainManager());
}

void WindowManager::ensureConsoleWindow() {
    if (console_window_) return;

    WindowProperties props;
    props.title = "Lua Console";
    props.type = WindowType::SCRIPT_CONSOLE;
    props.default_width = 600;
    props.default_height = 400;
    console_window_ = std::make_unique<ConsoleWindow>("lua_console", props);
}

void WindowManager::renderAllWindows() {
    if (!initialized_.load()) return;
    


    // Render dockspace
    docking_manager_.renderDockspace();
    
    // Render built-in windows
    if (hex_editor_ && hex_editor_->isVisible()) {
        hex_editor_->render();
    }
    if (watch_list_ && watch_list_->isVisible()) {
        watch_list_->render();
    }
    if (memory_search_ && memory_search_->isVisible()) {
        memory_search_->render();
    }
    if (disassembly_window_ && disassembly_window_->isVisible()) {
        disassembly_window_->render();
    }
    if (trace_logger_window_ && trace_logger_window_->isVisible()) {
        trace_logger_window_->render();
    }
    if (cheat_window_ && cheat_window_->isVisible()) {
        cheat_window_->render();
    }
    if (console_window_ && console_window_->isVisible()) {
        console_window_->render();
    }
    
    // Render custom windows
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto it = windows_.begin();
    while (it != windows_.end()) {
        auto& window = it->second;
        if (window->shouldClose()) {
            window->onClose();
            it = windows_.erase(it);
        } else {
            if (window->isVisible()) {
                window->render();
            }
            ++it;
        }
    }
}

void WindowManager::updateAllWindows() {
    if (!initialized_.load()) return;
    
    // Update built-in windows
    if (hex_editor_) hex_editor_->update();
    if (watch_list_) watch_list_->update();
    if (memory_search_) memory_search_->update();
    if (console_window_) console_window_->update();
    
    // Update custom windows
    std::lock_guard<std::mutex> lock(windows_mutex_);
    for (auto& [id, window] : windows_) {
        window->update();
    }
}

std::vector<std::string> WindowManager::getWindowIds() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, window] : windows_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<ToolWindow*> WindowManager::getVisibleWindows() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    std::vector<ToolWindow*> visible;
    for (const auto& [id, window] : windows_) {
        if (window->isVisible()) {
            visible.push_back(window.get());
        }
    }
    return visible;
}

int WindowManager::getWindowCount() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    return static_cast<int>(windows_.size());
}

void WindowManager::enableDocking(bool enabled) {
    docking_manager_.enableDocking(enabled);
}

void WindowManager::showDockspace(bool show) {
    docking_manager_.showDockspace(show);
}

void WindowManager::saveWindowStates(const std::string& filename) {
    // Implementation for saving window states
}

void WindowManager::loadWindowStates(const std::string& filename) {
    // Implementation for loading window states
}


void WindowManager::onSDLEvent(const SDL_Event& event) {
    // Only process mouse events for ImGui if DOSBox-X has not locked the mouse
    if (event.type == SDL_MOUSEMOTION ||
        event.type == SDL_MOUSEBUTTONDOWN ||
        event.type == SDL_MOUSEBUTTONUP) {
        if (!sdl.mouse.locked) {
            ImGui_ImplSDL2_ProcessEvent(&event);
        }
    } else {
        // Process all other events for ImGui unconditionally
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}

std::string WindowManager::generateWindowId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1000, 9999);
    
    return "window_" + std::to_string(dis(gen));
}

void WindowManager::registerLuaAPI() {
    if (!lua_state_) return;
    
    // Create window management table
    sol::table window_table = lua_state_->create_table();
    
    // Window creation functions
    window_table["create"] = [this](const std::string& title) -> std::string {
        WindowProperties props;
        props.title = title;
        return createWindow(props);
    };
    
    window_table["createLua"] = [this](const std::string& title, sol::table lua_table) -> std::string {
        return createLuaWindow(title, lua_table);
    };

    // Built-in tool helpers
    window_table["showDisassembly"] = [this]() {
        showDisassembly();
    };
    window_table["hideDisassembly"] = [this]() {
        hideDisassembly();
    };
    window_table["showHexEditor"] = [this](uint32_t base) {
        showHexEditor(base);
    };
    window_table["hideHexEditor"] = [this]() {
        hideHexEditor();
    };
    window_table["showWatchList"] = [this]() {
        showWatchList();
    };
    window_table["hideWatchList"] = [this]() {
        hideWatchList();
    };
    window_table["showMemorySearch"] = [this]() {
        showMemorySearch();
    };
    window_table["hideMemorySearch"] = [this]() {
        hideMemorySearch();
    };
    window_table["showTraceLogger"] = [this]() {
        showTraceLogger();
    };
    window_table["hideTraceLogger"] = [this]() {
        hideTraceLogger();
    };
    window_table["showCheatEngine"] = [this]() {
        showCheatEngine();
    };
    window_table["hideCheatEngine"] = [this]() {
        hideCheatEngine();
    };
    window_table["showConsole"] = [this]() {
        showConsole();
    };
    window_table["hideConsole"] = [this]() {
        hideConsole();
    };
    
    // Multi-viewport control functions
    window_table["enableMultiViewport"] = []() {
        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            
            // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    };
    
    window_table["disableMultiViewport"] = []() {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    };
    
    window_table["isMultiViewportEnabled"] = []() -> bool {
        ImGuiIO& io = ImGui::GetIO();
        return (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    };
    
    window_table["getMainViewportPos"] = []() -> sol::table {
        sol::state& lua = *g_window_manager->lua_state_;
        sol::table pos = lua.create_table();
        if (ImGui::GetCurrentContext()) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            pos["x"] = viewport->Pos.x;
            pos["y"] = viewport->Pos.y;
        } else {
            pos["x"] = 0;
            pos["y"] = 0;
        }
        return pos;
    };
    
    window_table["getMainViewportSize"] = []() -> sol::table {
        sol::state& lua = *g_window_manager->lua_state_;
        sol::table size = lua.create_table();
        if (ImGui::GetCurrentContext()) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            size["width"] = viewport->Size.x;
            size["height"] = viewport->Size.y;
        } else {
            size["width"] = 800;
            size["height"] = 600;
        }
        return size;
    };
    
    window_table["destroy"] = [this](const std::string& window_id) -> bool {
        return destroyWindow(window_id);
    };
    
    // Built-in tool windows
    window_table["showHexEditor"] = [this](sol::optional<uint32_t> base_addr) {
        showHexEditor(base_addr.value_or(0));
    };
    
    window_table["showWatchList"] = [this]() {
        showWatchList();
    };
    
    window_table["showMemorySearch"] = [this]() {
        showMemorySearch();
    };
    
    window_table["hideHexEditor"] = [this]() {
        hideHexEditor();
    };
    
    window_table["hideWatchList"] = [this]() {
        hideWatchList();
    };
    
    window_table["hideMemorySearch"] = [this]() {
        hideMemorySearch();
    };
    
    // Window enumeration
    window_table["getWindowIds"] = [this]() {
        return getWindowIds();
    };
    
    window_table["getWindowCount"] = [this]() {
        return getWindowCount();
    };
    
    // Docking management
    window_table["enableDocking"] = [this](bool enabled) {
        enableDocking(enabled);
    };
    
    window_table["showDockspace"] = [this](bool show) {
        showDockspace(show);
    };
    
    // Register in Lua global scope
    (*lua_state_)["window"] = window_table;
    
    std::cout << "[WindowSystem] Lua API registered" << std::endl;
}

void WindowManager::setupBuiltinWindows() {
    // Built-in windows are created on demand
}

// Utility functions
namespace WindowUtils {

bool initializeWindowSystem(sol::state* lua_state) {
    // Window manager is now initialized by LuaEngine
    if (!g_window_manager) {
        return false;  // Not initialized yet
    }

    return g_window_manager->initialize(lua_state);
}

void shutdownWindowSystem() {
    // NOTE: g_window_manager is owned by LuaEngine's unique_ptr and will be
    // automatically destroyed when LuaEngine is shut down. This function should
    // NOT attempt to delete or reset the manager - just clear the global pointer.
    // The actual cleanup happens in the WindowManager destructor when the unique_ptr
    // in LuaEngine is destroyed.
    if (g_window_manager) {
        g_window_manager->shutdown();
        g_window_manager = nullptr;
    }
}

WindowManager* getWindowManager() {
    return g_window_manager;
}

WindowProperties createWindowProperties(sol::table props_table) {
    WindowProperties props;
    
    if (props_table["title"].valid()) {
        props.title = props_table["title"];
    }
    if (props_table["width"].valid()) {
        props.default_width = props_table["width"];
    }
    if (props_table["height"].valid()) {
        props.default_height = props_table["height"];
    }
    if (props_table["can_close"].valid()) {
        props.can_close = props_table["can_close"];
    }
    if (props_table["can_resize"].valid()) {
        props.can_resize = props_table["can_resize"];
    }
    if (props_table["can_move"].valid()) {
        props.can_move = props_table["can_move"];
    }
    
    return props;
}

sol::table windowPropertiesToTable(const WindowProperties& props, sol::state& lua) {
    sol::table table = lua.create_table();
    
    table["title"] = props.title;
    table["width"] = props.default_width;
    table["height"] = props.default_height;
    table["can_close"] = props.can_close;
    table["can_resize"] = props.can_resize;
    table["can_move"] = props.can_move;
    
    return table;
}

void setupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Configure style for better usability
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(4, 3);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    
    // Dark theme colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.08f, 0.50f, 0.72f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
}

void loadImGuiFont(const std::string& font_path, float size) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), size);
}

ImVec4 colorFromHex(const std::string& hex_color) {
    if (hex_color.length() != 7 || hex_color[0] != '#') {
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default to white
    }
    
    unsigned int r, g, b;
    sscanf(hex_color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

std::string colorToHex(const ImVec4& color) {
    std::ostringstream oss;
    oss << "#" << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(color.x * 255)
        << std::setw(2) << static_cast<int>(color.y * 255)
        << std::setw(2) << static_cast<int>(color.z * 255);
    return oss.str();
}

} // namespace WindowUtils

} // namespace LuaEngineGUIWindows
