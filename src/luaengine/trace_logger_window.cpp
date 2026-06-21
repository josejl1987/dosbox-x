#include "trace_logger_window.h"
#include "imgui.h"
#include "logging.h"
#include "symbol_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <climits> // PATH_MAX

namespace LuaEngineTraceLogger {

    //=============================================================================
    // TraceLoggerWindow Implementation
    //=============================================================================

    TraceLoggerWindow::TraceLoggerWindow()
        : show_window_(true), show_filter_dialog_(false), show_search_dialog_(false),
        show_statistics_dialog_(false), show_export_dialog_(false),
        current_page_(0), entries_per_page_(100), selected_entry_index_(-1),
        auto_scroll_(true), follow_execution_(true), show_registers_(true),
        show_instruction_bytes_(false), show_changes_only_(false),
        highlight_calls_(true), highlight_jumps_(true),
        highlight_interrupts_(true), address_filter_enabled_(false),
        mnemonic_filter_enabled_(false), trace_logger_(nullptr), symbol_manager_(nullptr) {

        // Initialize search state
        search_input_[0] = '\0';
        address_filter_start_[0] = '\0';
        address_filter_end_[0] = '\0';
        mnemonic_filter_[0] = '\0';
        export_filename_[0] = '\0';
        strncpy(export_filename_, "trace_log.txt", sizeof(export_filename_) - 1);
        export_filename_[sizeof(export_filename_) - 1] = '\0';
        export_format_index_ = 0;

        // Initialize column widths
        column_widths_[0] = 100.0f;  // Address
        column_widths_[1] = 300.0f;  // Disassembly
        column_widths_[2] = 400.0f;  // Registers
        column_widths_[3] = 80.0f;   // Frame
        column_widths_[4] = 100.0f;  // Timestamp
        column_widths_[5] = 150.0f;  // Label
    }

    TraceLoggerWindow::~TraceLoggerWindow() {
    }

    void TraceLoggerWindow::initialize(TraceLogger* trace_logger, LuaEngineSymbols::SymbolManager* symbol_manager) {
        trace_logger_ = trace_logger;
        symbol_manager_ = symbol_manager ? symbol_manager : LuaEngineSymbols::g_symbol_manager;

        // Set up callbacks
        if(trace_logger_) {
            trace_logger_->onEntryAdded = [this](const TraceEntry& entry) {
                if(auto_scroll_) {
                    scrollToBottom();
                }
                };

            trace_logger_->onTraceCleared = [this]() {
                current_page_ = 0;
                selected_entry_index_ = -1;
                };

            trace_logger_->onTracingStateChanged = [this](bool enabled) {
                if(enabled && follow_execution_) {
                    scrollToBottom();
                }
                };
        }
    }

    void TraceLoggerWindow::render() {
        if(!show_window_) return;

        if(ImGui::Begin("Trace Logger", &show_window_, ImGuiWindowFlags_MenuBar)) {
            renderMenuBar();
            renderToolbar();
            renderTraceTable();
            renderStatusBar();
        }
        ImGui::End();

        // Open popups in correct context before rendering dialogs
        if(show_filter_dialog_ && !ImGui::IsPopupOpen("Filter Options")) {
            ImGui::OpenPopup("Filter Options");
        }
        if(show_search_dialog_ && !ImGui::IsPopupOpen("Search Trace")) {
            ImGui::OpenPopup("Search Trace");
        }
        if(show_statistics_dialog_ && !ImGui::IsPopupOpen("Trace Statistics")) {
            ImGui::OpenPopup("Trace Statistics");
        }
        if(show_export_dialog_ && !ImGui::IsPopupOpen("Export Trace")) {
            ImGui::OpenPopup("Export Trace");
        }

        // Render dialogs
        if(show_filter_dialog_) {
            renderFilterDialog();
        }

        if(show_search_dialog_) {
            renderSearchDialog();
        }

        if(show_statistics_dialog_) {
            renderStatisticsDialog();
        }

        if(show_export_dialog_) {
            renderExportDialog();
        }
    }

    void TraceLoggerWindow::renderMenuBar() {
        if(ImGui::BeginMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                if(ImGui::MenuItem("Export Trace...", "Ctrl+E")) {
                    openExportDialog();
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Clear Trace", "Ctrl+L")) {
                    clearTrace();
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Registers", nullptr, &show_registers_);
                ImGui::MenuItem("Show Instruction Bytes", nullptr, &show_instruction_bytes_);
                ImGui::Separator();
                ImGui::MenuItem("Highlight Calls", nullptr, &highlight_calls_);
                ImGui::MenuItem("Highlight Jumps", nullptr, &highlight_jumps_);
                ImGui::MenuItem("Highlight Interrupts", nullptr, &highlight_interrupts_);
                ImGui::Separator();
                ImGui::MenuItem("Auto Scroll", nullptr, &auto_scroll_);
                ImGui::MenuItem("Follow Execution", nullptr, &follow_execution_);
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Search")) {
                if(ImGui::MenuItem("Find...", "Ctrl+F")) {
                    openSearchDialog();
                }
                if(ImGui::MenuItem("Find by Address...", "Ctrl+A")) {
                    openAddressSearch();
                }
                if(ImGui::MenuItem("Find by Mnemonic...", "Ctrl+M")) {
                    openMnemonicSearch();
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Filter")) {
                if(ImGui::MenuItem("Filter Options...", "Ctrl+Shift+F")) {
                    openFilterDialog();
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Clear Filters")) {
                    clearFilters();
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Tools")) {
                if(ImGui::MenuItem("Statistics...", "Ctrl+S")) {
                    openStatisticsDialog();
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Reset Statistics")) {
                    resetStatistics();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    void TraceLoggerWindow::renderToolbar() {
        if(!trace_logger_) return;

        // Tracing controls
        bool is_enabled = trace_logger_->isEnabled();
        bool is_paused = trace_logger_->isPaused();

        if(ImGui::Button(is_enabled ? "Stop" : "Start")) {
            try {
                trace_logger_->setEnabled(!is_enabled);
            }
            catch(const std::exception& e) {
                LOG(LOG_MISC, LOG_ERROR)("TraceLogger start/stop failed: %s", e.what());
            }
        }

        ImGui::SameLine();

        if(ImGui::Button(is_paused ? "Resume" : "Pause")) {
            if(is_paused) {
                trace_logger_->resume();
            }
            else {
                trace_logger_->pause();
            }
        }

        ImGui::SameLine();

        if(ImGui::Button("Step")) {
            trace_logger_->step();
        }

        ImGui::SameLine();

        if(ImGui::Button("Clear")) {
            clearTrace();
        }

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        // Filter controls
        if(ImGui::Button("Filter")) {
            openFilterDialog();
        }

        ImGui::SameLine();

        if(ImGui::Button("Search")) {
            openSearchDialog();
        }

        ImGui::SameLine();

        if(ImGui::Button("Export")) {
            openExportDialog();
        }

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        ImGui::Checkbox("Show Registers", &show_registers_);
        ImGui::SameLine();
        ImGui::Checkbox("Show Bytes", &show_instruction_bytes_);
        if(show_registers_) {
            ImGui::SameLine();
            ImGui::Checkbox("Show changed only", &show_changes_only_);
        }

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        // Memory and step logging controls
        bool log_memory = trace_logger_->getLogMemoryAccess();
        if(ImGui::Checkbox("Log Memory", &log_memory)) {
            trace_logger_->setLogMemoryAccess(log_memory);
        }

        ImGui::SameLine();

        bool log_step = trace_logger_->getLogOnStep();
        if(ImGui::Checkbox("Log on Step", &log_step)) {
            trace_logger_->setLogOnStep(log_step);
        }

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        // Navigation controls
        if(ImGui::Button("<<")) {
            current_page_ = 0;
        }

        ImGui::SameLine();

        if(ImGui::Button("<")) {
            if(current_page_ > 0) current_page_--;
        }

        ImGui::SameLine();

        size_t total_entries = trace_logger_->getRingEntryCount();
        if(total_entries == 0) total_entries = trace_logger_->getEntryCount();
        size_t total_pages = (total_entries + entries_per_page_ - 1) / entries_per_page_;

        ImGui::Text("Page %zu/%zu", current_page_ + 1, std::max(size_t(1), total_pages));

        ImGui::SameLine();

        if(ImGui::Button(">")) {
            if(current_page_ < total_pages - 1) current_page_++;
        }

        ImGui::SameLine();

        if(ImGui::Button(">>")) {
            if(total_pages > 0) current_page_ = total_pages - 1;
        }

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        // Page size control
        ImGui::Text("Page size:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        int page_size_int = static_cast<int>(entries_per_page_);
        if(ImGui::InputInt("##PageSize", &page_size_int, 0, 0)) {
            if(page_size_int >= 10 && page_size_int <= 1000) {
                entries_per_page_ = static_cast<size_t>(page_size_int);
                current_page_ = 0;  // Reset to first page
            }
        }
    }

    void TraceLoggerWindow::renderTraceTable() {
        if(!trace_logger_) return;

        // PR4: Use range-based retrieval instead of full-copy getEntries()
        size_t total_entries = trace_logger_->getRingEntryCount();
        if(total_entries == 0) {
            // Fall back to legacy count for backward compat
            total_entries = trace_logger_->getEntryCount();
        }

        if(total_entries == 0) {
            ImGui::Text("No trace entries. Start tracing to see execution flow.");
            return;
        }

        // Calculate visible entries
        size_t start_index = current_page_ * entries_per_page_;
        // PR4: Clamp to valid ring buffer range (avoid showing overwritten entries)
        size_t ring_count = trace_logger_->getRingEntryCount();
        if(ring_count > 0 && start_index >= ring_count) {
            start_index = (ring_count > entries_per_page_) ? ring_count - entries_per_page_ : 0;
            current_page_ = start_index / entries_per_page_;
        }
        size_t end_index = std::min(start_index + entries_per_page_, total_entries);

        // PR4: Fetch only the visible page from ring buffer via DecodeCache
        auto raw_entries = trace_logger_->getEntriesRange(start_index, end_index - start_index);
        auto* decode_cache = trace_logger_->getDecodeCache();
        auto* snapshot_store = trace_logger_->getSnapshotStore();
        auto* debug_iface = trace_logger_->getDebugInterface();

        // Calculate column count: Address, Label, Type, Disassembly, Memory, Frame, Time (7 base)
        int column_count = 7;
        if(show_registers_) column_count++;
        if(show_instruction_bytes_) column_count++;

        if(ImGui::BeginTable("TraceTable", column_count,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {

            // Setup columns
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, column_widths_[0]);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, column_widths_[5]);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Disassembly", ImGuiTableColumnFlags_WidthStretch, column_widths_[1]);

            if(show_registers_) {
                ImGui::TableSetupColumn("Registers", ImGuiTableColumnFlags_WidthFixed, column_widths_[2]);
            }

            if(show_instruction_bytes_) {
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            }

            ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, column_widths_[3]);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, column_widths_[4]);

            ImGui::TableHeadersRow();

            // Render entries — PR4: use RawTraceEntry + lazy decode
            LuaEngineTraceLogger::FastRegisterSnapshot prev_regs{};
            bool have_prev_regs = false;

            for(size_t i = 0; i < raw_entries.size(); ++i) {
                const auto& raw = raw_entries[i];

                // Lazy-decode disassembly via address-keyed cache
                const std::string& disasm_str = (decode_cache && debug_iface)
                    ? decode_cache->getOrDecode(raw.address, debug_iface)
                    : *(new std::string());  // ponytail: rare no-interface case, just empty

                // Get register snapshot for this entry
                const DebuggerUiSnapshot* snap = (snapshot_store)
                    ? &snapshot_store->get(raw.snapshot_index) : nullptr;

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(start_index + i));

                // Determine row color based on flow type (PR4: enum-based, no string scanning)
                bool should_highlight = false;
                ImVec4 highlight_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                if(highlight_calls_ && raw.flow_type == FlowType::CALL) {
                    should_highlight = true;
                    highlight_color = ImVec4(0.8f, 0.9f, 1.0f, 1.0f);  // Light blue
                }
                else if(highlight_jumps_ && (raw.flow_type == FlowType::JMP || raw.flow_type == FlowType::CONDITIONAL_BRANCH)) {
                    should_highlight = true;
                    highlight_color = ImVec4(1.0f, 0.9f, 0.8f, 1.0f);  // Light orange
                }
                else if(highlight_interrupts_ && raw.flow_type == FlowType::INTERRUPT) {
                    should_highlight = true;
                    highlight_color = ImVec4(1.0f, 0.8f, 0.8f, 1.0f);  // Light red
                }

                if(should_highlight) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(highlight_color));
                }

                // Address column — format CS:IP from raw entry
                ImGui::TableNextColumn();
                char addr_buf[16];
                std::snprintf(addr_buf, sizeof(addr_buf), "%04X:%04X", raw.cs, raw.ip);
                ImGui::TextUnformatted(addr_buf);

                // Label column
                ImGui::TableNextColumn();
                std::string label;

                if(symbol_manager_) {
                    if(auto* sym = symbol_manager_->findSymbol(raw.address)) {
                        label = sym->name;
                    }
                    else {
                        label = symbol_manager_->getSymbolName(raw.address, true);
                    }
                }

                if(!label.empty()) {
                    ImGui::TextDisabled("%s", label.c_str());
                }
                else {
                    ImGui::TextUnformatted("");
                }

                // Type column — flow type enum instead of string matching
                ImGui::TableNextColumn();
                const char* type_str = "INSTRUCTION";
                switch(raw.flow_type) {
                case FlowType::CALL: type_str = "CALL"; break;
                case FlowType::JMP: type_str = "JMP"; break;
                case FlowType::RET: type_str = "RET"; break;
                case FlowType::CONDITIONAL_BRANCH: type_str = "BRANCH"; break;
                case FlowType::INTERRUPT: type_str = "INT"; break;
                default: break;
                }
                ImGui::TextUnformatted(type_str);

                // Disassembly column — lazy-decoded from cache
                ImGui::TableNextColumn();
                ImGui::Text("%s", disasm_str.c_str());

                // Registers column — from snapshot store
                if(show_registers_) {
                    ImGui::TableNextColumn();
                    if(snap && !show_changes_only_) {
                        const auto& r = snap->registers;
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                            "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X ESP=%08X EBP=%08X",
                            r.eax, r.ebx, r.ecx, r.edx, r.esi, r.edi, r.esp, r.ebp);
                        ImGui::TextUnformatted(buf);
                    }
                    else if(snap && have_prev_regs) {
                        const auto& cur = snap->registers;
                        char buf[256];
                        char* p = buf;

                        auto emit = [&](const char* name, uint32_t oldv, uint32_t newv) {
                            if(oldv == newv) return;
                            p += std::sprintf(p, "%s:%08X->%08X ", name, oldv, newv);
                        };

                        emit("EAX", prev_regs.eax, cur.eax);
                        emit("EBX", prev_regs.ebx, cur.ebx);
                        emit("ECX", prev_regs.ecx, cur.ecx);
                        emit("EDX", prev_regs.edx, cur.edx);
                        emit("ESI", prev_regs.esi, cur.esi);
                        emit("EDI", prev_regs.edi, cur.edi);
                        emit("ESP", prev_regs.esp, cur.esp);
                        emit("EBP", prev_regs.ebp, cur.ebp);
                        emit("EFL", prev_regs.eflags, cur.eflags);

                        if(p == buf) {
                            ImGui::TextUnformatted("-");
                        }
                        else {
                            *p = '\0';
                            ImGui::TextUnformatted(buf);
                        }
                    }
                    if(snap) {
                        prev_regs = snap->registers;
                        have_prev_regs = true;
                    }
                }

                // Instruction bytes column — PR4: not in RawTraceEntry, show opcode byte
                if(show_instruction_bytes_) {
                    ImGui::TableNextColumn();
                    char byte_buf[8];
                    std::snprintf(byte_buf, sizeof(byte_buf), "%02X", raw.opcode_byte);
                    ImGui::TextUnformatted(byte_buf);
                }

                // Memory column — PR4: not tracked in RawTraceEntry for hot path
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("");

                // Frame column
                ImGui::TableNextColumn();
                ImGui::Text("%u", raw.frame);

                // Timestamp column — show packed relative ms
                ImGui::TableNextColumn();
                ImGui::Text("%u", raw.timestamp_packed);

                // Handle selection
                if(ImGui::IsItemClicked()) {
                    selected_entry_index_ = static_cast<int>(start_index + i);
                }

                // Context menu
                if(ImGui::BeginPopupContextItem("TraceLoggerContextMenu")) {
                    if(ImGui::MenuItem("Copy Address")) {
                        char addr_copy[16];
                        std::snprintf(addr_copy, sizeof(addr_copy), "%04X:%04X", raw.cs, raw.ip);
                        ImGui::SetClipboardText(addr_copy);
                    }
                    if(ImGui::MenuItem("Copy Disassembly")) {
                        if(!disasm_str.empty()) {
                            ImGui::SetClipboardText(disasm_str.c_str());
                        }
                    }
                    ImGui::Separator();
                    if(ImGui::MenuItem("Find at Address")) {
                        findAtAddress(raw.address);
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    void TraceLoggerWindow::renderStatusBar() {
        if(!trace_logger_) return;

        const auto& stats = trace_logger_->getStatistics();

        ImGui::Text("Entries: %zu/%zu | Instructions: %u | Unique Addresses: %u | Calls: %u | Jumps: %u | Interrupts: %u",
            trace_logger_->getRingEntryCount(), trace_logger_->getMaxEntries(),
            stats.total_instructions, stats.unique_addresses,
            stats.call_count, stats.jump_count, stats.interrupt_count);

        // PR4: Show dropped count in status bar when > 0
        uint64_t dropped = trace_logger_->getDroppedCount();
        if(dropped > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "| Dropped: %llu",
                static_cast<unsigned long long>(dropped));
        }

        if(trace_logger_->isEnabled()) {
            ImGui::SameLine();
            ImGui::Text("| Status: %s", trace_logger_->isPaused() ? "Paused" : "Running");
        }
    }

    void TraceLoggerWindow::renderFilterDialog() {
        if(ImGui::BeginPopupModal("Filter Options", &show_filter_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
            if(!trace_logger_) {
                ImGui::Text("No trace logger available");
                ImGui::EndPopup();
                return;
            }

            auto& filter = trace_logger_->getFilter();

            ImGui::Checkbox("Enable Filter", &filter.enabled);

            ImGui::Separator();

            // Address range filter
            ImGui::Text("Address Range:");
            ImGui::Text("Start Address:");
            ImGui::InputText("##StartAddress", address_filter_start_, sizeof(address_filter_start_));
            ImGui::Text("End Address:");
            ImGui::InputText("##EndAddress", address_filter_end_, sizeof(address_filter_end_));

            ImGui::Separator();
            ImGui::Text("Range Behavior:");
            ImGui::Checkbox("Auto-start when entering range", &filter.autostart_in_range);
            ImGui::Checkbox("Auto-stop when leaving range", &filter.autostop_at_end);

            ImGui::Separator();

            // Mnemonic filter
            ImGui::Text("Mnemonic Filter:");
            ImGui::InputText("##MnemonicFilter", mnemonic_filter_, sizeof(mnemonic_filter_));

            ImGui::Separator();

            // Instruction type filters
            ImGui::Text("Hide Instruction Types:");
            ImGui::Checkbox("Filter Calls", &filter.filter_calls);
            ImGui::Checkbox("Filter Jumps", &filter.filter_jumps);
            ImGui::Checkbox("Filter Returns", &filter.filter_rets);
            ImGui::Checkbox("Filter Interrupts", &filter.filter_interrupts);

            ImGui::Separator();

            if(ImGui::Button("Apply", ImVec2(120, 0))) {
                applyFilters();
                show_filter_dialog_ = false;
            }

            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_filter_dialog_ = false;
            }

            ImGui::EndPopup();
        }
    }

    void TraceLoggerWindow::renderSearchDialog() {
        if(ImGui::BeginPopupModal("Search Trace", &show_search_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Search for:");
            ImGui::InputText("##SearchInput", search_input_, sizeof(search_input_));

            ImGui::Separator();

            if(ImGui::Button("Find Next", ImVec2(120, 0))) {
                performSearch(search_input_, false);
                show_search_dialog_ = false;
            }

            ImGui::SameLine();
            if(ImGui::Button("Find All", ImVec2(120, 0))) {
                performSearch(search_input_, true);
                show_search_dialog_ = false;
            }

            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_search_dialog_ = false;
            }

            ImGui::EndPopup();
        }
    }

    void TraceLoggerWindow::renderStatisticsDialog() {
        if(ImGui::BeginPopupModal("Trace Statistics", &show_statistics_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
            if(!trace_logger_) {
                ImGui::Text("No trace logger available");
                ImGui::EndPopup();
                return;
            }

            const auto& stats = trace_logger_->getStatistics();

            ImGui::Text("=== Trace Statistics ===");
            ImGui::Text("Total Instructions: %u", stats.total_instructions);
            ImGui::Text("Unique Addresses: %u", stats.unique_addresses);
            ImGui::Text("Call Instructions: %u", stats.call_count);
            ImGui::Text("Jump Instructions: %u", stats.jump_count);
            ImGui::Text("Return Instructions: %u", stats.ret_count);
            ImGui::Text("Interrupt Instructions: %u", stats.interrupt_count);

            ImGui::Separator();

            // Top instructions
            ImGui::Text("Top 10 Instructions:");
            auto instruction_freq = stats.instruction_frequency;
            std::vector<std::pair<std::string, uint32_t>> sorted_instructions(
                instruction_freq.begin(), instruction_freq.end());
            std::sort(sorted_instructions.begin(), sorted_instructions.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });

            for(size_t i = 0; i < std::min(size_t(10), sorted_instructions.size()); ++i) {
                ImGui::Text("  %s: %u", sorted_instructions[i].first.c_str(), sorted_instructions[i].second);
            }

            ImGui::Separator();

            if(ImGui::Button("Close", ImVec2(120, 0))) {
                show_statistics_dialog_ = false;
            }

            ImGui::EndPopup();
        }
    }

    void TraceLoggerWindow::renderExportDialog() {
        // Set initial size for the popup
        if(show_export_dialog_ && !ImGui::IsPopupOpen("Export Trace")) {
            ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);
        }

        if(ImGui::BeginPopupModal("Export Trace", &show_export_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Export Filename:");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##ExportFilename", export_filename_, sizeof(export_filename_));

            ImGui::Text("Export Format:");
            ImGui::SetNextItemWidth(350);
            const char* formats[] = { "Text", "CSV", "XML" };
            ImGui::Combo("##ExportFormat", &export_format_index_, formats, IM_ARRAYSIZE(formats));

            // Display export status message if present
            if(!export_status_message_.empty()) {
                ImGui::Separator();
                if(export_status_is_success_) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", export_status_message_.c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", export_status_message_.c_str());
                }
            }

            ImGui::Separator();

            if(ImGui::Button("Export", ImVec2(120, 0))) {
                performExport();
                // Don't close dialog immediately - let user see the result
            }

            ImGui::SameLine();
            if(ImGui::Button("Close", ImVec2(120, 0))) {
                export_status_message_.clear();  // Clear status when closing
                show_export_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    //=============================================================================
    // Helper Methods
    //=============================================================================

    void TraceLoggerWindow::openFilterDialog() {
        if(trace_logger_) {
            auto& filter = trace_logger_->getFilter();

            // Fill current filter values
            snprintf(address_filter_start_, sizeof(address_filter_start_), "0x%08X", filter.start_address);
            snprintf(address_filter_end_, sizeof(address_filter_end_), "0x%08X", filter.end_address);
            strncpy(mnemonic_filter_, filter.mnemonic_filter.c_str(), sizeof(mnemonic_filter_) - 1);
            mnemonic_filter_[sizeof(mnemonic_filter_) - 1] = '\0';

            show_filter_dialog_ = true;
            // Popup will be opened in render() method
        }
    }

    void TraceLoggerWindow::openSearchDialog() {
        show_search_dialog_ = true;
        // Popup will be opened in render() method
    }

    void TraceLoggerWindow::openStatisticsDialog() {
        show_statistics_dialog_ = true;
        // Popup will be opened in render() method
    }

    void TraceLoggerWindow::openExportDialog() {
        export_status_message_.clear();  // Clear any previous status
        show_export_dialog_ = true;
        // Popup will be opened in render() method
    }

    void TraceLoggerWindow::applyFilters() {
        if(!trace_logger_) return;

        auto& filter = trace_logger_->getFilter();

        // Parse address range
        try {
            if(strlen(address_filter_start_) > 0) {
                filter.start_address = std::stoul(address_filter_start_, nullptr, 16);
            }
            if(strlen(address_filter_end_) > 0) {
                filter.end_address = std::stoul(address_filter_end_, nullptr, 16);
            }
        }
        catch(const std::exception&) {
            // Invalid address format - ignore
        }

        // Set mnemonic filter
        filter.mnemonic_filter = mnemonic_filter_;

        // Filter is already updated by reference
    }

    void TraceLoggerWindow::clearFilters() {
        if(!trace_logger_) return;

        auto& filter = trace_logger_->getFilter();
        filter.enabled = false;
        filter.start_address = 0;
        filter.end_address = 0xFFFFFFFF;
        filter.mnemonic_filter.clear();
        filter.filter_calls = false;
        filter.filter_jumps = false;
        filter.filter_rets = false;
        filter.filter_interrupts = false;
        filter.autostart_in_range = false;
        filter.autostop_at_end = false;
    }

    void TraceLoggerWindow::performSearch(const std::string& query, bool find_all) {
        if(!trace_logger_ || query.empty()) return;

        search_results_.clear();

        // Use direct iteration instead of copying entire trace log
        size_t count = trace_logger_->getEntryCount();

        for(size_t i = 0; i < count; ++i) {
            const auto* entry = trace_logger_->getEntry(i);
            if(!entry) continue;

            // Search in disassembly and mnemonic
            if(entry->disassembly.find(query) != std::string::npos ||
                entry->instruction_mnemonic.find(query) != std::string::npos) {

                search_results_.push_back(i);

                if(!find_all) {
                    // Jump to first result
                    navigateToEntry(i);
                    break;
                }
            }
        }

        if(find_all && !search_results_.empty()) {
            // Navigate to first result
            navigateToEntry(search_results_[0]);
        }
    }

    void TraceLoggerWindow::navigateToEntry(size_t entry_index) {
        if(!trace_logger_) return;

        if(entry_index < trace_logger_->getEntryCount()) {
            current_page_ = entry_index / entries_per_page_;
            selected_entry_index_ = static_cast<int>(entry_index);
        }
    }

    void TraceLoggerWindow::scrollToBottom() {
        if(!trace_logger_) return;

        size_t total_entries = trace_logger_->getEntryCount();
        if(total_entries > 0) {
            size_t total_pages = (total_entries + entries_per_page_ - 1) / entries_per_page_;
            if(total_pages > 0) {
                current_page_ = total_pages - 1;
            }
        }
    }

    void TraceLoggerWindow::clearTrace() {
        if(trace_logger_) {
            trace_logger_->clear();
            current_page_ = 0;
            selected_entry_index_ = -1;
        }
    }

    void TraceLoggerWindow::resetStatistics() {
        if(trace_logger_) {
            trace_logger_->resetStatistics();
        }
    }

    // Helper function to get absolute path from filename
    static std::string getAbsolutePath(const std::string& filename) {
#ifdef _WIN32
        char full_path[_MAX_PATH];
        if(_fullpath(full_path, filename.c_str(), _MAX_PATH) != nullptr) {
            return std::string(full_path);
        }
#else
        char full_path[PATH_MAX];
        if(realpath(filename.c_str(), full_path) != nullptr) {
            return std::string(full_path);
        }
#endif
        // Fallback: return original filename if resolution fails
        return filename;
    }

    void TraceLoggerWindow::performExport() {
        if(!trace_logger_) return;

        // Clear previous status
        export_status_message_.clear();
        export_status_is_success_ = false;

        // Validate filename
        if(strlen(export_filename_) == 0) {
            export_status_message_ = "Error: Please enter a filename";
            export_status_is_success_ = false;
            return;
        }

        // Check if we have entries to export
        if(trace_logger_->getEntryCount() == 0) {
            export_status_message_ = "Error: No trace entries to export";
            export_status_is_success_ = false;
            return;
        }

        std::string format;
        switch(export_format_index_) {
        case 0: format = "txt"; break;
        case 1: format = "csv"; break;
        case 2: format = "xml"; break;
        default: format = "txt"; break;
        }

        // Attempt to export the trace log
        bool success = trace_logger_->exportToFile(export_filename_, format);

        if(success) {
            // Get the absolute path of the saved file
            std::string abs_path = getAbsolutePath(export_filename_);
            export_status_message_ = "Export successful: " + abs_path;
            export_status_is_success_ = true;
        }
        else {
            export_status_message_ = "Export failed: Could not create file (check permissions and path)";
            export_status_is_success_ = false;
        }
    }

    void TraceLoggerWindow::findSimilarInstructions(const std::string& mnemonic) {
        if(!trace_logger_) return;

        auto results = trace_logger_->findEntriesByMnemonic(mnemonic);
        if(!results.empty()) {
            search_results_ = results;
            navigateToEntry(results[0]);
        }
    }

    void TraceLoggerWindow::findAtAddress(uint32_t address) {
        if(!trace_logger_) return;

        auto results = trace_logger_->findEntriesByAddress(address);
        if(!results.empty()) {
            search_results_ = results;
            navigateToEntry(results[0]);
        }
    }

    void TraceLoggerWindow::openAddressSearch() {
        // TODO: Implement address-specific search dialog
        openSearchDialog();
    }

    void TraceLoggerWindow::openMnemonicSearch() {
        // TODO: Implement mnemonic-specific search dialog
        openSearchDialog();
    }

    //=============================================================================
    // Public Interface
    //=============================================================================

    void TraceLoggerWindow::show() {
        show_window_ = true;
    }

    void TraceLoggerWindow::hide() {
        show_window_ = false;
    }

    bool TraceLoggerWindow::isVisible() const {
        return show_window_;
    }

    void TraceLoggerWindow::setAutoScroll(bool enabled) {
        auto_scroll_ = enabled;
    }

    void TraceLoggerWindow::setFollowExecution(bool enabled) {
        follow_execution_ = enabled;
    }

    void TraceLoggerWindow::setShowRegisters(bool enabled) {
        show_registers_ = enabled;
    }

    void TraceLoggerWindow::setShowInstructionBytes(bool enabled) {
        show_instruction_bytes_ = enabled;
    }

    void TraceLoggerWindow::setEntriesPerPage(size_t count) {
        entries_per_page_ = std::max(size_t(10), std::min(size_t(1000), count));
        current_page_ = 0;
    }

    void TraceLoggerWindow::jumpToEntry(size_t index) {
        navigateToEntry(index);
    }

    void TraceLoggerWindow::jumpToAddress(uint32_t address) {
        findAtAddress(address);
    }

    void TraceLoggerWindow::searchForMnemonic(const std::string& mnemonic) {
        findSimilarInstructions(mnemonic);
    }
} // namespace LuaEngineTraceLogger
