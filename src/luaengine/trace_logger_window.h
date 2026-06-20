#ifndef TRACE_LOGGER_WINDOW_H
#define TRACE_LOGGER_WINDOW_H

#include "trace_logger.h"
#include <functional>

namespace LuaEngineSymbols { class SymbolManager; }

namespace LuaEngineTraceLogger {

    class TraceLoggerWindow {
    private:
        // Core components
        TraceLogger* trace_logger_;
        LuaEngineSymbols::SymbolManager* symbol_manager_;

        // UI state
        bool show_window_;
        bool show_filter_dialog_;
        bool show_search_dialog_;
        bool show_statistics_dialog_;
        bool show_export_dialog_;

        // Display settings
        size_t current_page_;
        size_t entries_per_page_;
        int selected_entry_index_;
        bool auto_scroll_;
        bool follow_execution_;
        bool show_registers_;
        bool show_instruction_bytes_;
        bool show_changes_only_;

        // Highlighting options
        bool highlight_calls_;
        bool highlight_jumps_;
        bool highlight_interrupts_;

        // Filter state
        bool address_filter_enabled_;
        bool mnemonic_filter_enabled_;
        char address_filter_start_[32];
        char address_filter_end_[32];
        char mnemonic_filter_[64];

        // Search state
        char search_input_[256];
        std::vector<size_t> search_results_;
        size_t current_search_index_;

        // Export state
        char export_filename_[256];
        int export_format_index_;
        std::string export_status_message_;
        bool export_status_is_success_;

        // Column widths
        float column_widths_[6];

        // UI rendering methods
        void renderMenuBar();
        void renderToolbar();
        void renderTraceTable();
        void renderStatusBar();
        void renderFilterDialog();
        void renderSearchDialog();
        void renderStatisticsDialog();
        void renderExportDialog();

        // Dialog management
        void openFilterDialog();
        void openSearchDialog();
        void openStatisticsDialog();
        void openExportDialog();

        // Filter operations
        void applyFilters();
        void clearFilters();

        // Search operations
        void performSearch(const std::string& query, bool find_all);
        void findSimilarInstructions(const std::string& mnemonic);
        void findAtAddress(uint32_t address);
        void openAddressSearch();
        void openMnemonicSearch();

        // Navigation
        void navigateToEntry(size_t entry_index);
        void scrollToBottom();

        // Utility methods
        void clearTrace();
        void resetStatistics();
        void performExport();

    public:
        TraceLoggerWindow();
        ~TraceLoggerWindow();

        // Initialization
        void initialize(TraceLogger* trace_logger, LuaEngineSymbols::SymbolManager* symbol_manager = nullptr);

        // Main render method
        void render();

        // Window control
        void show();
        void hide();
        bool isVisible() const;

        // Display settings
        void setAutoScroll(bool enabled);
        void setFollowExecution(bool enabled);
        void setShowRegisters(bool enabled);
        void setShowInstructionBytes(bool enabled);
        void setEntriesPerPage(size_t count);

        // Navigation methods
        void jumpToEntry(size_t index);
        void jumpToAddress(uint32_t address);
        void searchForMnemonic(const std::string& mnemonic);

        // Accessors
        TraceLogger* getTraceLogger() const { return trace_logger_; }
        size_t getCurrentPage() const { return current_page_; }
        size_t getEntriesPerPage() const { return entries_per_page_; }
        int getSelectedEntryIndex() const { return selected_entry_index_; }

        // Callbacks for integration
        std::function<void(uint32_t address)> onAddressSelected;
        std::function<void(const std::string& mnemonic)> onMnemonicSelected;
        std::function<void(const TraceEntry& entry)> onEntrySelected;
    };

} // namespace LuaEngineTraceLogger

#endif // TRACE_LOGGER_WINDOW_H
