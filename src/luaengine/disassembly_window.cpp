#include "disassembly_window.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <sstream>
#include <fstream>

#include "symbolic_breakpoints.h"
#include "symbol_manager.h"
#include "trace_logger.h"
#include "trace_logger_window.h"
#include "../debug/debug_inc.h"

// Optimized Hex formatting helper
static const char s_hex_lut[] = "0123456789ABCDEF";
static inline void format_hex4(char* buf, uint16_t val) {
    buf[0] = s_hex_lut[(val >> 12) & 0xF];
    buf[1] = s_hex_lut[(val >> 8) & 0xF];
    buf[2] = s_hex_lut[(val >> 4) & 0xF];
    buf[3] = s_hex_lut[val & 0xF];
}

namespace LuaEngineDebugger {

    // Static member definitions
    char DisassemblyWindow::segment_input_[8] = "";
    char DisassemblyWindow::offset_input_[8] = "";
    char DisassemblyWindow::address_input_[10] = "";
    char DisassemblyWindow::memory_address_input_[10] = "";
    bool DisassemblyWindow::navigation_initialized_ = false;
    bool DisassemblyWindow::follow_execution_ = true;
    bool DisassemblyWindow::user_navigated_ = false;

    DisassemblyWindow::DisassemblyWindow() {
        // Initialize dump dialog buffers
        dump_start_input_[0] = '\0';
        dump_length_input_[0] = '\0';
        dump_filename_input_[0] = '\0';
    }
    DisassemblyWindow::~DisassemblyWindow() {
        unregisterListener();
        if(code_view_callback_registered_) {
#if C_DEBUG
            debug_on_code_view_update = nullptr;
#endif
            code_view_callback_registered_ = false;
        }
    }

    void DisassemblyWindow::initialize(LuaEngineDebug::CoreDebugInterface* debug_interface,
        LuaEngineTraceLogger::TraceLogger* trace_logger,
        LuaEngineTraceLogger::TraceLoggerWindow* trace_logger_window,
        LuaEngineSymbols::SymbolManager* symbol_manager) {
        debug_interface_ = debug_interface;
        symbol_manager_ = symbol_manager ? symbol_manager : LuaEngineSymbols::g_symbol_manager;
        trace_logger_ = trace_logger;
        trace_logger_window_ = trace_logger_window;

        if(debug_interface_) {
            uint32_t cs = debug_interface_->getCurrentCS();
            uint32_t ip = debug_interface_->getCurrentEIP();
            current_address_ = (cs << 4) + ip;
            registerListener();
        }

#if C_DEBUG
        debug_on_code_view_update = [this](uint16_t seg, uint32_t off) {
            this->updateCurrentAddress((seg << 4) + off);
            // Disable auto-follow when user manually navigates via command
            DisassemblyWindow::user_navigated_ = true;
            };
        code_view_callback_registered_ = true;
#endif
    }

    bool DisassemblyWindow::toggleSymbolicBreakpoint(uint32_t address) {
        using namespace LuaEngineSymbols;
        SymbolicBreakpointManager* manager = GetSymbolicBreakpointManager();

        // Prefer the locally cached symbol manager, but fall back to the global if it was created later
        SymbolManager* symbols = symbol_manager_ ? symbol_manager_ : LuaEngineSymbols::g_symbol_manager;

        std::string symbol_name;
        int32_t offset = 0;

        // Priority 1: Look for exact symbol match at this address
        if(symbols) {
            if(auto* sym = symbols->findSymbol(address)) {
                symbol_name = sym->name;
                offset = 0;
            }
            else {
                // Priority 2: Try to find nearest base symbol for relative offset
                std::string base_name = symbols->getSymbolName(address, false);
                if(!base_name.empty()) {
                    symbol_name = base_name;
                    uint32_t base = symbols->getSymbolAddress(base_name);
                    if(base != SymbolManager::INVALID_ADDRESS && address >= base) {
                        offset = static_cast<int32_t>(address - base);
                    }
                }
            }
        }

        // Check for existing breakpoint to toggle
        auto& bps = manager->getBreakpoints();
        for(size_t i = 0; i < bps.size(); ++i) {
            const auto& bp = bps[i];
            bool matches_symbolic = !symbol_name.empty() && !bp.is_absolute &&
                bp.symbol_name == symbol_name && bp.offset == offset;
            bool matches_absolute = symbol_name.empty() && bp.is_absolute &&
                bp.address_valid && bp.resolved_linear_address == address;
            bool matches_resolved = !bp.is_absolute && bp.address_valid &&
                bp.resolved_linear_address == address && symbol_name.empty();

            if(matches_symbolic || matches_absolute || matches_resolved) {
                manager->removeBreakpoint(i);
                return true; // SymbolicBreakpointManager handles core debugger sync via resolveAll()
            }
        }

        // Add new breakpoint - SymbolicBreakpointManager is the single source of truth
        if(!symbol_name.empty()) {
            manager->addBreakpoint(symbol_name, offset);
        }
        else {
            manager->addAbsoluteBreakpoint(address);
        }
        return true;
    }

    void DisassemblyWindow::applyTraceRangeFilter() {
        if(!trace_logger_ || !trace_range_enabled_) {
            return;
        }

        if(trace_start_addr_ > trace_end_addr_) {
            return;
        }

        LuaEngineTraceLogger::TraceFilter filter = trace_logger_->getFilter();
        filter.enabled = true;
        filter.start_address = trace_start_addr_;
        filter.end_address = trace_end_addr_;
        filter.autostart_in_range = true;
        filter.autostop_at_end = true;
        trace_logger_->setFilter(filter);

        // Let the auto-start logic enable tracing when execution enters the range
        trace_logger_->setEnabled(false);
        trace_range_active_ = false;

        if(trace_logger_window_) {
            trace_logger_window_->show();
        }
    }

    void DisassemblyWindow::registerListener() {
        if(debug_interface_ && !listener_registered_) {
            debug_interface_->addListener(this);
            listener_registered_ = true;
        }
    }

    void DisassemblyWindow::unregisterListener() {
        if(debug_interface_ && listener_registered_) {
            debug_interface_->removeListener(this);
            listener_registered_ = false;
        }
    }

    void DisassemblyWindow::updateCurrentAddress(uint32_t address) {
        current_address_ = address;
        navigation_initialized_ = true;
        user_navigated_ = false;
        if(debug_interface_ && debug_interface_->isPaused()) {
            refreshCachedLines();
        }
    }

    void DisassemblyWindow::onPaused(LuaEngineDebug::BreakReason, uint32_t address) {
        updateCurrentAddress(address);
    }

    void DisassemblyWindow::onResumed() {
        refreshCachedLines();
    }

    void DisassemblyWindow::onStepComplete(uint32_t address) {
        updateCurrentAddress(address);

        // Log to trace logger if enabled and log_on_step is active
        if(trace_logger_ && trace_logger_->getLogOnStep()) {
            // Get CS:IP from debug interface
            uint16_t cs = debug_interface_ ? debug_interface_->getCurrentCS() : 0;
            uint16_t ip = debug_interface_ ? debug_interface_->getCurrentEIP() : 0;

            // Get disassembly for this address
            std::string disasm;
            if(debug_interface_) {
                auto lines = debug_interface_->disassembleRange(address, 1);
                if(!lines.empty() && !lines[0].full_text.empty()) {
                    disasm = lines[0].full_text;
                }
            }

            // Fallback if disassembly failed
            if(disasm.empty()) {
                disasm = "step";
            }

            trace_logger_->addEntry(address, cs, ip, disasm);
        }
    }

    void DisassemblyWindow::onBreakpointHit(uint32_t address, LuaEngineDebug::BreakReason) {
        updateCurrentAddress(address);
    }

    void DisassemblyWindow::onBreakpointListChanged() {
        if(debug_interface_ && debug_interface_->isPaused()) {
            refreshCachedLines();
        }
    }

    void DisassemblyWindow::refreshCachedLines() {
        if(!debug_interface_) return;

        // Cache breakpoints for quick lookup while rendering
        cached_breakpoints_.clear();
        for(const auto& bp : debug_interface_->getBreakpoints()) {
            cached_breakpoints_.insert(bp.address);
        }
        if(LuaEngineSymbols::GetSymbolicBreakpointManager()) {
            for(const auto& sbp : LuaEngineSymbols::GetSymbolicBreakpointManager()->getBreakpoints()) {
                if(sbp.enabled && sbp.address_valid) {
                    cached_breakpoints_.insert(sbp.resolved_linear_address);
                }
            }
        }

        cached_lines_.clear();

        // Pull a reasonable window around the current instruction
        // PR4: Cap at CACHED_LINES_CAP to prevent unbounded growth
        constexpr int kLineCount = 128;
        const uint32_t start = current_address_;
        anchor_address_ = start;
        auto lines = debug_interface_->disassembleRange(start, kLineCount);
        cached_lines_.reserve(std::min(lines.size(), CACHED_LINES_CAP));

        for(const auto& l : lines) {
            // PR4: Enforce cap even on refresh
            if(cached_lines_.size() >= CACHED_LINES_CAP) break;
            DisassemblyLine dl{};
            dl.address = l.address;
            dl.opcodes = (l.bytes.size() > 64) ? l.bytes.substr(0, 64) : l.bytes;
            dl.mnemonic = l.mnemonic;
            dl.operands = l.operands;
            dl.disassembly = l.full_text;
            dl.instruction_length = l.length;
            cached_lines_.push_back(std::move(dl));

            // Pre-cache symbol information to avoid map lookups during render loop
            DisassemblyLine& last_line = cached_lines_.back();
            if(symbol_manager_) {
                if(auto* sym = symbol_manager_->findSymbol(last_line.address)) {
                    last_line.cached_symbol_name = sym->name;
                    last_line.cached_symbol_source = sym->sourceline;
                    last_line.has_symbol = true;
                }
                else {
                    last_line.cached_symbol_name = symbol_manager_->getSymbolName(last_line.address, false);
                    last_line.has_symbol = !last_line.cached_symbol_name.empty();
                }
            }
        }
    }

    // -----------------------------------------------------------------------------
    // Main Render Loop - Unified Layout
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::render() {
        if(!show_window_) return;

        // Prevent duplicate rendering within the same frame if called from multiple managers
        static int last_render_frame = -1;
        const int current_frame = ImGui::GetFrameCount();
        if(last_render_frame == current_frame) return;
        last_render_frame = current_frame;

        // Ensure a reasonable default size so the window is visible even with a tiny saved layout
        ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_FirstUseEver);
        ImGui::PushID(static_cast<void*>(this));
        bool begin_ok = ImGui::Begin("Disassembly", &show_window_, ImGuiWindowFlags_MenuBar);
        if(begin_ok) {

            // Menu Bar
            if(ImGui::BeginMenuBar()) {
                if(ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Show Jump Arrows", nullptr, &show_arrows_);
                    ImGui::MenuItem("Call Stack", nullptr, &show_callstack_);
                    ImGui::Separator();
                    if(ImGui::MenuItem("Dump Memory...")) {
                        uint32_t default_addr = (selected_address_ != 0) ? selected_address_ : current_address_;
                        std::snprintf(dump_start_input_, sizeof(dump_start_input_), "%08X", default_addr);
                        std::snprintf(dump_length_input_, sizeof(dump_length_input_), "1000"); // Default 4KB
                        std::strncpy(dump_filename_input_, "memdump.bin", sizeof(dump_filename_input_) - 1);
                        show_dump_dialog_ = true;
                    }
                    if(ImGui::MenuItem("Symbol Management...")) {
                        show_symbol_dialog_ = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            renderToolbar();

            // ===== UNIFIED 3-PANEL LAYOUT =====

            // Panel 1: Compact Registers (Top, fixed height)
            renderCompactRegisters();

            // Panel 2: Memory Hex Dump (Middle, fixed height)
            renderMemoryHexDump();

            // Panel 2.5: Call Stack (optional, fixed height)
            if(show_callstack_) {
                renderCallStack();
            }

            // Panel 3: Disassembly View (Bottom, remaining space)
            renderDisassemblyView();
        }
        ImGui::End();
        ImGui::PopID();

        // Render symbol dialog if requested
        if(show_symbol_dialog_) {
            renderSymbolDialog();
        }

        if(show_dump_dialog_) {
            renderDumpDialog();
        }
    }

    void DisassemblyWindow::renderToolbar() {
        if(!debug_interface_) return;

        bool paused = debug_interface_->isPaused();

        ImGui::PushID("Toolbar");
        ImGui::AlignTextToFramePadding();
        ImVec4 status_color = paused ? ImVec4(1.0f, 0.5f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        ImGui::TextColored(status_color, paused ? "[PAUSED]" : "[RUNNING]");
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();

        if(ImGui::Button("Resume (F5)"))   debug_interface_->resume();
        ImGui::SameLine();
        if(ImGui::Button("Step Into (F11)")) debug_interface_->stepInto();
        ImGui::SameLine();
        if(ImGui::Button("Step Over (F10)")) debug_interface_->stepOver();
        ImGui::SameLine();
        if(ImGui::Button("Pause"))         debug_interface_->pause();
        ImGui::SameLine();

        // Run until return button
        auto ret_addr = getCurrentReturnAddress();
        bool can_run_until_ret = ret_addr.has_value();
        if(!can_run_until_ret) ImGui::BeginDisabled();
        if(ImGui::Button("Run Until Return")) {
            if(ret_addr && debug_interface_) {
                debug_interface_->runToCursor(*ret_addr);
            }
        }
        if(ImGui::IsItemHovered() && can_run_until_ret) {
            ImGui::SetTooltip("Run until current function returns (to 0x%08X)", *ret_addr);
        }
        if(!can_run_until_ret) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::Checkbox("MemRef", &show_memrefs_);
        if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show memory reference column");
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();

        // Trace range controls
        if(!trace_logger_) {
            ImGui::TextDisabled("Trace range unavailable");
        }
        else {
            ImGui::Checkbox("Trace Range", &trace_range_enabled_);
            ImGui::SameLine();
            ImGui::Text(" [%08X - %08X]", trace_start_addr_, trace_end_addr_);
            ImGui::SameLine();
            if(ImGui::Button("Trace This Range")) {
                applyTraceRangeFilter();
            }
            ImGui::SameLine();
            if(ImGui::Button("Clear Range")) {
                trace_range_enabled_ = false;
                trace_range_active_ = false;
                trace_start_addr_ = 0;
                trace_end_addr_ = 0;
            }
        }

        ImGui::PopID();
    }

    // -----------------------------------------------------------------------------
    // Disassembly View (Arrows + Syntax Highlighting)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderDisassemblyTab() {
        if(!debug_interface_) {
            ImGui::TextUnformatted("Debug interface not available.");
            return;
        }
        ImGui::PushID("DisasmTab");
        // Compute current linear address (real-mode CS:IP). Protected mode base unknown; assume real-mode layout.
        uint32_t cs = debug_interface_->getCurrentCS();
        uint32_t ip = debug_interface_->getCurrentEIP();
        uint32_t linear_eip = (cs << 4) + ip;
        const bool paused = debug_interface_->isPaused();

        // Only auto-follow EIP if follow_execution_ is enabled and user hasn't manually navigated
        bool should_follow = follow_execution_ && !user_navigated_;

        // Refresh only when paused/stepping or when cache is empty; avoid chasing while running.
        bool needs_refresh = cached_lines_.empty();

        // Auto-follow current EIP when appropriate
        if(should_follow && paused) {
            // Check if EIP is outside current view
            bool eip_visible = false;
            if(!cached_lines_.empty()) {
                uint32_t first = cached_lines_.front().address;
                uint32_t last = cached_lines_.back().address;
                eip_visible = (linear_eip >= first && linear_eip <= last);
            }

            if(!eip_visible || cached_lines_.empty()) {
                current_address_ = linear_eip;
                needs_refresh = true;
            }
        }

        if(needs_refresh) {
            refreshCachedLines();
        }

        // Reset user_navigated_ flag when execution resumes (so we follow again after next pause)
        static bool was_paused = false;
        if(!paused && was_paused) {
            user_navigated_ = false;
        }
        was_paused = paused;

        // Navigation Controls
        renderNavigationControls();

        char child_id[64];
        std::snprintf(child_id, sizeof(child_id), "CodeView##Disasm_%p", this);
        ImGui::BeginChild(child_id, ImVec2(0, 0), true, ImGuiWindowFlags_None);

        // Reserve space for arrow area on the left
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ARROW_AREA_WIDTH);

        bool requested_refresh = false; // Track if user action requires a refresh (e.g. BP toggle)

        char table_id[64];
        std::snprintf(table_id, sizeof(table_id), "DisasmTable##code_%p", this);
        if(ImGui::BeginTable(table_id, 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // Now ImGui has advanced the cursor to the first DATA row
            ImVec2 first_row_pos = ImGui::GetCursorScreenPos();
            float row_height = ImGui::GetTextLineHeightWithSpacing();

            // Draw arrows now, aligned to the actual row origin
       

            const uint32_t seg_base = cs << 4;

            // Use ImGuiListClipper to optimize rendering of large lists
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(cached_lines_.size()));

            while(clipper.Step()) {
                for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const auto& line = cached_lines_[row];

                    // Use cached symbol data
                    const std::string& symbol_name = line.cached_symbol_name;
                    const std::string& symbol_source = line.cached_symbol_source;

                    ImGui::PushID(row);
                    ImGui::TableNextRow();

                    // Address Column
                    ImGui::TableNextColumn();

                    // Highlight Execution Point when paused
                    if(paused && line.address == linear_eip) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 100, 50, 255));
                    }
                    // Highlight Breakpoint
                    bool is_bp = cached_breakpoints_.count(line.address) > 0;
                    if(is_bp) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 40, 40, 200));
                    }
                    // Highlight trace range selection
                    if(trace_range_enabled_ && trace_start_addr_ <= trace_end_addr_ &&
                        line.address >= trace_start_addr_ && line.address <= trace_end_addr_) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(40, 40, 90, 120));
                    }
                    // Highlight trace range selection
                    if(trace_range_enabled_ && trace_start_addr_ <= trace_end_addr_ &&
                        line.address >= trace_start_addr_ && line.address <= trace_end_addr_) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(40, 40, 90, 120));
                    }

                    // Display as CS:Offset
                    char addr_buf[32] = {};
                    format_hex4(addr_buf, cs);
                    addr_buf[4] = ':';
                    format_hex4(addr_buf + 5, static_cast<uint16_t>((line.address - seg_base) & 0xFFFF));
                    addr_buf[9] = '\0';

                    if(ImGui::Selectable(addr_buf, selected_address_ == line.address, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                        selected_address_ = line.address;
                    }

                    // Toggle Breakpoint on Double Click
                    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        bool handled = toggleSymbolicBreakpoint(line.address);
                        if(!handled && debug_interface_) {
                            debug_interface_->toggleBreakpoint(line.address);
                        }
                        requested_refresh = true;
                    }

                    // Context Menu
                    char context_id[64];
                    std::snprintf(context_id, sizeof(context_id), "LineContext##Disasm_%p_%08X", this, line.address);
                    if(ImGui::BeginPopupContextItem(context_id)) {
                        if(ImGui::MenuItem("Run to Cursor (F4)")) debug_interface_->runToCursor(line.address);

                        // Run until return menu item
                        auto ret_addr = getCurrentReturnAddress();
                        bool can_run_until_ret = ret_addr.has_value();
                        if(ImGui::MenuItem("Run Until Return", nullptr, false, can_run_until_ret)) {
                            if(ret_addr && debug_interface_) {
                                debug_interface_->runToCursor(*ret_addr);
                            }
                        }

                        if(ImGui::MenuItem("Toggle Breakpoint")) {
                            bool handled = toggleSymbolicBreakpoint(line.address);
                            if(!handled && debug_interface_) {
                                debug_interface_->toggleBreakpoint(line.address);
                            }
                            requested_refresh = true;
                        }
                        if(ImGui::MenuItem("Set Next Statement")) debug_interface_->setCpuRegister("EIP", line.address);
                        if(trace_logger_) {
                            if(ImGui::MenuItem("Set Trace Start Here")) {
                                trace_start_addr_ = line.address;
                                trace_range_enabled_ = true;
                            }
                            if(ImGui::MenuItem("Set Trace End Here")) {
                                trace_end_addr_ = line.address;
                                trace_range_enabled_ = true;
                            }
                            if(ImGui::MenuItem("Trace This Range", nullptr,
                                false, trace_range_enabled_ && trace_start_addr_ <= trace_end_addr_)) {
                                applyTraceRangeFilter();
                            }
                        }
                        ImGui::EndPopup();
                    }

                    // Bytes Column
                    ImGui::TableNextColumn();
                    std::string bytes_display = line.opcodes;
                    if(bytes_display.size() > 64) {
                        bytes_display.resize(61);
                        bytes_display.append("...");
                    }
                    ImGui::TextUnformatted(bytes_display.c_str());

                    // Label Column (Symbol names)
                    ImGui::TableNextColumn();
                    if(!symbol_source.empty()) {
                        // Source code lines - display in label column with brackets
                        ImGui::TextDisabled("[%s]", symbol_name.c_str());
                    }
                    else if(!symbol_name.empty()) {
                        // Symbolic breakpoints and labels - de-emphasized
                        ImGui::TextDisabled("[%s]", symbol_name.c_str());
                    }
                    else {
                        // No symbol - leave empty
                        ImGui::TextUnformatted("");
                    }

                    // Instruction Column (Mnemonic + Operands only)
                    ImGui::TableNextColumn();
                    ImVec4 color = getMnemonicColor(line.mnemonic);
                    bool use_color = symbol_source.empty(); // Source lines are displayed as-is
                    if(use_color) {
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                    }
                    if(!symbol_source.empty()) {
                        // Source code lines - display source text as instruction
                        ImGui::TextUnformatted(symbol_source.c_str());
                    }
                    else {
                        // Assembly instructions - display mnemonic + operands only
                        ImGui::Text("%s %s",
                            line.mnemonic.empty() ? "?" : line.mnemonic.c_str(),
                            line.operands.c_str());
                    }
                    if(use_color) {
                        ImGui::PopStyleColor();
                    }

                    // Hover Inspection
                    if(ImGui::IsItemHovered()) {
                        handleHoverInspection(line);
                    }

                    ImGui::PopID();
                }
            } // End clipper
            ImGui::EndTable();
        }

        ImGui::EndChild();

        // Only refresh if explicitly requested by user action, not every frame.
        if(requested_refresh) {
            refreshCachedLines();
        }

        ImGui::PopID(); // DisasmTab
    }


    // -----------------------------------------------------------------------------
    // Hover Logic
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::handleHoverInspection(const DisassemblyLine& line) {
        if(!debug_interface_) return;
        if(!ImGui::IsItemHovered()) return;

        std::string tooltip;
        LuaEngineDebug::CPUState state = debug_interface_->getCpuState();

        auto add_reg = [&](const char* name, uint32_t value) {
            char buf[64];
            if(line.operands.find(name) != std::string::npos) {
                std::snprintf(buf, sizeof(buf), "%s = 0x%08X\n", name, value);
                tooltip += buf;
            }
            };

        add_reg("EAX", state.eax);
        add_reg("EBX", state.ebx);
        add_reg("ECX", state.ecx);
        add_reg("EDX", state.edx);
        add_reg("ESI", state.esi);
        add_reg("EDI", state.edi);
        add_reg("EBP", state.ebp);
        add_reg("ESP", state.esp);

        // Check for Memory Brackets [0x...]
        size_t open_b = line.operands.find('[');
        size_t close_b = line.operands.find(']');
        if(open_b != std::string::npos && close_b != std::string::npos && close_b > open_b + 1) {
            std::string inner = line.operands.substr(open_b + 1, close_b - open_b - 1);
            try {
                uint32_t addr = std::stoul(inner, nullptr, 16);
                uint8_t b = debug_interface_->readByte(addr);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "[%08X] = %02X", addr, b);
                tooltip += buf;
            }
            catch(...) {
                // Ignore malformed addresses or invalid memory during hover
            }
        }

        if(!tooltip.empty()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
    }

    // -----------------------------------------------------------------------------
    // Compact Registers View (Unified Layout)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderCompactRegisters() {
        if(!debug_interface_) return;

        LuaEngineDebug::CPUState state = debug_interface_->getCpuState();
        static LuaEngineDebug::CPUState prev_state = state;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
        ImGui::BeginChild("CompactRegs", ImVec2(0, 80), true);

        // Helper to color changed registers
        auto coloredReg = [&](const char* name, uint32_t value, uint32_t prev_value) {
            if(value != prev_value) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s=%08X", name, value);
            }
            else {
                ImGui::Text("%s=%08X", name, value);
            }
            };

        // First row: General purpose registers
        coloredReg("EAX", state.eax, prev_state.eax);
        ImGui::SameLine(120);
        coloredReg("EBX", state.ebx, prev_state.ebx);
        ImGui::SameLine(240);
        coloredReg("ECX", state.ecx, prev_state.ecx);
        ImGui::SameLine(360);
        coloredReg("EDX", state.edx, prev_state.edx);

        // Second row: Index registers
        coloredReg("ESI", state.esi, prev_state.esi);
        ImGui::SameLine(120);
        coloredReg("EDI", state.edi, prev_state.edi);
        ImGui::SameLine(240);
        coloredReg("EBP", state.ebp, prev_state.ebp);
        ImGui::SameLine(360);
        coloredReg("ESP", state.esp, prev_state.esp);

        // Third row: Segment registers and IP
        ImGui::Text("CS=%04X  DS=%04X  ES=%04X  FS=%04X  GS=%04X  SS=%04X  IP=%08X",
            state.cs, state.ds, state.es, state.fs, state.gs, state.ss, state.ip);

        // Fourth row: Flags
        ImGui::Text("Flags: %c%c%c%c%c%c%c%c%c",
            (state.flags & 0x0001) ? 'C' : '-',  // Carry
            (state.flags & 0x0004) ? 'P' : '-',  // Parity
            (state.flags & 0x0010) ? 'A' : '-',  // Auxiliary
            (state.flags & 0x0040) ? 'Z' : '-',  // Zero
            (state.flags & 0x0080) ? 'S' : '-',  // Sign
            (state.flags & 0x0100) ? 'T' : '-',  // Trap
            (state.flags & 0x0200) ? 'I' : '-',  // Interrupt
            (state.flags & 0x0400) ? 'D' : '-',  // Direction
            (state.flags & 0x0800) ? 'O' : '-'); // Overflow

        prev_state = state;

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // -----------------------------------------------------------------------------
    // Memory Hex Dump View (Unified Layout)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderMemoryHexDump() {
        if(!debug_interface_) return;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
        ImGui::BeginChild("MemoryHexDump", ImVec2(0, 200), true);

        // Memory view controls
        ImGui::Checkbox("Follow CS:IP", &memory_follow_csip_);
        ImGui::SameLine();

        // Update memory view address if following CS:IP
        if(memory_follow_csip_) {
            uint32_t cs = debug_interface_->getCurrentCS();
            uint32_t ip = debug_interface_->getCurrentEIP();
            memory_view_address_ = (cs << 4) + ip;
            std::snprintf(memory_address_input_, sizeof(memory_address_input_), "%08X", memory_view_address_);
        }

        // Manual address input
        ImGui::Text("Address:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if(ImGui::InputText("##mem_addr", memory_address_input_, sizeof(memory_address_input_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            try {
                memory_view_address_ = static_cast<uint32_t>(std::stoul(memory_address_input_, nullptr, 16));
                memory_follow_csip_ = false; // Disable follow when user manually enters address
            }
            catch(...) {
                // Invalid input - ignore
            }
        }

        ImGui::Separator();

        // Render hex dump rows
        uint32_t bytes_per_row = 16;
        for(int row = 0; row < memory_visible_rows_; ++row) {
            uint32_t row_address = memory_view_address_ + (row * bytes_per_row);

            // Address column
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%08X:", row_address);
            ImGui::SameLine();

            // Hex bytes
            std::string hex_part;
            std::string ascii_part;
            for(uint32_t i = 0; i < bytes_per_row; ++i) {
                uint32_t byte_addr = row_address + i;
                uint8_t byte_value = debug_interface_->readByte(byte_addr);

                char hex_buf[4];
                std::snprintf(hex_buf, sizeof(hex_buf), "%02X ", byte_value);
                hex_part += hex_buf;

                // ASCII representation
                char ascii_char = (byte_value >= 32 && byte_value < 127) ? static_cast<char>(byte_value) : '.';
                ascii_part += ascii_char;

                // Add space after 8 bytes for readability
                if(i == 7) {
                    hex_part += " ";
                }
            }

            ImGui::TextUnformatted(hex_part.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.8f, 1.0f), "%s", ascii_part.c_str());
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // -----------------------------------------------------------------------------
    // Disassembly View (Unified Layout with Infinite Scrolling)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderDisassemblyView() {
        if(!debug_interface_) {
            ImGui::TextUnformatted("Debug interface not available.");
            return;
        }

        ImGui::PushID("DisasmView");

        // Compute current linear address
        uint32_t cs = debug_interface_->getCurrentCS();
        uint32_t ip = debug_interface_->getCurrentEIP();
        uint32_t linear_eip = (cs << 4) + ip;
        const bool paused = debug_interface_->isPaused();

        // Auto-follow EIP if enabled and user hasn't manually navigated
        bool should_follow = follow_execution_ && !user_navigated_;

        bool needs_refresh = cached_lines_.empty();
        bool reset_scroll = false;

        // Auto-follow current EIP when appropriate
        if(should_follow && paused) {
            // Strictly follow CS:IP - if the address is different, update and refresh.
            // This ensures the current instruction is always at the top/anchor of the view.
            // Added check for cached_lines_.empty() to force initial load.
            if(current_address_ != linear_eip || cached_lines_.empty()) {
                current_address_ = linear_eip;
                needs_refresh = true;
                reset_scroll = true;
            }
        }

        if(needs_refresh) {
            refreshCachedLines();
        }

        // Reset user_navigated_ flag when execution resumes
        static bool was_paused = false;
        if(!paused && was_paused) {
            user_navigated_ = false;
        }
        was_paused = paused;

        // Navigation Controls
        renderNavigationControls();

        // Disassembly view with infinite scrolling support
        char child_id[64];
        std::snprintf(child_id, sizeof(child_id), "CodeView##Disasm_%p", this);
        ImGui::BeginChild(child_id, ImVec2(0, 0), true, ImGuiWindowFlags_None);

        if(reset_scroll) {
            ImGui::SetScrollY(0.0f);
        }

        // Reserve space for arrow area on the left
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ARROW_AREA_WIDTH);

        bool requested_refresh = false;

        char table_id[64];
        std::snprintf(table_id, sizeof(table_id), "DisasmTable##code_%p", this);
        int num_columns = show_memrefs_ ? 5 : 4;
        if(ImGui::BeginTable(table_id, num_columns, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
            if(show_memrefs_) {
                ImGui::TableSetupColumn("MemRef", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            }
            ImGui::TableHeadersRow();

            // Now ImGui has advanced the cursor to the first DATA row
            ImVec2 first_row_pos = ImGui::GetCursorScreenPos();
            float row_height = ImGui::GetTextLineHeightWithSpacing();

            // Table internal scroll (because of ImGuiTableFlags_ScrollY)
            float scroll_y = ImGui::GetScrollY();

            // Convert scroll in pixels to "row offset"
            int first_visible_row = (int)(scroll_y / row_height + 0.5f);

            // Draw arrows now, aligned to the actual row origin
       

            const uint32_t seg_base = cs << 4;

            // Clipper for performance
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(cached_lines_.size()));

            while(clipper.Step()) {
                for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const auto& line = cached_lines_[row];

                    // Use cached symbol info
                    const std::string& symbol_name = line.cached_symbol_name;
                    const std::string& symbol_source = line.cached_symbol_source;

                    ImGui::PushID(row);
                    ImGui::TableNextRow();

                    // Address Column
                    ImGui::TableNextColumn();

                    // Highlight Execution Point when paused
                    if(paused && line.address == linear_eip) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 100, 50, 255));
                    }
                    // Highlight Breakpoint
                    bool is_bp = cached_breakpoints_.count(line.address) > 0;
                    if(is_bp) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(120, 40, 40, 200));
                    }

                    // Display as CS:Offset
                    char addr_buf[32] = {};
                    format_hex4(addr_buf, cs);
                    addr_buf[4] = ':';
                    format_hex4(addr_buf + 5, static_cast<uint16_t>((line.address - seg_base) & 0xFFFF));
                    addr_buf[9] = '\0';

                    if(ImGui::Selectable(addr_buf, selected_address_ == line.address, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                        selected_address_ = line.address;
                    }

                    // Toggle Breakpoint on Double Click
                    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        bool handled = toggleSymbolicBreakpoint(line.address);
                        if(!handled && debug_interface_) {
                            debug_interface_->toggleBreakpoint(line.address);
                        }
                        requested_refresh = true;
                    }

                    // Context Menu
                    char context_id[64];
                    std::snprintf(context_id, sizeof(context_id), "LineContext##Disasm_%p_%08X", this, line.address);
                    if(ImGui::BeginPopupContextItem(context_id)) {
                        if(ImGui::MenuItem("Run to Cursor (F4)")) debug_interface_->runToCursor(line.address);

                        // Run until return menu item
                        auto ret_addr = getCurrentReturnAddress();
                        bool can_run_until_ret = ret_addr.has_value();
                        if(ImGui::MenuItem("Run Until Return", nullptr, false, can_run_until_ret)) {
                            if(ret_addr && debug_interface_) {
                                debug_interface_->runToCursor(*ret_addr);
                            }
                        }

                        if(ImGui::MenuItem("Toggle Breakpoint")) {
                            bool handled = toggleSymbolicBreakpoint(line.address);
                            if(!handled && debug_interface_) {
                                debug_interface_->toggleBreakpoint(line.address);
                            }
                            requested_refresh = true;
                        }
                        if(ImGui::MenuItem("Set Next Statement")) debug_interface_->setCpuRegister("EIP", line.address);
                        if(trace_logger_) {
                            if(ImGui::MenuItem("Set Trace Start Here")) {
                                trace_start_addr_ = line.address;
                                trace_range_enabled_ = true;
                            }
                            if(ImGui::MenuItem("Set Trace End Here")) {
                                trace_end_addr_ = line.address;
                                trace_range_enabled_ = true;
                            }
                            if(ImGui::MenuItem("Trace This Range", nullptr,
                                false, trace_range_enabled_ && trace_start_addr_ <= trace_end_addr_)) {
                                applyTraceRangeFilter();
                            }
                        }
                        ImGui::EndPopup();
                    }

                    // Bytes Column
                    ImGui::TableNextColumn();
                    std::string bytes_display = line.opcodes;
                    if(bytes_display.size() > 64) {
                        bytes_display.resize(61);
                        bytes_display.append("...");
                    }
                    ImGui::TextUnformatted(bytes_display.c_str());

                    // Label Column (Symbol names)
                    ImGui::TableNextColumn();
                    if(!symbol_source.empty()) {
                        ImGui::TextDisabled("[%s]", symbol_name.c_str());
                    }
                    else if(!symbol_name.empty()) {
                        ImGui::TextDisabled("[%s]", symbol_name.c_str());
                    }
                    else {
                        ImGui::TextUnformatted("");
                    }

                    // Instruction Column (Mnemonic + Operands only)
                    ImGui::TableNextColumn();
                    ImVec4 color = getMnemonicColor(line.mnemonic);
                    bool use_color = symbol_source.empty();
                    if(use_color) {
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                    }
                    if(!symbol_source.empty()) {
                        ImGui::TextUnformatted(symbol_source.c_str());
                    }
                    else {
                        ImGui::Text("%s %s",
                            line.mnemonic.empty() ? "?" : line.mnemonic.c_str(),
                            line.operands.c_str());
                    }
                    if(use_color) {
                        ImGui::PopStyleColor();
                    }

                    // Hover Inspection
                    if(ImGui::IsItemHovered()) {
                        handleHoverInspection(line);
                    }

                    // Memory Reference Column (lazy evaluation)
                    if(show_memrefs_) {
                        ImGui::TableNextColumn();

                        // Lazy evaluation: only compute if not already computed
                        DisassemblyLine& mutable_line = const_cast<DisassemblyLine&>(line);
                        if(!mutable_line.memref_computed) {
                            LuaEngineDebug::CPUState cpu_state = debug_interface_->getCpuState();
                            mutable_line.cached_memref = memory_analyzer_.analyzeOperands(
                                line.operands,
                                line.mnemonic,
                                cpu_state,
                                debug_interface_
                            );
                            mutable_line.memref_computed = true;
                        }

                        // Display memory reference if valid
                        if(mutable_line.cached_memref.valid && !mutable_line.cached_memref.display_text.empty()) {
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s",
                                mutable_line.cached_memref.display_text.c_str());
                        }
                        else {
                            ImGui::TextUnformatted("");
                        }
                    }

                    ImGui::PopID();
                }
            } // End clipper

            ImGui::EndTable();

            // Infinite scrolling: Load more lines when scrolling near the bottom
            // PR4: Cap cached_lines_ at CACHED_LINES_CAP (500) to prevent unbounded growth
            scroll_y = ImGui::GetScrollY();
            float scroll_max_y = ImGui::GetScrollMaxY();
            if(scroll_max_y > 0 && scroll_y >= scroll_max_y * 0.9f) {
                // Near bottom - load more lines
                if(!cached_lines_.empty()) {
                    uint32_t last_address = cached_lines_.back().address;
                    uint32_t next_address = last_address + cached_lines_.back().instruction_length;
                    auto more_lines = debug_interface_->disassembleRange(next_address, 64);

                    // Recycle from top if adding would exceed cap
                    size_t to_add = more_lines.size();
                    if(cached_lines_.size() + to_add > CACHED_LINES_CAP) {
                        size_t excess = (cached_lines_.size() + to_add) - CACHED_LINES_CAP;
                        if(excess < cached_lines_.size()) {
                            // Shift anchor forward
                            anchor_address_ = cached_lines_[excess].address;
                            cached_lines_.erase(cached_lines_.begin(),
                                               cached_lines_.begin() + static_cast<ptrdiff_t>(excess));
                        } else {
                            cached_lines_.clear();
                            anchor_address_ = next_address;
                        }
                    }

                    for(const auto& l : more_lines) {
                        if(cached_lines_.size() >= CACHED_LINES_CAP) break;
                        DisassemblyLine dl{};
                        dl.address = l.address;
                        dl.opcodes = (l.bytes.size() > 64) ? l.bytes.substr(0, 64) : l.bytes;
                        dl.mnemonic = l.mnemonic;
                        dl.operands = l.operands;
                        dl.disassembly = l.full_text;
                        dl.instruction_length = l.length;
                        cached_lines_.push_back(std::move(dl));
                    }
                }
            }
        }

        ImGui::EndChild();

        if(requested_refresh) {
            refreshCachedLines();
        }

        ImGui::PopID();
    }

    // -----------------------------------------------------------------------------
    // Registers Tab (Safe Editing)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderRegistersTab() {
        if(!debug_interface_) {
            ImGui::TextUnformatted("Debug interface not available.");
            return;
        }

        char reg_tab_id[64];
        std::snprintf(reg_tab_id, sizeof(reg_tab_id), "RegistersTab_%p", this);
        ImGui::PushID(reg_tab_id);

        LuaEngineDebug::CPUState state = debug_interface_->getCpuState();
        static LuaEngineDebug::CPUState prev_state = state;

        char regs_table_id[64];
        std::snprintf(regs_table_id, sizeof(regs_table_id), "Regs##Disasm_%p", this);
        if(ImGui::BeginTable(regs_table_id, 3, ImGuiTableFlags_RowBg)) {
            auto draw_reg = [&](const char* name, uint32_t value, uint32_t prev_value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                uint32_t val_copy = value;
                ImGui::SetNextItemWidth(120);
                char label[32];
                std::snprintf(label, sizeof(label), "##%s", name);
                if(ImGui::InputScalar(label, ImGuiDataType_U32, &val_copy, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                    debug_interface_->setCpuRegister(name, val_copy);
                }
                ImGui::TableNextColumn();
                if(value != prev_value) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "*");
                }
                };

            draw_reg("EAX", state.eax, prev_state.eax);
            draw_reg("EBX", state.ebx, prev_state.ebx);
            draw_reg("ECX", state.ecx, prev_state.ecx);
            draw_reg("EDX", state.edx, prev_state.edx);
            draw_reg("ESI", state.esi, prev_state.esi);
            draw_reg("EDI", state.edi, prev_state.edi);
            draw_reg("EBP", state.ebp, prev_state.ebp);
            draw_reg("ESP", state.esp, prev_state.esp);
            ImGui::EndTable();
        }

        prev_state = state;
        ImGui::PopID();
    }

    // -----------------------------------------------------------------------------
    // Symbol Management Dialog (Popup)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderSymbolDialog() {
        if(!show_symbol_dialog_) return;

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Symbol Management", &show_symbol_dialog_)) {
            if(!symbol_manager_) {
                ImGui::TextUnformatted("Symbol manager not available.");
                ImGui::End();
                return;
            }

            static char path_buffer[512] = "";
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("Map Path", path_buffer, sizeof(path_buffer));
            ImGui::SameLine();

            if(ImGui::Button("Load")) {
                // TODO: Call symbol manager load function
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Loaded Segments (Rebase for relocation):");

            auto mappings = symbol_manager_->getSegmentMappings();
            if(ImGui::BeginChild("SegmentList", ImVec2(0, 0), true)) {
                for(size_t idx = 0; idx < mappings.size(); ++idx) {
                    auto& map = mappings[idx];
                    ImGui::PushID(static_cast<int>(idx));
                    ImGui::Text("%s (File Seg: %04X)", map.segment_name.c_str(), map.file_segment);
                    ImGui::SameLine();

                    uint32_t base = map.memory_base;
                    ImGui::SetNextItemWidth(100);
                    if(ImGui::InputScalar("Base", ImGuiDataType_U32, &base, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                        symbol_manager_->updateSegmentMapping(map.segment_name, base);
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    // -----------------------------------------------------------------------------
    // Symbol Loading (MASM Map Parser)
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderSymbolsTab() {
        if(!symbol_manager_) {
            ImGui::TextUnformatted("Symbol manager not available.");
            return;
        }

        char symbols_tab_id[64];
        std::snprintf(symbols_tab_id, sizeof(symbols_tab_id), "SymbolsTab_%p", this);
        ImGui::PushID(symbols_tab_id);

        static char path_buffer[512] = "";
        ImGui::SetNextItemWidth(300);
        char map_label[64];
        std::snprintf(map_label, sizeof(map_label), "Map Path##symbols_%p", this);
        ImGui::InputText(map_label, path_buffer, sizeof(path_buffer));
        ImGui::SameLine();

        ImGui::Separator();
        ImGui::TextUnformatted("Loaded Segments (Rebase for relocation):");

        auto mappings = symbol_manager_->getSegmentMappings();
        for(size_t idx = 0; idx < mappings.size(); ++idx) {
            auto& map = mappings[idx];
            ImGui::PushID(static_cast<int>(idx));
            ImGui::Text("%s (File Seg: %04X)", map.segment_name.c_str(), map.file_segment);
            ImGui::SameLine();

            uint32_t base = map.memory_base;
            ImGui::SetNextItemWidth(100);
            if(ImGui::InputScalar("Base", ImGuiDataType_U32, &base, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                symbol_manager_->updateSegmentMapping(map.segment_name, base);
            }
            ImGui::PopID();
        }

        ImGui::PopID();
    }

    // -----------------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------------
    ImVec4 DisassemblyWindow::getMnemonicColor(const std::string& mnemonic) {
        std::string upper_mnemonic = mnemonic;
        std::transform(upper_mnemonic.begin(), upper_mnemonic.end(), upper_mnemonic.begin(), ::toupper);

        // Control Flow (Cyan)
        if(upper_mnemonic == "CALL" || upper_mnemonic == "RET" || upper_mnemonic == "RETF" || upper_mnemonic == "RETN" ||
            upper_mnemonic == "IRET" || upper_mnemonic == "IRETD")
            return ImVec4(0.0f, 0.8f, 1.0f, 1.0f);

        // Jumps (Yellow)
        if(upper_mnemonic == "JMP" || (!upper_mnemonic.empty() && upper_mnemonic[0] == 'J'))
            return ImVec4(1.0f, 0.9f, 0.3f, 1.0f);

        // Loops (Orange)
        if(upper_mnemonic.find("LOOP") == 0)
            return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);

        // Interrupts (Magenta)
        if(upper_mnemonic == "INT" || upper_mnemonic == "INTO" || upper_mnemonic == "UD2")
            return ImVec4(1.0f, 0.4f, 1.0f, 1.0f);

        // Stack (Muted Blue)
        if(upper_mnemonic == "PUSH" || upper_mnemonic == "POP" || upper_mnemonic == "PUSHA" || upper_mnemonic == "POPA" ||
            upper_mnemonic == "PUSHAD" || upper_mnemonic == "POPAD" || upper_mnemonic == "PUSHF" || upper_mnemonic == "POPF" ||
            upper_mnemonic == "PUSHFD" || upper_mnemonic == "POPFD" || upper_mnemonic == "ENTER" || upper_mnemonic == "LEAVE")
            return ImVec4(0.6f, 0.6f, 0.9f, 1.0f);

        // Comparison/Test (Bright Orange/Red)
        if(upper_mnemonic == "CMP" || upper_mnemonic == "TEST")
            return ImVec4(1.0f, 0.5f, 0.4f, 1.0f);

        // Arithmetic/Logic (Green)
        if(upper_mnemonic == "ADD" || upper_mnemonic == "SUB" || upper_mnemonic == "INC" || upper_mnemonic == "DEC" ||
            upper_mnemonic == "MUL" || upper_mnemonic == "IMUL" || upper_mnemonic == "DIV" || upper_mnemonic == "IDIV" ||
            upper_mnemonic == "AND" || upper_mnemonic == "OR" || upper_mnemonic == "XOR" || upper_mnemonic == "NOT" ||
            upper_mnemonic == "NEG" || upper_mnemonic == "ADC" || upper_mnemonic == "SBB" ||
            upper_mnemonic == "SHL" || upper_mnemonic == "SHR" || upper_mnemonic == "SAL" || upper_mnemonic == "SAR" ||
            upper_mnemonic == "ROL" || upper_mnemonic == "ROR" || upper_mnemonic == "RCL" || upper_mnemonic == "RCR")
            return ImVec4(0.5f, 1.0f, 0.5f, 1.0f);

        // String operations (Pink)
        // MOVSX is excluded as it is not a string op
        if((upper_mnemonic.find("MOVS") == 0 && upper_mnemonic.find("MOVSX") == std::string::npos) ||
            upper_mnemonic.find("STOS") == 0 || upper_mnemonic.find("LODS") == 0 ||
            upper_mnemonic.find("SCAS") == 0 || upper_mnemonic.find("CMPS") == 0 ||
            upper_mnemonic.find("INS") == 0 || upper_mnemonic.find("OUTS") == 0 ||
            upper_mnemonic.find("REP") == 0)
            return ImVec4(1.0f, 0.6f, 0.8f, 1.0f);

        // I/O (Purple)
        if(upper_mnemonic == "IN" || upper_mnemonic == "OUT")
            return ImVec4(0.8f, 0.4f, 1.0f, 1.0f);

        // NOP (Grey)
        if(upper_mnemonic == "NOP" || upper_mnemonic == "HLT" || upper_mnemonic == "WAIT")
            return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

        return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);          // Default
    }

    uint32_t DisassemblyWindow::parseJumpTarget(const std::string& operands, uint32_t current_addr) {
        // Very simple parser: try to parse absolute hex, otherwise relative displacement like "short -0x5"
        try {
            if(!operands.empty() && operands.find("0x") != std::string::npos) {
                size_t pos = operands.find("0x");
                return std::stoul(operands.substr(pos), nullptr, 16);
            }

            // Relative offset (e.g., "5" or "-5")
            int32_t rel = std::stoi(operands, nullptr, 0);
            return static_cast<uint32_t>(current_addr + rel);
        }
        catch(...) {
            return 0;
        }
    }

    // -----------------------------------------------------------------------------
    // Navigation Controls
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderNavigationControls() {
        // Initialize static strings on first call
        if(!navigation_initialized_) {
            std::snprintf(segment_input_, sizeof(segment_input_), "%04X", debug_interface_->getCurrentCS());
            std::snprintf(offset_input_, sizeof(offset_input_), "%04X", debug_interface_->getCurrentEIP());
            std::snprintf(address_input_, sizeof(address_input_), "%08X", current_address_);
            navigation_initialized_ = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Navigation:");
        ImGui::SameLine();

        // Jump to CS:IP button
        if(ImGui::Button("Jump to CS:IP")) {
            uint32_t cs = debug_interface_->getCurrentCS();
            uint32_t ip = debug_interface_->getCurrentEIP();
            current_address_ = (cs << 4) + ip;
            refreshCachedLines();
            std::snprintf(segment_input_, sizeof(segment_input_), "%04X", cs);
            std::snprintf(offset_input_, sizeof(offset_input_), "%04X", ip);
            std::snprintf(address_input_, sizeof(address_input_), "%08X", current_address_);
        }

        ImGui::SameLine();

        // Follow execution checkbox
        if(ImGui::Checkbox("Follow Execution", &follow_execution_)) {
            if(follow_execution_) {
                // When re-enabling follow, reset user navigation flag
                user_navigated_ = false;
            }
        }

        ImGui::Spacing();

        // Segment:Offset input
        ImGui::TextUnformatted("Segment:Offset:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(60);
        ImGui::InputText("##segment", segment_input_, sizeof(segment_input_), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();

        ImGui::TextUnformatted(":");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(60);
        ImGui::InputText("##offset", offset_input_, sizeof(offset_input_), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();

        if(ImGui::Button("Go##goto_segment_offset")) {
            try {
                uint16_t segment = static_cast<uint16_t>(std::stoul(segment_input_, nullptr, 16));
                uint16_t offset = static_cast<uint16_t>(std::stoul(offset_input_, nullptr, 16));
                current_address_ = (segment << 4) + offset;
                user_navigated_ = true;  // Mark that user manually navigated
                refreshCachedLines();
                std::snprintf(address_input_, sizeof(address_input_), "%08X", current_address_);
            }
            catch(...) {
                // Invalid input - ignore
            }
        }

        ImGui::SameLine();

        // Linear address input
        ImGui::TextUnformatted("Linear:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##linear_addr", address_input_, sizeof(address_input_), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();

        if(ImGui::Button("Go##goto_linear")) {
            try {
                uint32_t linear_addr = static_cast<uint32_t>(std::stoul(address_input_, nullptr, 16));
                current_address_ = linear_addr;
                user_navigated_ = true;  // Mark that user manually navigated
                refreshCachedLines();

                // Update segment:offset display
                uint16_t segment = static_cast<uint16_t>(linear_addr >> 4);
                uint16_t offset = static_cast<uint16_t>(linear_addr & 0xFFFF);
                std::snprintf(segment_input_, sizeof(segment_input_), "%04X", segment);
                std::snprintf(offset_input_, sizeof(offset_input_), "%04X", offset);
            }
            catch(...) {
                // Invalid input - ignore
            }
        }

        // Symbol jumping
        ImGui::Spacing();
        ImGui::TextUnformatted("Symbol:");
        ImGui::SameLine();

        static std::string selected_symbol = "";
        static std::vector<std::string> available_symbols;

        // Refresh symbol list if manager is available
        if(symbol_manager_ && available_symbols.empty()) {
            std::vector<std::string> symbol_names = symbol_manager_->getSymbolNames();
            available_symbols = symbol_names;
        }

        // Symbol combo box with search capability
        if(ImGui::BeginCombo("##symbol_jump", selected_symbol.empty() ? "Select symbol..." : selected_symbol.c_str())) {
            // Add search text input at the top
            static char symbol_search[256] = "";
            ImGui::InputText("##symbol_search", symbol_search, sizeof(symbol_search));
            ImGui::Separator();

            // Filter and display symbols
            for(const auto& symbol : available_symbols) {
                bool matches = true;
                if(strlen(symbol_search) > 0) {
                    matches = symbol.find(symbol_search) != std::string::npos;
                }

                if(matches && ImGui::Selectable(symbol.c_str(), selected_symbol == symbol)) {
                    selected_symbol = symbol;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // Jump to symbol button
        if(ImGui::Button("Go##goto_symbol") && !selected_symbol.empty() && symbol_manager_) {
            uint32_t symbol_addr = symbol_manager_->getSymbolAddress(selected_symbol);
            if(symbol_addr != LuaEngineSymbols::SymbolManager::INVALID_ADDRESS) {
                current_address_ = symbol_addr;
                user_navigated_ = true;  // Mark that user manually navigated
                refreshCachedLines();

                // Update all input displays
                uint16_t segment = static_cast<uint16_t>(symbol_addr >> 4);
                uint16_t offset = static_cast<uint16_t>(symbol_addr & 0xFFFF);
                std::snprintf(segment_input_, sizeof(segment_input_), "%04X", segment);
                std::snprintf(offset_input_, sizeof(offset_input_), "%04X", offset);
                std::snprintf(address_input_, sizeof(address_input_), "%08X", current_address_);
            }
        }

        ImGui::SameLine();

        // Refresh symbols button
        if(ImGui::Button("Refresh##refresh_symbols")) {
            if(symbol_manager_) {
                available_symbols.clear();
                std::vector<std::string> symbol_names = symbol_manager_->getSymbolNames();
                available_symbols = symbol_names;
            }
        }

        // Common segment quick jumps
        ImGui::Spacing();
        ImGui::TextUnformatted("Quick Segments:");
        ImGui::SameLine();

        const struct { const char* name; uint16_t segment; } common_segments[] = {
            {"CS", 0}, {"DS", 0}, {"ES", 0}, {"SS", 0},
            {"Bios", 0xF000}, {"Video", 0xA000}, {"Text", 0xB800}
        };

        for(size_t i = 0; i < sizeof(common_segments) / sizeof(common_segments[0]); ++i) {
            if(i > 0) ImGui::SameLine();

            char button_label[32];
            std::snprintf(button_label, sizeof(button_label), "%s##%zu", common_segments[i].name, i);

            if(ImGui::Button(button_label)) {
                uint16_t segment = common_segments[i].segment;
                if(strcmp(common_segments[i].name, "CS") == 0) {
                    segment = debug_interface_->getCurrentCS();
                }
                // DS, ES, SS would need to be implemented in CoreDebugInterface
                // For now, using fixed segment values

                current_address_ = (segment << 4); // Jump to segment base
                user_navigated_ = true;  // Mark that user manually navigated
                refreshCachedLines();

                // Update displays
                std::snprintf(segment_input_, sizeof(segment_input_), "%04X", segment);
                std::snprintf(offset_input_, sizeof(offset_input_), "0000");
                std::snprintf(address_input_, sizeof(address_input_), "%08X", current_address_);
            }
        }

        // Display current location info
        ImGui::Spacing();
        uint16_t current_seg = static_cast<uint16_t>(current_address_ >> 4);
        uint16_t current_off = static_cast<uint16_t>(current_address_ & 0xFFFF);
        ImGui::Text("Current: %04X:%04X (%08X)", current_seg, current_off, current_address_);

        ImGui::Separator();
    }

    // -----------------------------------------------------------------------------
    // Dump Memory Dialog
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderDumpDialog() {
        if(ImGui::Begin("Dump Memory", &show_dump_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Start Address (Hex/Linear):");
            ImGui::InputText("##DumpStart", dump_start_input_, sizeof(dump_start_input_), ImGuiInputTextFlags_CharsHexadecimal);

            ImGui::Text("Length (Hex Bytes):");
            ImGui::InputText("##DumpLength", dump_length_input_, sizeof(dump_length_input_), ImGuiInputTextFlags_CharsHexadecimal);

            ImGui::Text("Filename:");
            ImGui::InputText("##DumpFile", dump_filename_input_, sizeof(dump_filename_input_));

            ImGui::Separator();

            if(ImGui::Button("Dump", ImVec2(120, 0))) {
                performMemoryDump();
                show_dump_dialog_ = false;
            }

            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_dump_dialog_ = false;
            }

            ImGui::End();
        }
    }

    void DisassemblyWindow::performMemoryDump() {
        if(!debug_interface_) return;

        try {
            uint32_t start_addr = std::stoul(dump_start_input_, nullptr, 16);
            uint32_t length = std::stoul(dump_length_input_, nullptr, 16);

            std::vector<uint8_t> buffer(length);
            debug_interface_->readMemoryBlock(start_addr, buffer.data(), length);

            std::ofstream file(dump_filename_input_, std::ios::binary);
            if(file.is_open()) {
                file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                file.close();
            }
        }
        catch(...) {
            // Ignore parse errors or file errors
        }
    }

    // -----------------------------------------------------------------------------
    // Call Stack Panel
    // -----------------------------------------------------------------------------
    void DisassemblyWindow::renderCallStack() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
        ImGui::BeginChild("CallStackPanel", ImVec2(0, 150), true);
        ImGui::PopStyleColor();

        ImGui::TextUnformatted("Call Stack");
        ImGui::Separator();

        // Try to get stack from live debugger first
        std::vector<LuaEngineDebug::CallStackFrame> stack;
        if(debug_interface_) {
            stack = debug_interface_->getCallStack();
        }

        // Smart Fallback:
        // If the live debugger stack failed (only returned current IP or empty) due to
        // missing frame pointers (common in optimized code), fall back to the
        // Trace Logger's history-based stack if available.
        if((stack.empty() || stack.size() <= 1) && trace_logger_) {
            auto trace_stack = trace_logger_->getCallStack();
            if(!trace_stack.empty()) {
                stack.clear();
                // Trace stack is usually chronological (pushed on call).
                // We want most recent (top) first.
                for(auto it = trace_stack.rbegin(); it != trace_stack.rend(); ++it) {
                    LuaEngineDebug::CallStackFrame frame(*it);

                    // Attempt to populate CS:IP using current CS when plausible
                    if(debug_interface_) {
                        uint16_t cs = debug_interface_->getCurrentCS();
                        uint32_t cs_base = static_cast<uint32_t>(cs) << 4;

                        if(*it >= cs_base && *it < cs_base + 0x10000) {
                            frame.code_segment = cs;
                            frame.instruction_pointer = *it - cs_base;
                        }
                        else {
                            // Use canonical calculation for real mode
                            frame.code_segment = static_cast<uint16_t>((*it >> 4) & 0xFFFF);
                            frame.instruction_pointer = static_cast<uint16_t>(*it - (static_cast<uint32_t>(frame.code_segment) << 4));
                        }
                    }
                    stack.emplace_back(frame);
                }
            }
        }

        if(stack.empty()) {
            ImGui::TextUnformatted("<empty>");
        }
        else {
            // Display stack from top to bottom (most recent call first)
            // Note: getCallStack usually returns most recent first or last.
            // Assuming debugger returns [current, caller, caller...] (top-down)
            // If trace logger returns history (bottom-up), we might need to reverse.
            // Let's assume top-down for standard debugger style.

            for(size_t i = 0; i < stack.size(); ++i) {
                uint32_t addr = stack[i].address;

                // Prefer explicit CS:IP from the frame (avoids incorrect reverse calc)
                uint16_t seg = stack[i].code_segment;
                uint16_t off = static_cast<uint16_t>(stack[i].instruction_pointer & 0xFFFF);

                // Fallback: derive a reasonable seg:off if not provided
                if(seg == 0 && addr != 0) {
                    // Use canonical calculation for real mode
                    // Linear address = (segment << 4) + offset
                    seg = static_cast<uint16_t>((addr >> 4) & 0xF000); // Common real mode pattern
                    off = static_cast<uint16_t>(addr - (static_cast<uint32_t>(seg) << 4));
                }

                // Use linear address from frame if present, otherwise rebuild from seg:off
                uint32_t display_linear = addr;
                if(display_linear == 0 && seg != 0) {
                    display_linear = (static_cast<uint32_t>(seg) << 4) + off;
                }

                // Look up symbol for this address if symbol manager is available
                std::string symbol_name;
                if(symbol_manager_) {
                    if(auto* sym = symbol_manager_->findSymbol(addr)) {
                        symbol_name = sym->name;
                    }
                    else {
                        // Try to get nearest symbol name
                        symbol_name = symbol_manager_->getSymbolName(addr, false);
                    }
                }

                // Use symbol from CallStackFrame if available, otherwise use our lookup
                if(!stack[i].function_name.empty()) {
                    symbol_name = stack[i].function_name;
                }

                char label[256];
                if(!symbol_name.empty()) {
                    std::snprintf(label, sizeof(label), "%02zu: %04X:%04X  %s", i, seg, off, symbol_name.c_str());
                }
                else {
                    std::snprintf(label, sizeof(label), "%02zu: %04X:%04X  (%08X)", i, seg, off, display_linear);
                }

                if(ImGui::Selectable(label, false)) {
                    // Clicking navigates disassembly to that address
                    setCurrentAddress(display_linear);
                    refreshCachedLines();
                }
            }
        }

        ImGui::EndChild();
    }

    // -----------------------------------------------------------------------------
    // Helper: Get current return address from call stack
    // -----------------------------------------------------------------------------
    std::optional<uint32_t> DisassemblyWindow::getCurrentReturnAddress() const {
        // Prefer debugger interface
        if(debug_interface_) {
            auto stack = debug_interface_->getCallStack();
            if(stack.size() >= 2) {
                // Use linear address if provided, otherwise derive from CS:IP
                const auto& frame = stack[1];
                if(frame.address != 0) {
                    return frame.address;
                }
                if(frame.code_segment != 0) {
                    return (static_cast<uint32_t>(frame.code_segment) << 4) +
                        static_cast<uint32_t>(frame.instruction_pointer & 0xFFFF);
                }
            }
        }

        // Fallback to trace logger history
        if(trace_logger_) {
            auto stack = trace_logger_->getCallStack();
            if(!stack.empty()) return stack.back();
        }

        // Last resort: try to read return address directly from stack
        if(debug_interface_) {
            // Read return address from stack (assuming standard calling convention)
            // In real mode with 16-bit stack: [SS:SP] or [SS:BP+2]
            // In 32-bit mode: [SS:ESP] or [SS:EBP+4]

            auto state = debug_interface_->getCpuState();
            uint32_t ss_base = static_cast<uint32_t>(state.ss) << 4;  // Real mode calculation

            // Try BP+2 (16-bit) or EBP+4 (32-bit) for return address
            // This assumes we're inside a function with standard prologue
            bool is_32bit = (state.ebp > 0xFFFF);  // Heuristic

            uint32_t ret_addr;
            if(is_32bit) {
                ret_addr = debug_interface_->readDWord(ss_base + state.ebp + 4);
            } else {
                ret_addr = debug_interface_->readWord(ss_base + (state.ebp & 0xFFFF) + 2);
            }

            // Validate: return address should be in reasonable code range
            if(ret_addr > 0 && ret_addr < 0x100000) {  // < 1MB for real mode
                return ret_addr;
            }
        }

        return std::nullopt;
    }

} // namespace LuaEngineDebugger
