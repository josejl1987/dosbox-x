#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <imgui/imgui.h>

namespace LuaEngineDebugConfig {

// Configuration categories
enum class ConfigCategory {
    GENERAL,
    MEMORY_SEARCH,
    WATCH_LIST,
    HEX_EDITOR,
    CHEAT_ENGINE,
    TRACE_LOGGER,
    DISASSEMBLY,
    HOTKEYS
};

// Configuration value types
enum class ConfigValueType {
    BOOL,
    INT,
    FLOAT,
    STRING,
    COLOR,
    HOTKEY
};

// Configuration entry
struct ConfigEntry {
    std::string key;
    std::string name;
    std::string description;
    ConfigValueType type;
    ConfigCategory category;
    
    // Value storage
    union {
        bool bool_value;
        int int_value;
        float float_value;
    };
    std::string string_value;
    ImVec4 color_value;
    
    // Constraints
    int min_int = 0;
    int max_int = 100;
    float min_float = 0.0f;
    float max_float = 1.0f;
    
    // Default values
    bool default_bool = false;
    int default_int = 0;
    float default_float = 0.0f;
    std::string default_string = "";
    ImVec4 default_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Callbacks
    std::function<void(const ConfigEntry&)> on_changed;
    
    ConfigEntry(const std::string& k, const std::string& n, const std::string& desc, 
                ConfigValueType t, ConfigCategory cat) 
        : key(k), name(n), description(desc), type(t), category(cat) {
        bool_value = default_bool;
        int_value = default_int;
        float_value = default_float;
        string_value = default_string;
        color_value = default_color;
    }
};

// Hotkey configuration
struct HotkeyConfig {
    std::string name;
    std::string description;
    int key_code = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    std::function<void()> callback;
    
    // Default constructor
    HotkeyConfig() = default;
    
    HotkeyConfig(const std::string& n, const std::string& desc, 
                 int key, bool c = false, bool a = false, bool s = false)
        : name(n), description(desc), key_code(key), ctrl(c), alt(a), shift(s) {}
    
    std::string toString() const {
        std::string result;
        if (ctrl) result += "Ctrl+";
        if (alt) result += "Alt+";
        if (shift) result += "Shift+";
        result += std::to_string(key_code);
        return result;
    }
};

// Main configuration manager
class DebugConfigManager {
private:
    std::map<std::string, std::unique_ptr<ConfigEntry>> config_entries_;
    std::map<std::string, HotkeyConfig> hotkeys_;
    std::string config_file_path_;
    bool show_config_window_;
    bool config_changed_;
    
    // UI state
    int selected_category_;
    char search_filter_[256];
    
    // UI rendering
    void renderConfigWindow();
    void renderCategorySelector();
    void renderConfigEntries();
    void renderHotkeyEditor();
    void renderConfigEntry(ConfigEntry& entry);
    
    // File operations
    void loadConfigFromFile();
    void saveConfigToFile();
    std::string getConfigFilePath() const;
    
    // Default configuration
    void setupDefaultConfig();
    
public:
    DebugConfigManager();
    ~DebugConfigManager();
    
    // Initialization
    void initialize(const std::string& config_file = "");
    void shutdown();
    
    // Configuration management
    void addConfigEntry(const std::string& key, const std::string& name, 
                       const std::string& description, ConfigValueType type, 
                       ConfigCategory category);
    
    // Value accessors
    bool getBool(const std::string& key) const;
    int getInt(const std::string& key) const;
    float getFloat(const std::string& key) const;
    std::string getString(const std::string& key) const;
    ImVec4 getColor(const std::string& key) const;
    
    // Value setters
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int value);
    void setFloat(const std::string& key, float value);
    void setString(const std::string& key, const std::string& value);
    void setColor(const std::string& key, const ImVec4& value);
    
    // Hotkey management
    void addHotkey(const std::string& name, const std::string& description, 
                   int key_code, bool ctrl = false, bool alt = false, bool shift = false);
    void setHotkeyCallback(const std::string& name, std::function<void()> callback);
    void processHotkeys();
    
    // UI
    void render();
    void showConfigWindow() { show_config_window_ = true; }
    void hideConfigWindow() { show_config_window_ = false; }
    bool isConfigWindowVisible() const { return show_config_window_; }
    
    // Callbacks
    void setConfigChangedCallback(const std::string& key, std::function<void(const ConfigEntry&)> callback);
    
    // Utility
    void resetToDefaults();
    void exportConfig(const std::string& filename);
    bool importConfig(const std::string& filename);
    
    // Accessors
    bool isConfigChanged() const { return config_changed_; }
    void markConfigSaved() { config_changed_ = false; }
    
    // Configuration categories
    std::vector<std::string> getCategoryNames() const;
    std::vector<ConfigEntry*> getEntriesForCategory(ConfigCategory category) const;
};

// Global configuration instance
extern DebugConfigManager* g_debug_config;

// Helper macros for common configurations
#define DEBUG_CONFIG_BOOL(key, name, desc, category, default_val) \
    g_debug_config->addConfigEntry(key, name, desc, ConfigValueType::BOOL, category); \
    g_debug_config->setBool(key, default_val);

#define DEBUG_CONFIG_INT(key, name, desc, category, default_val, min_val, max_val) \
    g_debug_config->addConfigEntry(key, name, desc, ConfigValueType::INT, category); \
    g_debug_config->setInt(key, default_val);

#define DEBUG_CONFIG_FLOAT(key, name, desc, category, default_val, min_val, max_val) \
    g_debug_config->addConfigEntry(key, name, desc, ConfigValueType::FLOAT, category); \
    g_debug_config->setFloat(key, default_val);

#define DEBUG_CONFIG_STRING(key, name, desc, category, default_val) \
    g_debug_config->addConfigEntry(key, name, desc, ConfigValueType::STRING, category); \
    g_debug_config->setString(key, default_val);

#define DEBUG_CONFIG_COLOR(key, name, desc, category, default_val) \
    g_debug_config->addConfigEntry(key, name, desc, ConfigValueType::COLOR, category); \
    g_debug_config->setColor(key, default_val);

} // namespace LuaEngineDebugConfig

#endif // DEBUG_CONFIG_H