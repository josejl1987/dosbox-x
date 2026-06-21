#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include "sol/sol.hpp"
#include "trace_logger_window.h"
#include "trace_logger.h"
#include "cheat_window.h"

// Forward declaration for debugger UI
namespace LuaEngineDebugger { class DisassemblyWindow; class ExecutionToolbar; }

// Forward declaration for debugger session
namespace LuaEngineDebugTools { class DebuggerSession; }

namespace LuaEngineGUIWindows {

// Bring DebuggerSession into this namespace for pointer usage
using LuaEngineDebugTools::DebuggerSession;

// Forward declarations
class WindowManager;
class ToolWindow;

// Window types
enum class WindowType {
    CUSTOM,         // User-defined Lua window
    HEX_EDITOR,     // Memory hex editor
    WATCH_LIST,     // Variable watch list
    RAM_SEARCH,     // Memory search tool
    MEMORY_VIEWER,  // Memory viewer/browser
    SCRIPT_CONSOLE, // Lua script console
    FRAME_COUNTER,  // Frame counter display
    INPUT_DISPLAY,  // Input display tool
    CHEATS,         // Cheat management
    DISASSEMBLER,   // CPU disassembler
    REGISTER_VIEWER // CPU register viewer
};

// Window state and properties
struct WindowProperties {
    std::string title;
    WindowType type;
    bool visible;
    bool can_close;
    bool can_resize;
    bool can_move;
    bool can_collapse;
    bool can_dock;
    bool always_on_top;
    bool no_title_bar;
    bool no_scrollbar;
    bool no_menu;
    bool no_collapse;
    bool auto_resize;
    int min_width;
    int min_height;
    int max_width;
    int max_height;
    int default_width;
    int default_height;
    int pos_x;
    int pos_y;
    float alpha;
    ImGuiWindowFlags imgui_flags;
    
    WindowProperties() :
        type(WindowType::CUSTOM), visible(true), can_close(true), can_resize(true),
        can_move(true), can_dock(true), always_on_top(false), no_title_bar(false),
        no_scrollbar(false), no_menu(false), no_collapse(false), auto_resize(false),
        min_width(200), min_height(150), max_width(2000), max_height(1500),
        default_width(400), default_height(300), pos_x(-1), pos_y(-1),
        alpha(1.0f), imgui_flags(0) {}
};

// Window event callbacks
using WindowDrawCallback = std::function<void()>;
using WindowCloseCallback = std::function<bool()>; // Return false to prevent closing
using WindowResizeCallback = std::function<void(int width, int height)>;
using WindowMoveCallback = std::function<void(int x, int y)>;
using WindowFocusCallback = std::function<void(bool focused)>;

// Base tool window class
class ToolWindow {
protected:
    std::string id_;
    WindowProperties properties_;
    bool is_open_;
    bool needs_focus_;
    std::atomic<bool> dirty_;
    
    // Event callbacks
    WindowDrawCallback draw_callback_;
    WindowCloseCallback close_callback_;
    WindowResizeCallback resize_callback_;
    WindowMoveCallback move_callback_;
    WindowFocusCallback focus_callback_;
    
    // Lua state for custom windows
    sol::state* lua_state_;
    sol::function lua_draw_func_;
    sol::function lua_update_func_;
    
public:
    ToolWindow(const std::string& id, const WindowProperties& props);
    virtual ~ToolWindow();
    
    // Core window management
    virtual void render();
    virtual void update();
    virtual bool shouldClose();
    virtual void onClose();
    virtual void onFocus(bool focused);
    virtual void onResize(int width, int height);
    virtual void onMove(int x, int y);
    
    // Property management
    const std::string& getId() const { return id_; }
    const WindowProperties& getProperties() const { return properties_; }
    void setProperties(const WindowProperties& props) { properties_ = props; }
    
    bool isVisible() const { return properties_.visible && is_open_; }
    void setVisible(bool visible) { properties_.visible = visible; }
    void show() { setVisible(true); needs_focus_ = true; }
    void hide() { setVisible(false); }
    void close() { is_open_ = false; }
    void focus() { needs_focus_ = true; }
    
    // Event callback registration
    void setDrawCallback(WindowDrawCallback callback) { draw_callback_ = callback; }
    void setCloseCallback(WindowCloseCallback callback) { close_callback_ = callback; }
    void setResizeCallback(WindowResizeCallback callback) { resize_callback_ = callback; }
    void setMoveCallback(WindowMoveCallback callback) { move_callback_ = callback; }
    void setFocusCallback(WindowFocusCallback callback) { focus_callback_ = callback; }
    
    // Lua integration
    void setLuaState(sol::state* state) { lua_state_ = state; }
    void setLuaDrawFunction(const sol::function& func) { lua_draw_func_ = func; }
    void setLuaUpdateFunction(const sol::function& func) { lua_update_func_ = func; }
    
    // Utility methods
    void markDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_.load(); }
    void clearDirty() { dirty_ = false; }

protected:
    // Helper methods for derived classes
    virtual void renderContent() = 0;
    void applyWindowFlags();
    void handleWindowEvents();
};

// Custom Lua-driven window
class CustomLuaWindow : public ToolWindow {
private:
    sol::table lua_window_table_;
    
public:
    CustomLuaWindow(const std::string& id, const WindowProperties& props, sol::table lua_table);
    
    void renderContent() override;
    void update() override;
    
    // Lua API methods
    void setTitle(const std::string& title);
    void setSize(int width, int height);
    void setPosition(int x, int y);
    void addButton(const std::string& label, sol::function callback);
    void addText(const std::string& text);
    void addInputText(const std::string& label, std::string& buffer);
    void addSlider(const std::string& label, float& value, float min_val, float max_val);
    void addCheckbox(const std::string& label, bool& value);
    void addCombo(const std::string& label, int& current_item, const std::vector<std::string>& items);
    void addSeparator();
    void addSpacing();
    bool isItemHovered() const;
    bool isItemActive() const;
    void setTooltip(const std::string& text);
};

// Memory hex editor window
class HexEditorWindow : public ToolWindow {
private:
    uint32_t base_address_;
    uint32_t current_address_;
    int bytes_per_row_;
    bool show_ascii_;
    bool show_addresses_;
    std::vector<uint8_t> memory_cache_;
    size_t cache_size_;
    bool follow_pointer_;
    uint32_t follow_address_;
    
public:
    HexEditorWindow(const std::string& id, uint32_t base_addr = 0);
    
    void renderContent() override;
    void update() override;
    
    // Hex editor specific methods
    void setBaseAddress(uint32_t address);
    void gotoAddress(uint32_t address);
    void setBytesPerRow(int bytes);
    void setShowAscii(bool show) { show_ascii_ = show; }
    void setShowAddresses(bool show) { show_addresses_ = show; }
    void setFollowPointer(uint32_t address);
    void refreshMemory();
    
private:
    void renderAddressColumn(int row);
    void renderHexColumn(int row);
    void renderAsciiColumn(int row);
    uint8_t readMemoryByte(uint32_t address);
    void writeMemoryByte(uint32_t address, uint8_t value);
};

// Variable watch list window
class WatchListWindow : public ToolWindow {
private:
    // PR2-002: No more embedded WatchEntry vector — uses session's WatchList via pointer
    DebuggerSession* session_ = nullptr;
    int add_type_index_;
    std::vector<std::string> data_types_;
    std::string add_name_;
    uint32_t add_address_ = 0;
    
public:
    WatchListWindow(const std::string& id, DebuggerSession* session = nullptr);
    
    void renderContent() override;
    void update() override;
    
    // Watch list specific methods (delegate to session's WatchList)
    void addWatch(const std::string& name, uint32_t address, const std::string& data_type);
    void removeWatch(int index);
    void clearWatches();
    void updateValues();
    
private:
    std::string formatValue(size_t watch_index);
    void renderAddWatchSection();
    void renderWatchTable();
};

// Memory search window  
class MemorySearchWindow : public ToolWindow {
private:
    // PR2-002: No more embedded SearchResult vector — uses session's RamSearchEngine via pointer
    DebuggerSession* session_ = nullptr;
    int search_type_index_;
    int search_comparison_index_;
    std::vector<std::string> search_types_;
    std::vector<std::string> search_comparisons_;
    bool first_search_;
    uint32_t search_start_address_;
    uint32_t search_end_address_;
    std::string search_value_;
    
public:
    MemorySearchWindow(const std::string& id, DebuggerSession* session = nullptr);
    
    void renderContent() override;
    void update() override;
    
    // Memory search specific methods (delegate to session's RamSearchEngine)
    void performSearch();
    void performNextSearch();
    void resetSearch();
    void setSearchRange(uint32_t start, uint32_t end);
    
private:
    void renderSearchControls();
    void renderSearchResults();
};

// Window docking and layout management
class DockingManager {
private:
    bool docking_enabled_;
    bool dockspace_visible_;
    std::map<std::string, ImGuiID> dock_nodes_;
    
public:
    DockingManager();
    
    void enableDocking(bool enabled) { docking_enabled_ = enabled; }
    bool isDockingEnabled() const { return docking_enabled_; }
    
    void showDockspace(bool show) { dockspace_visible_ = show; }
    bool isDockspaceVisible() const { return dockspace_visible_; }
    
    void renderDockspace();
    void dockWindow(const std::string& window_id, const std::string& dock_name);
    void createDockLayout();
    void saveDockLayout(const std::string& filename);
    void loadDockLayout(const std::string& filename);
};

// Main window manager
class WindowManager {
private:
    std::map<std::string, std::unique_ptr<ToolWindow>> windows_;
    mutable std::mutex windows_mutex_;
    std::atomic<bool> initialized_;
    
    DockingManager docking_manager_;
    sol::state* lua_state_;
    
    // Built-in tool windows
    std::unique_ptr<HexEditorWindow> hex_editor_;
    std::unique_ptr<WatchListWindow> watch_list_;
    std::unique_ptr<MemorySearchWindow> memory_search_;
    std::unique_ptr<LuaEngineDebugger::DisassemblyWindow> disassembly_window_;
    std::unique_ptr<LuaEngineDebugger::ExecutionToolbar> execution_toolbar_;
    // TraceLogger is now owned by DebuggerSession — not here
    std::unique_ptr<LuaEngineTraceLogger::TraceLoggerWindow> trace_logger_window_;
    std::unique_ptr<LuaEngineCheatEngine::CheatWindow> cheat_window_;
    std::unique_ptr<ToolWindow> console_window_;

    // Session provides all backend pointers
    LuaEngineDebugTools::DebuggerSession* session_;
    
public:
    WindowManager();
    ~WindowManager();
    
    // Initialization and cleanup
    bool initialize(sol::state* lua_state, LuaEngineDebugTools::DebuggerSession* session = nullptr);
    void shutdown();
    bool isInitialized() const { return initialized_.load(); }
    
    // Window management
    std::string createWindow(const WindowProperties& props);
    std::string createLuaWindow(const std::string& title, sol::table lua_table);
    bool destroyWindow(const std::string& window_id);
    ToolWindow* getWindow(const std::string& window_id);
    
    // Built-in tool windows
    void showHexEditor(uint32_t base_address = 0);
    void showWatchList();
    void showMemorySearch();
    void showDisassembly();
    void showTraceLogger();
    void showCheatEngine();
    void showConsole();
    void hideHexEditor();
    void hideWatchList();
    void hideMemorySearch();
    void hideDisassembly();
    void hideTraceLogger();
    void hideCheatEngine();
    void hideConsole();
    bool isDisassemblyVisible() const;
    bool isHexEditorVisible() const;
    bool isWatchListVisible() const;
    bool isMemorySearchVisible() const;
    bool isTraceLoggerVisible() const;
    bool isCheatEngineVisible() const;
    bool isConsoleVisible() const;
    LuaEngineTraceLogger::TraceLogger* getTraceLogger();
    
    // Rendering and updates
    void renderAllWindows();
    void updateAllWindows();
    
    // Window enumeration
    std::vector<std::string> getWindowIds() const;
    std::vector<ToolWindow*> getVisibleWindows() const;
    int getWindowCount() const;
    
    // Docking management
    DockingManager& getDockingManager() { return docking_manager_; }
    void enableDocking(bool enabled);
    void showDockspace(bool show);
    
    // Window state management
    void saveWindowStates(const std::string& filename);
    void loadWindowStates(const std::string& filename);
    bool window_states_loaded_{false};
    
    // Event handling
    void onSDLEvent(const SDL_Event& event);
    
private:
    std::string generateWindowId();
    void registerLuaAPI();
    void setupBuiltinWindows();
    void ensureDisassemblyWindow();
    void ensureTraceLoggerWindow();
    void ensureCheatWindow();
    void ensureConsoleWindow();
};

// Global window manager instance - managed by LuaEngine
// This pointer is set by LuaEngine to point to its member
extern WindowManager* g_window_manager;

// Utility functions
namespace WindowUtils {
    bool initializeWindowSystem(sol::state* lua_state);
    void shutdownWindowSystem();
    WindowManager* getWindowManager();
    
    // Helper functions for Lua integration
    WindowProperties createWindowProperties(sol::table props_table);
    sol::table windowPropertiesToTable(const WindowProperties& props, sol::state& lua);
    
    // ImGui utilities
    void setupImGuiStyle();
    void loadImGuiFont(const std::string& font_path, float size);
    ImVec4 colorFromHex(const std::string& hex_color);
    std::string colorToHex(const ImVec4& color);
}

} // namespace LuaEngineGUIWindows
