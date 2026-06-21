#ifndef DISASSEMBLY_WINDOW_H
#define DISASSEMBLY_WINDOW_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <optional>

#include "config.h"
#include "imgui.h"
#include "core_debug_interface.h"
#include "symbol_manager.h"
#include "memory_reference_analyzer.h"

// Forward declarations for optional parameters
namespace LuaEngineTraceLogger {
class TraceLogger;
class TraceLoggerWindow;
}

namespace LuaEngineDebugger {

struct DisassemblyLine {
    uint32_t address;
    std::string disassembly;
    std::string opcodes;
    std::string mnemonic;   // Instruction mnemonic (e.g., MOV, JMP)
    std::string operands;   // Operands text
    uint32_t instruction_length{0};

    // Cached symbol data to avoid O(log N) lookups during render loop
    std::string cached_symbol_name;
    std::string cached_symbol_source;
    bool has_symbol{false};

    // Cached memory reference data (lazy evaluation)
    LuaEngineDebug::MemoryReference cached_memref;
    bool memref_computed{false};
};

class DisassemblyWindow : public LuaEngineDebug::CoreDebuggerListener {
public:
    // Address-space display mode
    enum class AddressSpaceMode { Logical, Linear, Physical };

    DisassemblyWindow();
    ~DisassemblyWindow();

    // Initialization compatible with existing callers
    void initialize(LuaEngineDebug::CoreDebugInterface* debug_interface,
                    LuaEngineTraceLogger::TraceLogger* trace_logger = nullptr,
                    LuaEngineTraceLogger::TraceLoggerWindow* trace_logger_window = nullptr,
                    LuaEngineSymbols::SymbolManager* symbol_manager = LuaEngineSymbols::g_symbol_manager);

    // Primary render entry point
    void render();

    // Visibility controls retained for compatibility with DebugToolsManager
    void show() { show_window_ = true; }
    void hide() { show_window_ = false; }
    bool isVisible() const { return show_window_; }

    // Basic state setters (kept for integrations)
    void setActiveTab(int tab_index) { active_tab_ = tab_index; }
    int getActiveTab() const { return active_tab_; }
    void setCurrentAddress(uint32_t address) { current_address_ = address; }

    // Address-space mode accessors
    AddressSpaceMode getAddressSpaceMode() const { return address_space_mode_; }
    void setAddressSpaceMode(AddressSpaceMode mode) { address_space_mode_ = mode; }

    // Navigation history accessors (for persistence)
    const std::vector<uint32_t>& getNavBackStack() const { return nav_back_stack_; }
    const std::vector<uint32_t>& getNavForwardStack() const { return nav_forward_stack_; }
    void setNavStacks(const std::vector<uint32_t>& back, const std::vector<uint32_t>& forward) {
        nav_back_stack_ = back;
        nav_forward_stack_ = forward;
    }

private:
    // Core Components
    LuaEngineDebug::CoreDebugInterface* debug_interface_{nullptr};
    LuaEngineSymbols::SymbolManager* symbol_manager_{nullptr};
    LuaEngineDebug::MemoryReferenceAnalyzer memory_analyzer_;
    LuaEngineTraceLogger::TraceLogger* trace_logger_{nullptr};
    LuaEngineTraceLogger::TraceLoggerWindow* trace_logger_window_{nullptr};

    // State
    bool show_window_{false};
    int active_tab_{0};
    uint32_t current_address_{0};
    uint32_t selected_address_{0};
    std::vector<DisassemblyLine> cached_lines_;
    std::set<uint32_t> cached_breakpoints_;
    bool show_arrows_{true};
    bool show_memrefs_{true};  // Toggle for memory reference column
    bool show_callstack_{true};

    // Address-space mode
    AddressSpaceMode address_space_mode_{AddressSpaceMode::Logical};

    // PR4: Bounded cached lines
    static constexpr size_t CACHED_LINES_CAP = 500;
    uint32_t anchor_address_{0};  // First visible address for virtual window

    // Memory view state
    uint32_t memory_view_address_{0};
    bool memory_follow_csip_{true};
    static char memory_address_input_[10];
    int memory_visible_rows_{8};

    // Navigation state
    static char segment_input_[8];
    static char offset_input_[8];
    static char address_input_[10];
    static bool navigation_initialized_;
    static bool follow_execution_;  // Whether to auto-follow EIP
    static bool user_navigated_;    // Whether user manually navigated away from EIP

    // Navigation history (back/forward)
    std::vector<uint32_t> nav_back_stack_;
    std::vector<uint32_t> nav_forward_stack_;
    static constexpr size_t NAV_HISTORY_CAP = 256;
    void pushNavHistory(uint32_t addr);
    void navigateBack();
    void navigateForward();

    // Symbol management popup
    bool show_symbol_dialog_{false};

    // Go-to expression
    char goto_input_[64] = {};
    bool goto_error_{false};
    float goto_error_timer_{0.0f};

    // Run-to-cursor
    uint32_t run_to_cursor_addr_{0};
    bool run_to_cursor_active_{false};

    // Dump memory dialog state
    bool show_dump_dialog_{false};
    char dump_start_input_[32];
    char dump_length_input_[32];
    char dump_filename_input_[256];

    // Visual Settings
    const float ARROW_AREA_WIDTH = 30.0f;

    // Trace range state
    bool trace_range_enabled_{false};
    bool trace_range_active_{false};
    uint32_t trace_start_addr_{0};
    uint32_t trace_end_addr_{0};

    // Rendering Helpers - Unified Layout
    void renderTraceRangeControls();
    void renderNavigationControls();
    void renderCompactRegisters();
    void renderMemoryHexDump();
    void renderDisassemblyView();
    void renderJumpArrows(const std::vector<DisassemblyLine>& lines, ImVec2 first_row_pos, float row_height, int first_visible_row);
    void renderCallStack();

    // Helper for run until return
    std::optional<uint32_t> getCurrentReturnAddress() const;

    // Symbol management (now as popup)
    void renderSymbolDialog();

    // Memory dump functionality
    void renderDumpDialog();
    void performMemoryDump();

    // Legacy tab rendering (kept for reference, will be removed)
    void renderDisassemblyTab();
    void renderRegistersTab();
    void renderSymbolsTab();

    // Logic Helpers
    ImVec4 getMnemonicColor(const std::string& mnemonic);
    bool isJumpInstruction(const std::string& mnemonic);
    uint32_t parseJumpTarget(const std::string& operands, uint32_t current_addr);
    void handleHoverInspection(const DisassemblyLine& line);
    void refreshCachedLines();
    bool toggleSymbolicBreakpoint(uint32_t address);
    uint32_t resolveAddressExpression(const std::string& expr);

    // CoreDebuggerListener hooks
    void onPaused(LuaEngineDebug::BreakReason reason, uint32_t address) override;
    void onResumed() override;
    void onStepComplete(uint32_t address) override;
    void onBreakpointHit(uint32_t address, LuaEngineDebug::BreakReason reason) override;
    void onBreakpointListChanged() override;

    void updateCurrentAddress(uint32_t address);
    void registerListener();
    void unregisterListener();
    bool listener_registered_{false};
    bool code_view_callback_registered_{false};
    std::function<void(uint16_t, uint32_t)> debug_on_code_view_update; // ponytail: callback for code view updates

    void applyTraceRangeFilter();
};

} // namespace LuaEngineDebugger

#endif // DISASSEMBLY_WINDOW_H
