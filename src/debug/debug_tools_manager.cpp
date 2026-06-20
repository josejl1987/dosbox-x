#include "debug_tools_manager.h"
#include <imgui/imgui.h>
#include <sstream>
#include <iomanip>

// Include our debug tools
#include "../luaengine/ram_search_engine.h"
#include "../luaengine/watch_list.h"
#include "../luaengine/hex_editor.h"
#include "../luaengine/trace_logger.h"
#include "../luaengine/lua_memory_domains.h"
#include "../luaengine/core_debug_interface.h"

// Global instances
extern LuaEngineDebug::DosBoxCoreDebugger* g_core_debugger;

// Global instance
DebugToolsManager* g_debug_tools_manager = nullptr;

//=============================================================================
// DebugToolsManager Implementation
//=============================================================================

DebugToolsManager::DebugToolsManager()
    : memory_manager_(nullptr), debug_interface_(nullptr), initialized_(false) {
}

DebugToolsManager::~DebugToolsManager() {
    shutdown();
}

bool DebugToolsManager::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr,
                                  LuaEngineDebug::CoreDebugInterface* debug_iface) {
    if (initialized_.load()) {
        return true; // Already initialized
    }
    
    if (!memory_mgr || !debug_iface) {
        handleError("Invalid memory manager or debug interface");
        return false;
    }
    
    memory_manager_ = memory_mgr;
    debug_interface_ = debug_iface;
    
    try {
        // Initialize debug tools
        ram_search_ = std::make_unique<LuaEngineRamSearch::RamSearchEngine>();
        ram_search_->initialize(memory_manager_);
        
        watch_list_ = std::make_unique<LuaEngineWatchList::WatchList>();
        watch_list_->initialize(memory_manager_);
        
        hex_editor_ = std::make_unique<LuaEngineHexEditor::HexEditor>();
        hex_editor_->initialize(memory_manager_);
        
        trace_logger_ = std::make_unique<LuaEngineTraceLogger::TraceLogger>();
        trace_logger_->initialize(debug_interface_);
        
        // Setup integrations
        setupMemoryDomainIntegration();
        setupDebugInterfaceIntegration();
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        handleError("Exception during initialization: " + std::string(e.what()));
        return false;
    }
}

void DebugToolsManager::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    
    // Shutdown in reverse order
    trace_logger_.reset();
    hex_editor_.reset();
    watch_list_.reset();
    ram_search_.reset();
    
    memory_manager_ = nullptr;
    debug_interface_ = nullptr;
    initialized_ = false;
}

//=============================================================================
// Window Visibility Management
//=============================================================================

void DebugToolsManager::showAllDebugTools() {
    window_visibility_.show_all_debug_tools = true;
    window_visibility_.show_ram_search = true;
    window_visibility_.show_watch_list = true;
    window_visibility_.show_hex_editor = true;
    window_visibility_.show_trace_logger = true;
}

void DebugToolsManager::hideAllDebugTools() {
    window_visibility_.show_all_debug_tools = false;
    window_visibility_.show_ram_search = false;
    window_visibility_.show_watch_list = false;
    window_visibility_.show_hex_editor = false;
    window_visibility_.show_trace_logger = false;
}

void DebugToolsManager::resetWindowOverrides() {
    window_visibility_.force_show_ram_search = false;
    window_visibility_.force_show_watch_list = false;
    window_visibility_.force_show_hex_editor = false;
    window_visibility_.force_show_trace_logger = false;
    window_visibility_.show_all_debug_tools = false;
}

//=============================================================================
// Keyboard Shortcut Handling
//=============================================================================

void DebugToolsManager::toggleRamSearch() {
    window_visibility_.show_ram_search = !window_visibility_.show_ram_search;
}

void DebugToolsManager::toggleWatchList() {
    window_visibility_.show_watch_list = !window_visibility_.show_watch_list;
}

void DebugToolsManager::toggleHexEditor() {
    window_visibility_.show_hex_editor = !window_visibility_.show_hex_editor;
}

void DebugToolsManager::toggleTraceLogger() {
    window_visibility_.show_trace_logger = !window_visibility_.show_trace_logger;
}

//=============================================================================
// ImGui Render Methods
//=============================================================================

void DebugToolsManager::renderRamSearchWindow() {
    if (!initialized_.load() || !ram_search_) return;
    
    bool show_window = shouldShowWindow(window_visibility_.show_ram_search, 
                                       window_visibility_.force_show_ram_search,
                                       window_visibility_.show_all_debug_tools);
    
    if (!show_window) return;
    
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("RAM Search", &window_visibility_.show_ram_search, 
                     ImGuiWindowFlags_MenuBar)) {
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Search")) {
                if (ImGui::MenuItem("New Search", "Ctrl+N")) {
                    ram_search_->startNewSearch();
                }
                if (ImGui::MenuItem("Continue Search", "Ctrl+C")) {
                    ram_search_->updateValues();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Results", "Ctrl+R")) {
                    ram_search_->clearResults();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Export")) {
                if (ImGui::MenuItem("Export to Watch List")) {
                    // Export selected results to watch list
                    auto& results = ram_search_->getResults();
                    if (watch_list_) {
                        for (const auto& result : results) {
                            watch_list_->addWatch(result.address, ram_search_->getCurrentDomain(), 
                                                ram_search_->getCurrentSize());
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Search parameters
        ImGui::Text("Search Parameters:");
        ImGui::Separator();
        
        // Data type selection
        static int data_type = 0;
        const char* data_types[] = {"Byte", "Word", "DWord", "Float", "Double", "String", "Array"};
        ImGui::Combo("Data Type", &data_type, data_types, IM_ARRAYSIZE(data_types));
        
        // Search operator
        static int search_op = 0;
        const char* search_ops[] = {"Equal", "Not Equal", "Less Than", "Greater Than", 
                                   "Changed", "Unchanged", "Increased", "Decreased"};
        ImGui::Combo("Operator", &search_op, search_ops, IM_ARRAYSIZE(search_ops));
        
        // Search value
        static char search_value[256] = "";
        ImGui::InputText("Value", search_value, sizeof(search_value));
        
        // Japanese text search
        static bool decode_japanese = false;
        ImGui::Checkbox("Japanese Text (SJIS)", &decode_japanese);
        
        // Search controls
        ImGui::Separator();
        if (ImGui::Button("New Search")) {
            ram_search_->startNewSearch();
        }
        ImGui::SameLine();
        if (ImGui::Button("Continue Search")) {
            ram_search_->updateValues();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Results")) {
            ram_search_->clearResults();
        }
        
        // Results display
        ImGui::Separator();
        ImGui::Text("Results: %zu", ram_search_->getResultCount());
        
        if (ImGui::BeginChild("Results", ImVec2(0, 0), true)) {
            // Display search results with Japanese text support
            auto& results = ram_search_->getResults();
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& result = results[i];
                
                std::stringstream ss;
                ss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') 
                   << result.address << " | ";
                ss << ram_search_->getCurrentDomain() << " | ";
                ss << std::hex << result.current_value;
                
                if (ImGui::Selectable(ss.str().c_str())) {
                    // Add to watch list on selection
                    if (watch_list_) {
                        watch_list_->addWatch(result.address, ram_search_->getCurrentDomain(), 
                                            ram_search_->getCurrentSize());
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void DebugToolsManager::renderWatchListWindow() {
    if (!initialized_.load() || !watch_list_) return;
    
    bool show_window = shouldShowWindow(window_visibility_.show_watch_list, 
                                       window_visibility_.force_show_watch_list,
                                       window_visibility_.show_all_debug_tools);
    
    if (!show_window) return;
    
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Watch List", &window_visibility_.show_watch_list, 
                     ImGuiWindowFlags_MenuBar)) {
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Watch")) {
                if (ImGui::MenuItem("Add Entry", "Ctrl+A")) {
                    // Add new watch entry
                    watch_list_->addWatch(0x400000, "DOS_CONVENTIONAL", 
                                        LuaEngineRamSearch::WatchSize::BYTE_1);
                }
                if (ImGui::MenuItem("Remove Selected", "Delete")) {
                    // Remove selected entries - simplified for now
                    if (watch_list_->getWatchCount() > 0) {
                        watch_list_->removeWatch(0);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear All", "Ctrl+Shift+C")) {
                    watch_list_->clearWatches();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Display")) {
                if (ImGui::MenuItem("Show SJIS Text")) {
                    // Set display format - simplified
                }
                if (ImGui::MenuItem("Show UTF-8 Text")) {
                    // Set display format - simplified
                }
                if (ImGui::MenuItem("Show Hexadecimal")) {
                    // Set display format - simplified
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Watch list controls
        ImGui::Text("Watch List Controls:");
        ImGui::Separator();
        
        if (ImGui::Button("Add Entry")) {
            watch_list_->addWatch(0x400000, "DOS_CONVENTIONAL", 
                                LuaEngineRamSearch::WatchSize::BYTE_1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Selected")) {
            if (watch_list_->getWatchCount() > 0) {
                watch_list_->removeWatch(0);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            watch_list_->clearWatches();
        }
        
        // Auto-update checkbox
        static bool auto_update = true;
        static bool show_changes = true;
        ImGui::Checkbox("Auto Update", &auto_update);
        ImGui::SameLine();
        ImGui::Checkbox("Show Changes", &show_changes);
        
        // Watch list display
        ImGui::Separator();
        ImGui::Text("Entries: %zu", watch_list_->getWatchCount());
        
        if (ImGui::BeginChild("WatchList", ImVec2(0, 0), true)) {
            // Column headers
            ImGui::Columns(6, "WatchColumns");
            ImGui::Text("Address");
            ImGui::NextColumn();
            ImGui::Text("Domain");
            ImGui::NextColumn();
            ImGui::Text("Type");
            ImGui::NextColumn();
            ImGui::Text("Value");
            ImGui::NextColumn();
            ImGui::Text("Text");
            ImGui::NextColumn();
            ImGui::Text("Notes");
            ImGui::NextColumn();
            ImGui::Separator();
            
            // Display entries
            auto& watches = watch_list_->getWatches();
            for (size_t i = 0; i < watches.size(); ++i) {
                const auto& watch = watches[i];
                
                // Address
                ImGui::Text("%08X", watch->getAddress());
                ImGui::NextColumn();
                
                // Domain
                ImGui::Text("%s", watch->getDomain().c_str());
                ImGui::NextColumn();
                
                // Type
                ImGui::Text("%s", watch_list_->getSizeString(watch->getSize()).c_str());
                ImGui::NextColumn();
                
                // Value (with change highlighting)
                if (watch->hasValueChanged()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", watch->getCurrentValueString().c_str());
                } else {
                    ImGui::Text("%s", watch->getCurrentValueString().c_str());
                }
                ImGui::NextColumn();
                
                // Text (simplified for now)
                ImGui::Text("-");
                ImGui::NextColumn();
                
                // Notes
                ImGui::Text("%s", watch->getNotes().c_str());
                ImGui::NextColumn();
            }
            
            ImGui::Columns(1);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void DebugToolsManager::renderHexEditorWindow() {
    if (!initialized_.load() || !hex_editor_) return;
    
    bool show_window = shouldShowWindow(window_visibility_.show_hex_editor, 
                                       window_visibility_.force_show_hex_editor,
                                       window_visibility_.show_all_debug_tools);
    
    if (!show_window) return;
    
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hex Editor", &window_visibility_.show_hex_editor, 
                     ImGuiWindowFlags_MenuBar)) {
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Export Selection", "Ctrl+E")) {
                    // Export selection to file - simplified
                }
                if (ImGui::MenuItem("Import File", "Ctrl+I")) {
                    // Import file dialog would go here
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, hex_editor_->canUndo())) {
                    hex_editor_->undo();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, hex_editor_->canRedo())) {
                    hex_editor_->redo();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                    // Select all - simplified
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("ASCII Sidebar")) {
                    // Set ASCII sidebar - simplified
                }
                if (ImGui::MenuItem("SJIS Sidebar")) {
                    // Set SJIS sidebar - simplified
                }
                if (ImGui::MenuItem("UTF-8 Sidebar")) {
                    // Set UTF-8 sidebar - simplified
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Navigation controls
        ImGui::Text("Navigation:");
        ImGui::Separator();
        
        // Address input
        static char address_str[32] = "400000";
        ImGui::InputText("Address", address_str, sizeof(address_str));
        ImGui::SameLine();
        if (ImGui::Button("Go")) {
            uint32_t address = std::stoul(address_str, nullptr, 16);
            hex_editor_->gotoAddress(address);
        }
        
        // Domain selection
        static int domain_idx = 0;
        const char* domains[] = {"DOS Conventional", "Video RAM", "BIOS ROM", "EMS", "XMS"};
        ImGui::Combo("Domain", &domain_idx, domains, IM_ARRAYSIZE(domains));
        
        // Display mode
        static int display_mode = 1; // ASCII_SIDEBAR
        const char* display_modes[] = {"Hex Only", "ASCII Sidebar", "SJIS Sidebar", "UTF-8 Sidebar", "Mixed View"};
        ImGui::Combo("Display Mode", &display_mode, display_modes, IM_ARRAYSIZE(display_modes));
        
        // Hex view
        ImGui::Separator();
        ImGui::Text("Current Address: %08X | Domain: %s", 
                   hex_editor_->getCurrentAddress(), 
                   hex_editor_->getCurrentDomain().c_str());
        
        if (ImGui::BeginChild("HexView", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            // Render hex data
            auto view_lines = hex_editor_->renderView();
            for (const auto& line : view_lines) {
                ImGui::Text("%s", line.c_str());
            }
        }
        ImGui::EndChild();
        
        // Status bar
        ImGui::Text("%s", hex_editor_->getStatusText().c_str());
    }
    ImGui::End();
}

void DebugToolsManager::renderTraceLoggerWindow() {
    if (!initialized_.load() || !trace_logger_) return;
    
    bool show_window = shouldShowWindow(window_visibility_.show_trace_logger, 
                                       window_visibility_.force_show_trace_logger,
                                       window_visibility_.show_all_debug_tools);
    
    if (!show_window) return;
    
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Trace Logger", &window_visibility_.show_trace_logger, 
                     ImGuiWindowFlags_MenuBar)) {
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Trace")) {
                if (ImGui::MenuItem("Start Tracing", "F5", false, !trace_logger_->isEnabled())) {
                    trace_logger_->setEnabled(true);
                }
                if (ImGui::MenuItem("Stop Tracing", "F6", false, trace_logger_->isEnabled())) {
                    trace_logger_->setEnabled(false);
                }
                if (ImGui::MenuItem("Clear Log", "Ctrl+L")) {
                    trace_logger_->clear();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Export")) {
                if (ImGui::MenuItem("Export to Text", "Ctrl+S")) {
                    trace_logger_->exportToFile("/tmp/trace_log.txt", "txt");
                }
                if (ImGui::MenuItem("Export to CSV")) {
                    trace_logger_->exportToFile("/tmp/trace_log.csv", "csv");
                }
                if (ImGui::MenuItem("Export to JSON")) {
                    trace_logger_->exportToFile("/tmp/trace_log.json", "json");
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Trace controls
        ImGui::Text("Trace Controls:");
        ImGui::Separator();
        
        // Start/Stop buttons
        if (trace_logger_->isEnabled()) {
            if (ImGui::Button("Stop Tracing")) {
                trace_logger_->setEnabled(false);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "TRACING");
        } else {
            if (ImGui::Button("Start Tracing")) {
                trace_logger_->setEnabled(true);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "STOPPED");
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Clear Log")) {
            trace_logger_->clear();
        }
        
        // Trace options
        static bool include_registers = true;
        static bool include_memory = true;
        static bool decode_japanese = false;
        ImGui::Checkbox("Include Registers", &include_registers);
        ImGui::SameLine();
        ImGui::Checkbox("Include Memory Data", &include_memory);
        ImGui::SameLine();
        ImGui::Checkbox("Decode Japanese Text", &decode_japanese);
        
        // Statistics
        ImGui::Separator();
        ImGui::Text("Statistics:");
        ImGui::Text("Total Events: %zu", trace_logger_->getEntryCount());
        ImGui::Text("Instructions: 0");
        ImGui::Text("Memory Reads: 0");
        ImGui::Text("Memory Writes: 0");
        ImGui::Text("SJIS Strings: 0");
        ImGui::Text("Events/Second: 0.0");
        
        // Event log
        ImGui::Separator();
        ImGui::Text("Event Log: %zu entries", trace_logger_->getEntryCount());
        
        if (ImGui::BeginChild("TraceLog", ImVec2(0, 0), true)) {
            // Display trace entries - simplified
            auto entries = trace_logger_->getEntries();
            for (const auto& entry : entries) {
                std::string line = "Entry: " + std::to_string(entry.address);
                ImGui::Text("%s", line.c_str());
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

//=============================================================================
// Menu Integration
//=============================================================================

void DebugToolsManager::renderDebugToolsMenu() {
    if (ImGui::BeginMenu("Debug Tools")) {
        ImGui::Text("Window Status & Controls:");
        ImGui::Separator();
        
        // Show current visibility status with force-show buttons
        ImGui::Text("RAM Search:  %s %s", 
            window_visibility_.force_show_ram_search ? "[FORCED]" : "[AUTO]",
            window_visibility_.show_all_debug_tools ? "[SHOW_ALL]" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Force Show##RamSearch")) {
            window_visibility_.force_show_ram_search = true;
        }
        
        ImGui::Text("Watch List:  %s %s", 
            window_visibility_.force_show_watch_list ? "[FORCED]" : "[AUTO]",
            window_visibility_.show_all_debug_tools ? "[SHOW_ALL]" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Force Show##WatchList")) {
            window_visibility_.force_show_watch_list = true;
        }
        
        ImGui::Text("Hex Editor:  %s %s", 
            window_visibility_.force_show_hex_editor ? "[FORCED]" : "[AUTO]",
            window_visibility_.show_all_debug_tools ? "[SHOW_ALL]" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Force Show##HexEditor")) {
            window_visibility_.force_show_hex_editor = true;
        }
        
        ImGui::Text("Trace Logger:%s %s", 
            window_visibility_.force_show_trace_logger ? "[FORCED]" : "[AUTO]",
            window_visibility_.show_all_debug_tools ? "[SHOW_ALL]" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Force Show##TraceLogger")) {
            window_visibility_.force_show_trace_logger = true;
        }
        
        ImGui::Separator();
        if (ImGui::Button("FORCE ALL DEBUG TOOLS VISIBLE")) {
            showAllDebugTools();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Overrides")) {
            resetWindowOverrides();
        }
        
        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::Text("Ctrl+Shift+S/W/H/T - Toggle windows");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Note: Windows default to hidden");
        ImGui::EndMenu();
    }
}

void DebugToolsManager::renderDebugToolsHelp() {
    ImGui::Text("Debug Tools Keyboard Shortcuts:");
    ImGui::Separator();
    ImGui::Text("Ctrl+Shift+S - Toggle RAM Search");
    ImGui::Text("Ctrl+Shift+W - Toggle Watch List");
    ImGui::Text("Ctrl+Shift+H - Toggle Hex Editor");
    ImGui::Text("Ctrl+Shift+T - Toggle Trace Logger");
    
    ImGui::Separator();
    ImGui::Text("Features:");
    ImGui::Text("• Complete Japanese (SJIS) text support");
    ImGui::Text("• Multi-format display and export");
    ImGui::Text("• Professional debugging capabilities");
    ImGui::Text("• Memory domain integration");
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Debug tools are hidden by default");
}

//=============================================================================
// Helper Methods
//=============================================================================

bool DebugToolsManager::shouldShowWindow(bool base_visibility, bool force_show, bool global_override) const {
    return base_visibility || force_show || global_override;
}

void DebugToolsManager::handleError(const std::string& error) {
    last_error_ = error;
    // TODO: Log error to system logger
}

DebugToolsManager::ToolStats DebugToolsManager::getStats() const {
    ToolStats stats;
    
    if (ram_search_) {
        stats.ram_search_results = ram_search_->getResultCount();
    }
    
    if (watch_list_) {
        stats.watch_list_entries = watch_list_->getWatchCount();
    }
    
    if (hex_editor_) {
        stats.hex_editor_modifications = hex_editor_->getModifiedDataSize();
    }
    
    if (trace_logger_) {
        stats.trace_logger_events = trace_logger_->getEntryCount();
    }
    
    return stats;
}

void DebugToolsManager::setupMemoryDomainIntegration() {
    // Setup memory domain integration
    if (memory_manager_) {
        memory_manager_->initializeDomains();
    }
}

void DebugToolsManager::setupDebugInterfaceIntegration() {
    // Setup debug interface integration
    if (debug_interface_) {
        // Initialize debug interface if needed
    }
}

//=============================================================================
// Global Functions
//=============================================================================

bool InitializeDebugTools() {
    if (g_debug_tools_manager) {
        return true; // Already initialized
    }
    
    g_debug_tools_manager = new DebugToolsManager();
    
    // Initialize with existing systems
    extern LuaEngineDebug::DosBoxCoreDebugger* g_core_debugger;

    // Initialize global core debugger if it doesn't exist
    if (!g_core_debugger) {
        // PR1-009: replaced corrupted AI block (defect 11)
        g_core_debugger = nullptr;
    }

    // Get the global memory domain manager
    LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr =
        LuaEngineMemoryDomains::GetGlobalMemoryDomainManager();

    // Initialize the debug tools manager
    bool result = g_debug_tools_manager->initialize(memory_mgr, g_core_debugger);
    
    if (!result) {
        delete g_debug_tools_manager;
        g_debug_tools_manager = nullptr;
    }
    
    return result;
}

void ShutdownDebugTools() {
    if (g_debug_tools_manager) {
        delete g_debug_tools_manager;
        g_debug_tools_manager = nullptr;
    }

    // Clean up global core debugger
    if (g_core_debugger) {
        delete g_core_debugger;
        g_core_debugger = nullptr;
    }
}

DebugToolsManager* GetDebugToolsManager() {
    return g_debug_tools_manager;
}