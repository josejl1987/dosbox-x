#include "gui_windows.h"
#include "debugger_session.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>
#include <iomanip>
#include <cstring>

#include "disassembly_window.h"
#include "execution_toolbar.h"
#include "core_debug_interface.h"
#include "luaengine.h" // Access LuaEngine for console messages/commands
#include "symbol_manager.h"
#include "trace_logger_window.h"
#include "trace_logger.h"
#include "cheat_window.h"
#include "lua_memory_domains.h"
#include "../gui/imgui_window.h"
#include "debug_bridge.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // DockBuilder API


// DOSBox-X includes
#include "sdlmain.h"
extern SDL_Block sdl;
#include "mem.h"
#include "paging.h"

#ifdef HAVE_SDL
#include <SDL.h>
#endif

// Global Lua engine instance — now provided by debug_bridge.h

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
WatchListWindow::WatchListWindow(const std::string& id, DebuggerSession* session)
    : ToolWindow(id, WindowProperties{}), session_(session), add_address_(0), add_type_index_(0) {
    
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
    // PR2-002: delegate to session's WatchList
    if (session_ && session_->watches()) {
        auto* wl = session_->watches();
        // ponytail: map data_type string to WatchSize, use name as notes
        LuaEngineWatchList::WatchSize size = wl->stringToSize(data_type);
        size_t idx = wl->addWatch(address, "", size);
        auto* w = wl->getWatch(idx);
        if (w) w->setNotes(name);
    }
}

void WatchListWindow::removeWatch(int index) {
    // PR2-002: delegate to session's WatchList
    if (session_ && session_->watches()) {
        session_->watches()->removeWatch(index);
    }
}

void WatchListWindow::clearWatches() {
    // PR2-002: delegate to session's WatchList
    if (session_ && session_->watches()) {
        session_->watches()->clearWatches();
    }
}

void WatchListWindow::updateValues() {
    // PR2-002: update through session's WatchList
    if (session_ && session_->watches()) {
        session_->watches()->updateAllValues();
    }
}

std::string WatchListWindow::formatValue(size_t watch_index) {
    // PR2-002: read from session's WatchList
    if (session_ && session_->watches()) {
        auto* watch = session_->watches()->getWatch(watch_index);
        if (watch) return watch->getCurrentValueString();
    }
    return "N/A";
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
        
        // PR2-002: render from session's WatchList, not local vector
        if (session_ && session_->watches()) {
            auto& watches = session_->watches()->getWatches();
            for (size_t i = 0; i < watches.size(); i++) {
                const auto& watch = watches[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                
                ImGui::TableSetColumnIndex(0);
                // ponytail: Watch has no enabled field; using frozen as closest proxy
                bool enabled = watch->isFrozen();
                if (ImGui::Checkbox(("##enabled" + std::to_string(i)).c_str(), &enabled)) {
                    if (enabled) watch->freeze(); else watch->unfreeze();
                }
                
                ImGui::TableSetColumnIndex(1);
                if (watch->hasValueChanged()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                }
                ImGui::Text("%s", watch->getNotes().c_str());
                if (watch->hasValueChanged()) {
                    ImGui::PopStyleColor();
                }
                
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%08X", watch->getAddress());
                
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", session_->watches()->getSizeString(watch->getSize()).c_str());
                
                ImGui::TableSetColumnIndex(4);
                if (watch->hasValueChanged()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                }
                ImGui::Text("%s", watch->getCurrentValueString().c_str());
                if (watch->hasValueChanged()) {
                    ImGui::PopStyleColor();
                }
                
                if (ImGui::BeginPopupContextItem("GuiWindowsContextMenu")) {
                    if (ImGui::MenuItem("Remove")) {
                        removeWatch(static_cast<int>(i));
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
            }
        }
        
        ImGui::EndTable();
    }
}

// MemorySearchWindow implementation
MemorySearchWindow::MemorySearchWindow(const std::string& id, DebuggerSession* session)
    : ToolWindow(id, WindowProperties{}), session_(session), search_type_index_(0), search_comparison_index_(0),
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
    // PR2-002: delegate to session's RamSearchEngine
    if (session_ && session_->ramSearch()) {
        auto* engine = session_->ramSearch();
        // ponytail: no setSearchRange in API — range stored locally only
        LuaEngineRamSearch::WatchSize size = LuaEngineRamSearch::WatchSize::BYTE_1;
        if (search_type_index_ == 2 || search_type_index_ == 3) size = LuaEngineRamSearch::WatchSize::BYTE_2;
        else if (search_type_index_ == 4 || search_type_index_ == 5) size = LuaEngineRamSearch::WatchSize::BYTE_4;
        engine->setSize(size);
        engine->startNewSearch();
        engine->doSearch(LuaEngineRamSearch::SearchOperator::EQUAL,
                         LuaEngineRamSearch::CompareType::SPECIFIC_VALUE,
                         static_cast<uint64_t>(strtoull(search_value_.c_str(), nullptr, 0)));
    }
}

void MemorySearchWindow::performNextSearch() {
    if (first_search_) {
        performSearch();
        first_search_ = false;
        return;
    }
    // PR2-002: delegate to session's RamSearchEngine
    if (session_ && session_->ramSearch()) {
        session_->ramSearch()->doSearch(LuaEngineRamSearch::SearchOperator::EQUAL,
                                        LuaEngineRamSearch::CompareType::SPECIFIC_VALUE,
                                        static_cast<uint64_t>(strtoull(search_value_.c_str(), nullptr, 0)));
    }
}

void MemorySearchWindow::resetSearch() {
    // PR2-002: delegate to session's RamSearchEngine
    if (session_ && session_->ramSearch()) {
        session_->ramSearch()->clearResults();
    }
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
    
    ImGui::Text("Results: %zu", (session_ && session_->ramSearch()) ? session_->ramSearch()->getResultCount() : 0);
}

void MemorySearchWindow::renderSearchResults() {
    if (ImGui::BeginTable("SearchResults", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Changed", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        
        // PR2-002: render from session's RamSearchEngine results
        if (session_ && session_->ramSearch()) {
            auto& results = session_->ramSearch()->getResults();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(results.size()));
            
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const auto& result = results[i];
                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%08X", result.address);
                    
                    ImGui::TableSetColumnIndex(1);
                    if (result.current_value != result.previous_value) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                    }
                    ImGui::Text("%s", result.getFormattedValue().c_str());
                    if (result.current_value != result.previous_value) {
                        ImGui::PopStyleColor();
                    }
                    
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", result.getFormattedPreviousValue().c_str());
                    
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", result.current_value != result.previous_value ? "Yes" : "No");
                    
                    // Double-click to add to watch list
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        if (session_ && session_->watches()) {
                            std::string name = "Addr_" + std::to_string(result.address);
                            auto* wl = session_->watches();
                            // ponytail: map search type to WatchSize for watch creation
                            LuaEngineWatchList::WatchSize wsize = wl->stringToSize(search_types_[search_type_index_]);
                            size_t idx = wl->addWatch(result.address, "", wsize);
                            auto* w = wl->getWatch(idx);
                            if (w) w->setNotes(name);
                        }
                    }
                }
            }
        }
        
        ImGui::EndTable();
    }
}

// DockingManager implementation
DockingManager::DockingManager() : docking_enabled_(true), dockspace_visible_(true) {
}

void DockingManager::renderDockspace() {
    if (!dockspace_visible_ || !docking_enabled_) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", &dockspace_visible_, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("DebugDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Create default layout on first launch (no .ini file)
    static bool layout_created = false;
    if (!layout_created) {
        createDockLayout();
        layout_created = true;
    }

    ImGui::End();
}

void DockingManager::dockWindow(const std::string& window_id, const std::string& dock_name) {
    auto it = dock_nodes_.find(dock_name);
    if (it != dock_nodes_.end()) {
        ImGui::DockBuilderDockWindow(window_id.c_str(), it->second);
    }
}

void DockingManager::createDockLayout() {
    ImGuiID dockspace_id = ImGui::GetID("DebugDockSpace");
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    // Split: left (Registers) | center-right
    ImGuiID id_left, id_right;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &id_left, &id_right);

    // Split right: center (Disassembly) | right-bottom (WatchList/CallStack)
    ImGuiID id_center, id_bottom_right;
    ImGui::DockBuilderSplitNode(id_right, ImGuiDir_Down, 0.3f, &id_bottom_right, &id_center);

    // Dock windows to nodes
    ImGui::DockBuilderDockWindow("Disassembly", id_center);
    ImGui::DockBuilderDockWindow("Watch List", id_bottom_right);

    dock_nodes_["left"] = id_left;
    dock_nodes_["center"] = id_center;
    dock_nodes_["right_bottom"] = id_bottom_right;

    ImGui::DockBuilderFinish(dockspace_id);
}

void DockingManager::saveDockLayout(const std::string& filename) {
    // ImGui .ini handles dock layout automatically
}

void DockingManager::loadDockLayout(const std::string& filename) {
    // ImGui .ini handles dock layout automatically
}

// WindowManager implementation
WindowManager::WindowManager() : initialized_(false), lua_state_(nullptr), session_(nullptr) {
}

WindowManager::~WindowManager() {
    shutdown();
}

bool WindowManager::initialize(sol::state* lua_state, LuaEngineDebugTools::DebuggerSession* session) {
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

    // Set ImGui .ini path for automatic dock layout persistence
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "debugger_layout.ini";

    lua_state_ = lua_state;
    session_ = session;

    // Setup built-in windows
    setupBuiltinWindows();

    // Register Lua API
    registerLuaAPI();

    // Load custom window states (address-space mode, nav history, etc.)
    loadWindowStates("debugger_state.json");

    initialized_ = true;
    std::cout << "[WindowSystem] Window manager initialized" << std::endl;

    return true;
}

void WindowManager::shutdown() {
    if (!initialized_.load()) return;

    // Save custom window states before destroying windows
    saveWindowStates("debugger_state.json");

    std::lock_guard<std::mutex> lock(windows_mutex_);
    windows_.clear();
    hex_editor_.reset();
    watch_list_.reset();
    memory_search_.reset();
    disassembly_window_.reset();
    execution_toolbar_.reset();
    trace_logger_window_.reset();
    // trace_logger_ is no longer owned here — it's in DebuggerSession
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
        watch_list_ = std::make_unique<WatchListWindow>("watch_list", session_);
    }
    watch_list_->show();
}

void WindowManager::showMemorySearch() {
    if (!memory_search_) {
        memory_search_ = std::make_unique<MemorySearchWindow>("memory_search", session_);
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
    if (session_) {
        return session_->tracer();
    }
    // Fallback: no session available — should not happen in normal flow
    return nullptr;
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

    // Apply saved window states to the newly created disassembly window
    if (!window_states_loaded_) {
        loadWindowStates("debugger_state.json");
        window_states_loaded_ = true;
    }
}

void WindowManager::ensureTraceLoggerWindow() {
    if (trace_logger_window_) return;

    trace_logger_window_ = std::make_unique<LuaEngineTraceLogger::TraceLoggerWindow>();
    trace_logger_window_->initialize(getTraceLogger());
}

void WindowManager::ensureCheatWindow() {
    if (cheat_window_) return;

    cheat_window_ = std::make_unique<LuaEngineCheatEngine::CheatWindow>(session_);
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
    
    // Render execution toolbar inside dockspace (before windows)
    if (execution_toolbar_) {
        execution_toolbar_->render();
    }
    
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

// DEAD CODE: no callers outside definition — remove or wire in a future PR
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
    try {
        std::ofstream file(filename);
        if (!file.is_open()) return;

        file << "{\n";

        // Address-space mode from DisassemblyWindow
        if (disassembly_window_) {
            int mode = static_cast<int>(disassembly_window_->getAddressSpaceMode());
            file << "  \"address_space_mode\": " << mode << ",\n";

            // Nav history back stack
            const auto& back = disassembly_window_->getNavBackStack();
            file << "  \"nav_back_stack\": [";
            for (size_t i = 0; i < back.size(); ++i) {
                if (i > 0) file << ", ";
                file << back[i];
            }
            file << "],\n";

            // Nav history forward stack
            const auto& forward = disassembly_window_->getNavForwardStack();
            file << "  \"nav_forward_stack\": [";
            for (size_t i = 0; i < forward.size(); ++i) {
                if (i > 0) file << ", ";
                file << forward[i];
            }
            file << "]\n";
        } else {
            file << "  \"address_space_mode\": 0\n";
        }

        file << "}\n";
    } catch (...) {
        // Guard file I/O — corrupted state is better than a crash
    }
}

void WindowManager::loadWindowStates(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) return;

        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        // Simple manual JSON parsing — no nlohmann dependency
        // ponytail: manual string parsing, add nlohmann if complexity grows
        auto extract_int = [](const std::string& json, const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return 0;
            pos += search.size();
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            return std::stoi(json.substr(pos));
        };

        auto extract_array = [](const std::string& json, const std::string& key) -> std::vector<uint32_t> {
            std::vector<uint32_t> result;
            std::string search = "\"" + key + "\": [";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return result;
            pos += search.size();
            size_t end = json.find(']', pos);
            if (end == std::string::npos) return result;
            std::string arr = json.substr(pos, end - pos);
            std::istringstream iss(arr);
            std::string token;
            while (std::getline(iss, token, ',')) {
                // Trim whitespace
                size_t start = token.find_first_not_of(" \t\n\r");
                if (start != std::string::npos) {
                    try { result.push_back(static_cast<uint32_t>(std::stoul(token.substr(start)))); }
                    catch (...) {}
                }
            }
            return result;
        };

        // Apply loaded state to DisassemblyWindow
        if (disassembly_window_) {
            int mode = extract_int(content, "address_space_mode");
            disassembly_window_->setAddressSpaceMode(
                static_cast<LuaEngineDebugger::DisassemblyWindow::AddressSpaceMode>(mode));

            auto back_stack = extract_array(content, "nav_back_stack");
            auto forward_stack = extract_array(content, "nav_forward_stack");
            disassembly_window_->setNavStacks(back_stack, forward_stack);
        }

    } catch (...) {
        // Missing or corrupted file → defaults (no crash)
    }
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
    // Create execution toolbar (needs debug interface from session)
    if (session_ && session_->debugger()) {
        execution_toolbar_ = std::make_unique<LuaEngineDebugger::ExecutionToolbar>(
            session_->debugger());
    }
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
