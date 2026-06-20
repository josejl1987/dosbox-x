#include "debug_config.h"
#include <imgui/imgui.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace LuaEngineDebugConfig {

// Global instance
DebugConfigManager* g_debug_config = nullptr;

//=============================================================================
// DebugConfigManager Implementation
//=============================================================================

DebugConfigManager::DebugConfigManager() 
    : show_config_window_(false), config_changed_(false), selected_category_(0) {
    search_filter_[0] = '\0';
}

DebugConfigManager::~DebugConfigManager() {
    shutdown();
}

void DebugConfigManager::initialize(const std::string& config_file) {
    config_file_path_ = config_file.empty() ? getConfigFilePath() : config_file;
    g_debug_config = this;

    setupDefaultConfig();
    loadConfigFromFile();
    
    // Set global instance
    g_debug_config = this;
}

void DebugConfigManager::shutdown() {
    if (config_changed_) {
        saveConfigToFile();
    }
    
    config_entries_.clear();
    hotkeys_.clear();
    g_debug_config = nullptr;
}

void DebugConfigManager::setupDefaultConfig() {
    // General settings
    DEBUG_CONFIG_BOOL("general.show_fps", "Show FPS", "Display FPS counter", ConfigCategory::GENERAL, true);
    DEBUG_CONFIG_BOOL("general.auto_save", "Auto Save", "Automatically save configuration", ConfigCategory::GENERAL, true);
    DEBUG_CONFIG_INT("general.auto_save_interval", "Auto Save Interval", "Auto save interval in seconds", ConfigCategory::GENERAL, 300, 60, 3600);
    
    // Memory search settings
    DEBUG_CONFIG_INT("memory_search.max_results", "Max Results", "Maximum search results to display", ConfigCategory::MEMORY_SEARCH, 1000, 100, 10000);
    DEBUG_CONFIG_INT("memory_search.results_per_page", "Results Per Page", "Number of results per page", ConfigCategory::MEMORY_SEARCH, 100, 10, 500);
    DEBUG_CONFIG_BOOL("memory_search.highlight_changes", "Highlight Changes", "Highlight changed values", ConfigCategory::MEMORY_SEARCH, true);
    DEBUG_CONFIG_COLOR("memory_search.change_color", "Change Color", "Color for changed values", ConfigCategory::MEMORY_SEARCH, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
    
    // Watch list settings
    DEBUG_CONFIG_INT("watch_list.max_watches", "Max Watches", "Maximum number of watches", ConfigCategory::WATCH_LIST, 100, 10, 1000);
    DEBUG_CONFIG_BOOL("watch_list.auto_update", "Auto Update", "Automatically update watch values", ConfigCategory::WATCH_LIST, true);
    DEBUG_CONFIG_INT("watch_list.update_interval", "Update Interval", "Update interval in milliseconds", ConfigCategory::WATCH_LIST, 100, 10, 1000);
    DEBUG_CONFIG_COLOR("watch_list.frozen_color", "Frozen Color", "Color for frozen watch values", ConfigCategory::WATCH_LIST, ImVec4(0.5f, 0.5f, 1.0f, 1.0f));
    
    // Hex editor settings
    DEBUG_CONFIG_INT("hex_editor.bytes_per_line", "Bytes Per Line", "Number of bytes per line in hex editor", ConfigCategory::HEX_EDITOR, 16, 8, 32);
    DEBUG_CONFIG_BOOL("hex_editor.show_ascii", "Show ASCII", "Show ASCII representation", ConfigCategory::HEX_EDITOR, true);
    DEBUG_CONFIG_BOOL("hex_editor.show_addresses", "Show Addresses", "Show memory addresses", ConfigCategory::HEX_EDITOR, true);
    DEBUG_CONFIG_BOOL("hex_editor.highlight_selection", "Highlight Selection", "Highlight selected bytes", ConfigCategory::HEX_EDITOR, true);
    DEBUG_CONFIG_COLOR("hex_editor.selection_color", "Selection Color", "Color for selected bytes", ConfigCategory::HEX_EDITOR, ImVec4(0.3f, 0.7f, 1.0f, 0.5f));
    
    // Cheat engine settings
    DEBUG_CONFIG_BOOL("cheat_engine.auto_apply", "Auto Apply", "Automatically apply cheats", ConfigCategory::CHEAT_ENGINE, true);
    DEBUG_CONFIG_INT("cheat_engine.apply_interval", "Apply Interval", "Cheat apply interval in milliseconds", ConfigCategory::CHEAT_ENGINE, 16, 1, 1000);
    DEBUG_CONFIG_BOOL("cheat_engine.show_hit_count", "Show Hit Count", "Show cheat hit count", ConfigCategory::CHEAT_ENGINE, true);
    DEBUG_CONFIG_BOOL("cheat_engine.confirm_remove", "Confirm Remove", "Confirm before removing cheats", ConfigCategory::CHEAT_ENGINE, true);
    
    // Trace logger settings
    DEBUG_CONFIG_INT("trace_logger.max_entries", "Max Entries", "Maximum trace log entries", ConfigCategory::TRACE_LOGGER, 10000, 1000, 100000);
    DEBUG_CONFIG_BOOL("trace_logger.include_registers", "Include Registers", "Include CPU registers in trace", ConfigCategory::TRACE_LOGGER, true);
    DEBUG_CONFIG_BOOL("trace_logger.auto_scroll", "Auto Scroll", "Automatically scroll to latest entries", ConfigCategory::TRACE_LOGGER, true);
    DEBUG_CONFIG_INT("trace_logger.entries_per_page", "Entries Per Page", "Number of entries per page", ConfigCategory::TRACE_LOGGER, 100, 10, 500);
    DEBUG_CONFIG_BOOL("trace_logger.log_memory_access", "Log Memory Access", "Include memory reads/writes in trace logger", ConfigCategory::TRACE_LOGGER, false);
    DEBUG_CONFIG_BOOL("trace_logger.log_on_step", "Log On Step", "Log instructions executed via step into/over", ConfigCategory::TRACE_LOGGER, true);

    // Disassembly settings
    DEBUG_CONFIG_BOOL("disassembly.show_addresses", "Show Addresses", "Show memory addresses", ConfigCategory::DISASSEMBLY, true);
    DEBUG_CONFIG_BOOL("disassembly.show_opcodes", "Show Opcodes", "Show instruction opcodes", ConfigCategory::DISASSEMBLY, false);
    DEBUG_CONFIG_BOOL("disassembly.auto_follow", "Auto Follow", "Automatically follow execution", ConfigCategory::DISASSEMBLY, true);
    DEBUG_CONFIG_INT("disassembly.instructions_per_page", "Instructions Per Page", "Number of instructions per page", ConfigCategory::DISASSEMBLY, 50, 10, 200);
    DEBUG_CONFIG_COLOR("disassembly.breakpoint_color", "Breakpoint Color", "Color for breakpoints", ConfigCategory::DISASSEMBLY, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    DEBUG_CONFIG_COLOR("disassembly.current_instruction_color", "Current Instruction Color", "Color for current instruction", ConfigCategory::DISASSEMBLY, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    
    // Hotkey settings
    addHotkey("toggle_tools", "Toggle Debug Tools", 'D', true, false, false);
    addHotkey("ram_search", "RAM Search", '1', true, false, false);
    addHotkey("watch_list", "Watch List", '2', true, false, false);
    addHotkey("hex_editor", "Hex Editor", '3', true, false, false);
    addHotkey("cheat_engine", "Cheat Engine", '4', true, false, false);
    addHotkey("trace_logger", "Trace Logger", '5', true, false, false);
    addHotkey("disassembly", "Disassembly", '6', true, false, false);
    addHotkey("step_into", "Step Into", ImGuiKey_F11, false, false, false);
    addHotkey("step_over", "Step Over", ImGuiKey_F10, false, false, false);
    addHotkey("step_out", "Step Out", ImGuiKey_F12, false, false, false);
    addHotkey("run_pause", "Run/Pause", ImGuiKey_F5, false, false, false);
}

void DebugConfigManager::addConfigEntry(const std::string& key, const std::string& name, 
                                       const std::string& description, ConfigValueType type, 
                                       ConfigCategory category) {
    auto entry = std::make_unique<ConfigEntry>(key, name, description, type, category);
    config_entries_[key] = std::move(entry);
}

bool DebugConfigManager::getBool(const std::string& key) const {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::BOOL) {
        return it->second->bool_value;
    }
    return false;
}

int DebugConfigManager::getInt(const std::string& key) const {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::INT) {
        return it->second->int_value;
    }
    return 0;
}

float DebugConfigManager::getFloat(const std::string& key) const {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::FLOAT) {
        return it->second->float_value;
    }
    return 0.0f;
}

std::string DebugConfigManager::getString(const std::string& key) const {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::STRING) {
        return it->second->string_value;
    }
    return "";
}

ImVec4 DebugConfigManager::getColor(const std::string& key) const {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::COLOR) {
        return it->second->color_value;
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void DebugConfigManager::setBool(const std::string& key, bool value) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::BOOL) {
        if (it->second->bool_value != value) {
            it->second->bool_value = value;
            config_changed_ = true;
            if (it->second->on_changed) {
                it->second->on_changed(*it->second);
            }
        }
    }
}

void DebugConfigManager::setInt(const std::string& key, int value) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::INT) {
        value = std::max(it->second->min_int, std::min(it->second->max_int, value));
        if (it->second->int_value != value) {
            it->second->int_value = value;
            config_changed_ = true;
            if (it->second->on_changed) {
                it->second->on_changed(*it->second);
            }
        }
    }
}

void DebugConfigManager::setFloat(const std::string& key, float value) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::FLOAT) {
        value = std::max(it->second->min_float, std::min(it->second->max_float, value));
        if (it->second->float_value != value) {
            it->second->float_value = value;
            config_changed_ = true;
            if (it->second->on_changed) {
                it->second->on_changed(*it->second);
            }
        }
    }
}

void DebugConfigManager::setString(const std::string& key, const std::string& value) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::STRING) {
        if (it->second->string_value != value) {
            it->second->string_value = value;
            config_changed_ = true;
            if (it->second->on_changed) {
                it->second->on_changed(*it->second);
            }
        }
    }
}

void DebugConfigManager::setColor(const std::string& key, const ImVec4& value) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end() && it->second->type == ConfigValueType::COLOR) {
        if (memcmp(&it->second->color_value, &value, sizeof(ImVec4)) != 0) {
            it->second->color_value = value;
            config_changed_ = true;
            if (it->second->on_changed) {
                it->second->on_changed(*it->second);
            }
        }
    }
}

void DebugConfigManager::addHotkey(const std::string& name, const std::string& description, 
                                  int key_code, bool ctrl, bool alt, bool shift) {
    hotkeys_[name] = HotkeyConfig(name, description, key_code, ctrl, alt, shift);
}

void DebugConfigManager::setHotkeyCallback(const std::string& name, std::function<void()> callback) {
    auto it = hotkeys_.find(name);
    if (it != hotkeys_.end()) {
        it->second.callback = callback;
    }
}

void DebugConfigManager::setConfigChangedCallback(const std::string& key, std::function<void(const ConfigEntry&)> callback) {
    auto it = config_entries_.find(key);
    if (it != config_entries_.end()) {
        it->second->on_changed = callback;
    }
}

void DebugConfigManager::processHotkeys() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Skip hotkey processing when ImGui wants keyboard input (e.g., text fields)
    if (io.WantCaptureKeyboard) return;
    
    for (auto& [name, hotkey] : hotkeys_) {
        if (hotkey.callback) {
            bool ctrl_pressed = io.KeyCtrl;
            bool alt_pressed = io.KeyAlt;
            bool shift_pressed = io.KeyShift;
            
            // Check if the hotkey combination is pressed
            if (ctrl_pressed == hotkey.ctrl && 
                alt_pressed == hotkey.alt && 
                shift_pressed == hotkey.shift) {
                
                // Check if the specific key is pressed
                if (ImGui::IsKeyPressed((ImGuiKey)hotkey.key_code)) {
                    hotkey.callback();
                }
            }
        }
    }
}

void DebugConfigManager::render() {
    processHotkeys();
    
    if (show_config_window_) {
        renderConfigWindow();
    }
}

void DebugConfigManager::renderConfigWindow() {
    if (ImGui::Begin("Debug Configuration", &show_config_window_, ImGuiWindowFlags_MenuBar)) {
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Configuration")) {
                    saveConfigToFile();
                }
                if (ImGui::MenuItem("Load Configuration")) {
                    loadConfigFromFile();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export Configuration...")) {
                    // TODO: File dialog for export
                    exportConfig("debug_config_export.ini");
                }
                if (ImGui::MenuItem("Import Configuration...")) {
                    // TODO: File dialog for import
                    importConfig("debug_config_export.ini");
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Reset to Defaults")) {
                    resetToDefaults();
                }
                ImGui::EndMenu();
            }
            
            ImGui::EndMenuBar();
        }
        
        // Search filter
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##SearchFilter", search_filter_, sizeof(search_filter_));
        
        ImGui::Separator();
        
        // Two-column layout
        if (ImGui::BeginTable("ConfigTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            
            ImGui::TableNextRow();
            
            // Category selector
            ImGui::TableNextColumn();
            renderCategorySelector();
            
            // Configuration entries
            ImGui::TableNextColumn();
            renderConfigEntries();
            
            ImGui::EndTable();
        }
        
        // Status bar
        ImGui::Separator();
        if (config_changed_) {
            ImGui::Text("Configuration has unsaved changes");
            ImGui::SameLine();
            if (ImGui::Button("Save Now")) {
                saveConfigToFile();
            }
        } else {
            ImGui::Text("Configuration saved");
        }
    }
    ImGui::End();
}

void DebugConfigManager::renderCategorySelector() {
    std::vector<std::string> categories = {
        "General",
        "Memory Search",
        "Watch List",
        "Hex Editor",
        "Cheat Engine",
        "Trace Logger",
        "Disassembly",
        "Hotkeys"
    };
    
    for (size_t i = 0; i < categories.size(); ++i) {
        if (ImGui::Selectable(categories[i].c_str(), selected_category_ == static_cast<int>(i))) {
            selected_category_ = static_cast<int>(i);
        }
    }
}

void DebugConfigManager::renderConfigEntries() {
    ConfigCategory current_category = static_cast<ConfigCategory>(selected_category_);
    
    if (current_category == ConfigCategory::HOTKEYS) {
        renderHotkeyEditor();
        return;
    }
    
    std::string filter = search_filter_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    
    for (auto& [key, entry] : config_entries_) {
        if (entry->category != current_category) continue;
        
        // Apply search filter
        if (!filter.empty()) {
            std::string name_lower = entry->name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower.find(filter) == std::string::npos) {
                continue;
            }
        }
        
        renderConfigEntry(*entry);
    }
}

void DebugConfigManager::renderConfigEntry(ConfigEntry& entry) {
    ImGui::PushID(entry.key.c_str());
    
    ImGui::Text("%s", entry.name.c_str());
    if (!entry.description.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.description.c_str());
        }
    }
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    
    switch (entry.type) {
        case ConfigValueType::BOOL:
            if (ImGui::Checkbox("##Value", &entry.bool_value)) {
                config_changed_ = true;
                if (entry.on_changed) {
                    entry.on_changed(entry);
                }
            }
            break;
            
        case ConfigValueType::INT:
            if (ImGui::SliderInt("##Value", &entry.int_value, entry.min_int, entry.max_int)) {
                config_changed_ = true;
                if (entry.on_changed) {
                    entry.on_changed(entry);
                }
            }
            break;
            
        case ConfigValueType::FLOAT:
            if (ImGui::SliderFloat("##Value", &entry.float_value, entry.min_float, entry.max_float)) {
                config_changed_ = true;
                if (entry.on_changed) {
                    entry.on_changed(entry);
                }
            }
            break;
            
        case ConfigValueType::STRING:
            {
                char buffer[256];
                strncpy(buffer, entry.string_value.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                
                if (ImGui::InputText("##Value", buffer, sizeof(buffer))) {
                    entry.string_value = buffer;
                    config_changed_ = true;
                    if (entry.on_changed) {
                        entry.on_changed(entry);
                    }
                }
            }
            break;
            
        case ConfigValueType::COLOR:
            if (ImGui::ColorEdit4("##Value", &entry.color_value.x)) {
                config_changed_ = true;
                if (entry.on_changed) {
                    entry.on_changed(entry);
                }
            }
            break;
    }
    
    ImGui::PopID();
}

void DebugConfigManager::renderHotkeyEditor() {
    ImGui::Text("Hotkey Configuration");
    ImGui::Separator();
    
    if (ImGui::BeginTable("HotkeyTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hotkey", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        
        for (auto& [name, hotkey] : hotkeys_) {
            ImGui::TableNextRow();
            ImGui::PushID(name.c_str());
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", name.c_str());
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", hotkey.toString().c_str());
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", hotkey.description.c_str());
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

std::string DebugConfigManager::getConfigFilePath() const {
    return "debug_config.ini";
}

void DebugConfigManager::loadConfigFromFile() {
    std::ifstream file(config_file_path_);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        auto it = config_entries_.find(key);
        if (it != config_entries_.end()) {
            switch (it->second->type) {
                case ConfigValueType::BOOL:
                    it->second->bool_value = (value == "true" || value == "1");
                    break;
                case ConfigValueType::INT:
                    it->second->int_value = std::stoi(value);
                    break;
                case ConfigValueType::FLOAT:
                    it->second->float_value = std::stof(value);
                    break;
                case ConfigValueType::STRING:
                    it->second->string_value = value;
                    break;
                case ConfigValueType::COLOR:
                    // Parse color as "r,g,b,a"
                    {
                        std::stringstream ss(value);
                        std::string component;
                        int i = 0;
                        while (std::getline(ss, component, ',') && i < 4) {
                            (&it->second->color_value.x)[i] = std::stof(component);
                            i++;
                        }
                    }
                    break;
            }
        }
    }
    
    config_changed_ = false;
}

void DebugConfigManager::saveConfigToFile() {
    std::ofstream file(config_file_path_);
    if (!file.is_open()) return;
    
    file << "# DOSBox-X Debug Tools Configuration\n";
    file << "# Generated automatically - do not edit manually\n\n";
    
    for (const auto& [key, entry] : config_entries_) {
        file << key << "=";
        
        switch (entry->type) {
            case ConfigValueType::BOOL:
                file << (entry->bool_value ? "true" : "false");
                break;
            case ConfigValueType::INT:
                file << entry->int_value;
                break;
            case ConfigValueType::FLOAT:
                file << entry->float_value;
                break;
            case ConfigValueType::STRING:
                file << entry->string_value;
                break;
            case ConfigValueType::COLOR:
                file << entry->color_value.x << "," << entry->color_value.y << "," 
                     << entry->color_value.z << "," << entry->color_value.w;
                break;
        }
        
        file << "\n";
    }
    
    config_changed_ = false;
}

void DebugConfigManager::resetToDefaults() {
    for (auto& [key, entry] : config_entries_) {
        switch (entry->type) {
            case ConfigValueType::BOOL:
                entry->bool_value = entry->default_bool;
                break;
            case ConfigValueType::INT:
                entry->int_value = entry->default_int;
                break;
            case ConfigValueType::FLOAT:
                entry->float_value = entry->default_float;
                break;
            case ConfigValueType::STRING:
                entry->string_value = entry->default_string;
                break;
            case ConfigValueType::COLOR:
                entry->color_value = entry->default_color;
                break;
        }
    }
    
    config_changed_ = true;
}

void DebugConfigManager::exportConfig(const std::string& filename) {
    std::string old_path = config_file_path_;
    config_file_path_ = filename;
    saveConfigToFile();
    config_file_path_ = old_path;
}

bool DebugConfigManager::importConfig(const std::string& filename) {
    std::string old_path = config_file_path_;
    config_file_path_ = filename;
    loadConfigFromFile();
    config_file_path_ = old_path;
    return true;
}

} // namespace LuaEngineDebugConfig
