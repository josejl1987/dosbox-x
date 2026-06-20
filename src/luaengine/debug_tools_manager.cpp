#include "debug_tools_manager.h"
#include "gui_windows.h"
#include "debug_config.h"
#include "core_debug_interface.h"
#include <iostream>
#include <fstream>

// Forward declaration for GUI compatibility
class DebugToolsManager;

namespace LuaEngineDebugTools {

// Track whether we've shown the default UI (e.g., Disassembly) once the window manager is ready
static bool s_initial_ui_setup_complete = false;

DebugToolsManager::DebugToolsManager() 
    : initialized_(false), tools_visible_(false) {
    s_initial_ui_setup_complete = false;
}

DebugToolsManager::~DebugToolsManager() {
    shutdown();
}

bool DebugToolsManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    try {
        // Initialize core debug interface
        debug_interface_ = std::make_unique<LuaEngineDebug::DosBoxCoreDebugger>();
        debug_interface_->initialize(nullptr);
        
        // Set the global debugger instance
        LuaEngineDebug::g_core_debugger = debug_interface_.get();
        
        // Initialize memory domain manager
        memory_manager_ = std::make_unique<LuaEngineMemoryDomains::MemoryDomainManager>();
        memory_manager_->initializeDomains();
        
        // Initialize RAM search components
        ram_search_engine_ = std::make_unique<LuaEngineRamSearch::RamSearchEngine>();
        ram_search_engine_->initialize(memory_manager_.get());
        
        // Initialize watch components
        watch_list_ = std::make_unique<LuaEngineWatchList::WatchList>();
        watch_list_->initialize(memory_manager_.get());
        
        // Initialize trace components (reuse WindowManager's instance to keep UI and backend in sync)
        if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
            trace_logger_ = window_manager->getTraceLogger();
        } else {
            static std::unique_ptr<LuaEngineTraceLogger::TraceLogger> fallback_trace_logger;
            if (!fallback_trace_logger) {
                fallback_trace_logger = std::make_unique<LuaEngineTraceLogger::TraceLogger>();
                fallback_trace_logger->initialize(debug_interface_.get());
            }
            trace_logger_ = fallback_trace_logger.get();
        }
        
        // Initialize additional debug tools
        config_manager_ = std::make_unique<LuaEngineDebugConfig::DebugConfigManager>();
        config_manager_->initialize();
        // Wire global hotkeys to core debugger controls
        config_manager_->setHotkeyCallback("run_pause", [this]() {
            if (!debug_interface_) return;
            if (debug_interface_->isPaused()) {
                debug_interface_->resume();
            } else {
                debug_interface_->pause();
            }
        });
        config_manager_->setHotkeyCallback("step_into", [this]() {
            if (debug_interface_) debug_interface_->stepInto();
        });
        config_manager_->setHotkeyCallback("step_over", [this]() {
            if (debug_interface_) debug_interface_->stepOver();
        });
        config_manager_->setHotkeyCallback("step_out", [this]() {
            if (debug_interface_) debug_interface_->stepOut();
        });
        
        debug_logger_ = std::make_unique<LuaEngineDebugLogger::DebugLogger>();
        
        cheat_engine_ = std::make_unique<LuaEngineCheatEngine::CheatEngine>();
        cheat_engine_->initialize(memory_manager_.get());
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize DebugToolsManager: " << e.what() << std::endl;
        return false;
    }
}

void DebugToolsManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    cheat_engine_.reset();
    debug_logger_.reset();
    config_manager_.reset();
    trace_logger_ = nullptr;
    watch_list_.reset();
    ram_search_engine_.reset();
    memory_manager_.reset();
    debug_interface_.reset();
    
    initialized_ = false;
}

void DebugToolsManager::render() {
    if (!initialized_) {
        return;
    }

    // Defer initial UI show until the window manager exists (first render frame)
    if (!s_initial_ui_setup_complete) {
        if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
            window_manager->showDisassembly();
            tools_visible_ = true;
            s_initial_ui_setup_complete = true;
        }
    }

    // Rendering is handled by LuaEngineGUIWindows::WindowManager.
}

void DebugToolsManager::update() {
    if (!initialized_) {
        return;
    }
    
    // Update watch list values
    if (watch_list_) {
        watch_list_->updateAllValues();
        watch_list_->applyAllFrozenValues();
    }

    // Process global hotkeys even when config window is closed
    if (config_manager_) {
        config_manager_->processHotkeys();
    }
}

void DebugToolsManager::showAllTools() {
    tools_visible_ = true;
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showMemorySearch();
        window_manager->showWatchList();
        window_manager->showHexEditor();
        window_manager->showTraceLogger();
        window_manager->showCheatEngine();
        window_manager->showDisassembly();
    }
}

void DebugToolsManager::hideAllTools() {
    tools_visible_ = false;
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->hideMemorySearch();
        window_manager->hideWatchList();
        window_manager->hideHexEditor();
        window_manager->hideTraceLogger();
        window_manager->hideCheatEngine();
        window_manager->hideDisassembly();
    }
}

void DebugToolsManager::toggleToolsVisibility() {
    if (tools_visible_) {
        hideAllTools();
    } else {
        showAllTools();
    }
}

void DebugToolsManager::showRamSearch() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showMemorySearch();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showWatchList() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showWatchList();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showHexEditor() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showHexEditor();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showTraceLogger() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showTraceLogger();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showLogViewer() {
    if (debug_logger_) {
        // Debug logger automatically handles log viewing
        tools_visible_ = true;
    }
}

void DebugToolsManager::showConfiguration() {
    if (config_manager_) {
        config_manager_->showConfigWindow();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showCheatEngine() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showCheatEngine();
        tools_visible_ = true;
    }
}

void DebugToolsManager::showDisassemblyWindow() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        window_manager->showDisassembly();
        tools_visible_ = true;
    }
}

void DebugToolsManager::onInstructionExecuted(uint32_t address) {
    if (debug_interface_) {
        debug_interface_->onInstructionExecuted(address);
    }
    if (trace_logger_) {
        // Add trace entry if tracing is enabled
    }
}

void DebugToolsManager::onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value) {
    if (watch_list_) {
        watch_list_->onMemoryChanged(address, old_value, new_value);
    }
}

void DebugToolsManager::onBreakpointHit(uint32_t address) {
    // Handle breakpoint hit
}

void DebugToolsManager::freezeAddress(uint32_t address, uint64_t value, const std::string& domain) {
    if (memory_manager_) {
        memory_manager_->writeDWord(domain, address, static_cast<uint32_t>(value));
    }
}

void DebugToolsManager::addWatch(uint32_t address, const std::string& domain) {
    if (watch_list_) {
        watch_list_->addWatch(address, domain, LuaEngineRamSearch::WatchSize::BYTE_1);
    }
}

void DebugToolsManager::searchMemory(const std::string& value, LuaEngineRamSearch::SearchOperator op) {
    if (ram_search_engine_) {
        ram_search_engine_->setSearchValue(value);
        ram_search_engine_->performSearch(op);
    }
}

void DebugToolsManager::goToAddress(uint32_t address) {
    if (debug_interface_) {
        debug_interface_->goToAddress(address);
    }
}

void DebugToolsManager::toggleBreakpoint(uint32_t address) {
    if (debug_interface_) {
        debug_interface_->toggleBreakpoint(address);
    }
}

void DebugToolsManager::startTracing() {
    if (trace_logger_) {
        trace_logger_->setEnabled(true);
    }
}

void DebugToolsManager::stopTracing() {
    if (trace_logger_) {
        trace_logger_->setEnabled(false);
    }
}

bool DebugToolsManager::saveSession(const std::string& filename) {
    if (!watch_list_ || !cheat_engine_) {
        return false;
    }

    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        file << "{\n";
        file << "  \"watches\": [\n";

        // Export watches (simplified - actual implementation would need proper JSON escaping)
        bool first = true;
        for (const auto& watch_ptr : watch_list_->getWatches()) {
            if (!first) file << ",\n";
            file << "    {\"address\": " << watch_ptr->getAddress() << ", \"domain\": \"" << watch_ptr->getDomain() << "\"}";
            first = false;
        }

        file << "\n  ],\n";
        file << "  \"cheats\": []\n"; // Placeholder for cheats
        file << "}\n";

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool DebugToolsManager::loadSession(const std::string& filename) {
    if (!watch_list_) {
        return false;
    }

    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        // Simplified JSON parsing - just look for address patterns
        std::string line;
        while (std::getline(file, line)) {
            // Look for address:domain patterns
            size_t addr_pos = line.find("\"address\":");
            if (addr_pos != std::string::npos) {
                size_t addr_start = line.find(":", addr_pos) + 1;
                size_t addr_end = line.find(",", addr_start);
                if (addr_end == std::string::npos) addr_end = line.find("}", addr_start);

                if (addr_start != std::string::npos && addr_end != std::string::npos) {
                    std::string addr_str = line.substr(addr_start, addr_end - addr_start);
                    uint32_t address = static_cast<uint32_t>(std::stoul(addr_str));

                    // Add watch with default domain
                    watch_list_->addWatch(address, "RAM", LuaEngineRamSearch::WatchSize::BYTE_1);
                }
            }
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void DebugToolsManager::exportWatchList(const std::string& filename) {
    if (watch_list_) {
        watch_list_->exportToFile(filename);
    }
}

void DebugToolsManager::exportCheats(const std::string& filename) {
    if (!cheat_engine_) {
        return;
    }

    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return;
        }

        file << "# DOSBox-X Cheat Export\n";
        file << "# Generated by DebugToolsManager\n\n";

        // Export active cheats (simplified format)
        file << "[cheats]\n";
        file << "# Cheat format: address=value,description\n";
        file << "# No cheats currently exported\n";

        file.close();
    } catch (const std::exception&) {
        // Silent fail for export functionality
    }
}

void DebugToolsManager::exportTraceLog(const std::string& filename) {
    if (trace_logger_) {
        trace_logger_->exportToFile(filename);
    }
}

// GUI integration functions (for compatibility with imgui_window.cpp)
void DebugToolsManager::toggleRamSearch() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        if (window_manager->isMemorySearchVisible()) {
            window_manager->hideMemorySearch();
        } else {
            window_manager->showMemorySearch();
            tools_visible_ = true;
        }
    }
}

void DebugToolsManager::toggleWatchList() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        if (window_manager->isWatchListVisible()) {
            window_manager->hideWatchList();
        } else {
            window_manager->showWatchList();
            tools_visible_ = true;
        }
    }
}

void DebugToolsManager::toggleHexEditor() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        if (window_manager->isHexEditorVisible()) {
            window_manager->hideHexEditor();
        } else {
            window_manager->showHexEditor();
            tools_visible_ = true;
        }
    }
}

void DebugToolsManager::toggleTraceLogger() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        if (window_manager->isTraceLoggerVisible()) {
            window_manager->hideTraceLogger();
        } else {
            window_manager->showTraceLogger();
            tools_visible_ = true;
        }
    }
}

void DebugToolsManager::toggleDisassemblyWindow() {
    if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
        if (window_manager->isDisassemblyVisible()) {
            window_manager->hideDisassembly();
        } else {
            window_manager->showDisassembly();
            tools_visible_ = true;
        }
    }
}

void DebugToolsManager::renderRamSearchWindow() {
    // Rendering handled by WindowManager
}

void DebugToolsManager::renderWatchListWindow() {
    // Rendering handled by WindowManager
}

void DebugToolsManager::renderHexEditorWindow() {
    // Rendering handled by WindowManager
}

void DebugToolsManager::renderTraceLoggerWindow() {
    // Rendering handled by WindowManager
}

void DebugToolsManager::renderCheatEngineWindow() {
    // Deprecated: UI handled by WindowManager
}

void DebugToolsManager::renderDebugToolsMenu() {
    if (ImGui::BeginMenu("Debug Tools")) {
        ImGui::Text("Memory Analysis:");
        ImGui::Separator();
        
        if (ImGui::MenuItem("RAM Search", "Ctrl+Shift+S")) {
            toggleRamSearch();
        }
        
        if (ImGui::MenuItem("Watch List", "Ctrl+Shift+W")) {
            toggleWatchList();
        }
        
        if (ImGui::MenuItem("Hex Editor", "Ctrl+Shift+H")) {
            toggleHexEditor();
        }
        
        ImGui::Spacing();
        ImGui::Text("Execution Analysis:");
        ImGui::Separator();
        
        if (ImGui::MenuItem("Disassembly Debugger", "Ctrl+Shift+D")) {
            toggleDisassemblyWindow();
        }
        
        if (ImGui::MenuItem("Trace Logger", "Ctrl+Shift+T")) {
            toggleTraceLogger();
        }
        
        ImGui::Spacing();
        ImGui::Text("Game Enhancement:");
        ImGui::Separator();
        
        if (ImGui::MenuItem("Cheat Engine")) {
            showCheatEngine();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::MenuItem("Show All Tools")) {
            showAllTools();
        }
        
        if (ImGui::MenuItem("Hide All Tools")) {
            hideAllTools();
        }
        
        ImGui::EndMenu();
    }
}

void DebugToolsManager::renderDebugToolsHelp() {
    // Placeholder for debug tools help rendering
    // This would show help information
}

} // namespace LuaEngineDebugTools

// Global functions for GUI integration (outside namespace)
static LuaEngineDebugTools::DebugToolsManager* g_debug_tools_manager_instance = nullptr;

bool InitializeDebugTools() {
    if (g_debug_tools_manager_instance) {
        return true; // Already initialized
    }
    
    g_debug_tools_manager_instance = new LuaEngineDebugTools::DebugToolsManager();
    return g_debug_tools_manager_instance->initialize();
}

void ShutdownDebugTools() {
    if (g_debug_tools_manager_instance) {
        delete g_debug_tools_manager_instance;
        g_debug_tools_manager_instance = nullptr;
    }
}

LuaEngineDebugTools::DebugToolsManager* GetDebugToolsManager() {
    return g_debug_tools_manager_instance;
}
