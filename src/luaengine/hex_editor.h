#ifndef HEX_EDITOR_H
#define HEX_EDITOR_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <chrono>
#include "lua_memory_domains.h"

namespace LuaEngineHexEditor {

// Forward declarations
class HexEditor;
using MemoryDomainManager = LuaEngineMemoryDomains::MemoryDomainManager;

// Display modes for the hex editor
enum class HexDisplayMode {
    HEX_ONLY,           // Just hexadecimal values
    ASCII_SIDEBAR,      // Hex with ASCII sidebar
    SJIS_SIDEBAR,       // Hex with SJIS text sidebar
    UTF8_SIDEBAR,       // Hex with UTF-8 text sidebar
    MIXED_VIEW          // Alternating hex/text view
};

// Edit modes
enum class HexEditMode {
    READ_ONLY,          // No editing allowed
    HEX_EDIT,           // Edit hexadecimal values
    TEXT_EDIT,          // Edit as text with encoding
    OVERWRITE,          // Overwrite mode
    INSERT              // Insert mode
};

// Data grouping options
enum class HexGrouping {
    BYTE,               // Individual bytes
    WORD,               // 16-bit words
    DWORD,              // 32-bit double words
    QWORD               // 64-bit quad words
};

// Selection state
struct HexSelection {
    uint32_t start_address;
    uint32_t end_address;
    bool active;
    std::string domain_name;
    
    HexSelection() : start_address(0), end_address(0), active(false) {}
    
    size_t getSize() const {
        return active ? (end_address - start_address + 1) : 0;
    }
    
    bool contains(uint32_t address) const {
        return active && address >= start_address && address <= end_address;
    }
};

// Bookmark for quick navigation
struct HexBookmark {
    uint32_t address;
    std::string domain_name;
    std::string label;
    std::string notes;
    uint16_t color_id;
    
    HexBookmark(uint32_t addr = 0, const std::string& domain = "", 
               const std::string& lbl = "", const std::string& note = "")
        : address(addr), domain_name(domain), label(lbl), notes(note), color_id(0) {}
};

// Edit history for undo/redo
struct HexEditOperation {
    uint32_t address;
    std::string domain_name;
    std::vector<uint8_t> old_data;
    std::vector<uint8_t> new_data;
    std::chrono::steady_clock::time_point timestamp;
    
    HexEditOperation(uint32_t addr, const std::string& domain)
        : address(addr), domain_name(domain), timestamp(std::chrono::steady_clock::now()) {}
};

// Find/Replace functionality
struct HexSearchOptions {
    std::string pattern;
    bool hex_pattern;           // true = hex pattern, false = text pattern
    bool case_sensitive;
    bool wrap_around;
    uint16_t codepage;          // For text searches
    std::string domain_filter;  // Empty = all domains
    
    HexSearchOptions() : hex_pattern(true), case_sensitive(false), 
                        wrap_around(true), codepage(932) {}
};

// Color scheme for syntax highlighting
struct HexColorScheme {
    uint32_t background_color;
    uint32_t text_color;
    uint32_t address_color;
    uint32_t hex_color;
    uint32_t ascii_color;
    uint32_t selection_color;
    uint32_t bookmark_color;
    uint32_t modified_color;
    uint32_t null_byte_color;
    uint32_t printable_color;
    
    HexColorScheme() : background_color(0xFF000000), text_color(0xFFFFFFFF),
                      address_color(0xFF00FFFF), hex_color(0xFFFFFF00),
                      ascii_color(0xFF00FF00), selection_color(0xFF0080FF),
                      bookmark_color(0xFFFF8000), modified_color(0xFFFF0000),
                      null_byte_color(0xFF808080), printable_color(0xFF00FF00) {}
};

// Configuration for hex editor
struct HexEditorConfig {
    HexDisplayMode display_mode;
    HexEditMode edit_mode;
    HexGrouping grouping;
    
    // Display options
    size_t bytes_per_row;
    bool show_addresses;
    bool show_ascii;
    bool uppercase_hex;
    bool zero_pad_addresses;
    
    // Text encoding
    uint16_t default_codepage;
    bool auto_detect_encoding;
    
    // Performance options
    size_t cache_size;
    bool lazy_loading;
    size_t max_undo_operations;
    
    // Visual options
    HexColorScheme colors;
    bool highlight_changes;
    bool show_bookmarks;
    
    HexEditorConfig() : display_mode(HexDisplayMode::ASCII_SIDEBAR),
                       edit_mode(HexEditMode::HEX_EDIT),
                       grouping(HexGrouping::BYTE),
                       bytes_per_row(16), show_addresses(true),
                       show_ascii(true), uppercase_hex(true),
                       zero_pad_addresses(true), default_codepage(932),
                       auto_detect_encoding(false), cache_size(1024*1024),
                       lazy_loading(true), max_undo_operations(100),
                       highlight_changes(true), show_bookmarks(true) {}
};

// Main hex editor class
class HexEditor {
private:
    // Core components
    MemoryDomainManager* memory_manager_;
    HexEditorConfig config_;
    
    // Current view state
    uint32_t current_address_;
    std::string current_domain_;
    size_t viewport_rows_;
    size_t viewport_cols_;
    
    // Selection and cursor
    HexSelection selection_;
    uint32_t cursor_address_;
    bool cursor_on_hex_;    // true = hex side, false = text side
    
    // Bookmarks and navigation
    std::vector<HexBookmark> bookmarks_;
    std::vector<uint32_t> navigation_history_;
    size_t history_position_;
    
    // Edit operations and undo
    std::vector<HexEditOperation> undo_stack_;
    std::vector<HexEditOperation> redo_stack_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> modified_data_;
    
    // Search functionality
    HexSearchOptions search_options_;
    std::vector<uint32_t> search_results_;
    size_t current_search_result_;
    
    // Caching for performance
    struct CacheEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::map<uint32_t, CacheEntry> data_cache_;
    std::mutex cache_mutex_;
    
    // Threading for large operations
    std::atomic<bool> operation_cancelled_;
    
public:
    HexEditor();
    ~HexEditor();
    
    // Initialization
    bool initialize(MemoryDomainManager* memory_mgr);
    void shutdown();
    
    // Configuration
    void setConfig(const HexEditorConfig& config) { config_ = config; }
    const HexEditorConfig& getConfig() const { return config_; }
    
    // Navigation
    void gotoAddress(uint32_t address, const std::string& domain = "");
    void gotoBookmark(size_t bookmark_index);
    void navigateUp(size_t rows = 1);
    void navigateDown(size_t rows = 1);
    void navigateLeft();
    void navigateRight();
    void pageUp();
    void pageDown();
    
    // Navigation history
    void goBack();
    void goForward();
    void addToHistory(uint32_t address);
    
    // Display and viewport
    void setViewport(size_t rows, size_t cols);
    std::vector<std::string> renderView();
    std::string renderAddressColumn(uint32_t address);
    std::string renderHexColumn(uint32_t address, size_t bytes);
    std::string renderTextColumn(uint32_t address, size_t bytes, uint16_t codepage = 932);
    
    // Selection
    void startSelection(uint32_t address);
    void extendSelection(uint32_t address);
    void clearSelection();
    void selectAll();
    void selectRange(uint32_t start, uint32_t end);
    const HexSelection& getSelection() const { return selection_; }
    
    // Data access and editing
    uint8_t readByte(uint32_t address);
    std::vector<uint8_t> readBytes(uint32_t address, size_t count);
    std::string readText(uint32_t address, size_t count, uint16_t codepage = 932);
    
    void writeByte(uint32_t address, uint8_t value);
    void writeBytes(uint32_t address, const std::vector<uint8_t>& data);
    void writeText(uint32_t address, const std::string& text, uint16_t codepage = 932);
    
    // Clipboard operations
    void copySelection();
    void cutSelection();
    void paste();
    void pasteSpecial(bool as_hex, uint16_t codepage = 932);
    
    // Find and replace
    bool findNext(const HexSearchOptions& options);
    bool findPrevious(const HexSearchOptions& options);
    size_t findAll(const HexSearchOptions& options);
    size_t replaceAll(const HexSearchOptions& find_options, 
                     const std::string& replace_pattern, 
                     bool replace_as_hex = true);
    
    // Bookmarks
    size_t addBookmark(uint32_t address, const std::string& label = "", 
                       const std::string& notes = "");
    void removeBookmark(size_t index);
    void removeBookmarkByAddress(uint32_t address);
    HexBookmark* getBookmark(size_t index);
    const std::vector<HexBookmark>& getBookmarks() const { return bookmarks_; }
    
    // Undo/Redo
    bool canUndo() const { return !undo_stack_.empty(); }
    bool canRedo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();
    void clearUndoHistory();
    
    // Import/Export
    bool exportSelection(const std::string& filename, const std::string& format = "bin");
    bool importFile(const std::string& filename, uint32_t address, bool insert = false);
    bool exportToIntelHex(const std::string& filename);
    bool importFromIntelHex(const std::string& filename);
    
    // Analysis tools
    std::map<uint8_t, size_t> getByteFrequency(uint32_t start, uint32_t end);
    std::vector<uint32_t> findPattern(const std::vector<uint8_t>& pattern);
    std::vector<uint32_t> findTextPattern(const std::string& text, uint16_t codepage = 932);
    
    // File operations
    bool saveToFile(const std::string& filename) const;
    bool loadFromFile(const std::string& filename);
    bool saveBookmarks(const std::string& filename) const;
    bool loadBookmarks(const std::string& filename);
    
    // Properties
    uint32_t getCurrentAddress() const { return current_address_; }
    const std::string& getCurrentDomain() const { return current_domain_; }
    uint32_t getCursorAddress() const { return cursor_address_; }
    bool isCursorOnHex() const { return cursor_on_hex_; }
    
    // Status information
    size_t getModifiedDataSize() const { return modified_data_.size(); }
    bool hasUnsavedChanges() const { return !modified_data_.empty(); }
    std::string getStatusText() const;
    
    // Encoding utilities
    std::vector<std::string> getAvailableCodepages() const;
    std::string getCodepageName(uint16_t codepage) const;
    bool isTextPrintable(const std::string& text) const;
    
    // Event callbacks
    std::function<void(uint32_t address)> onAddressChanged;
    std::function<void(const HexSelection&)> onSelectionChanged;
    std::function<void(uint32_t address, uint8_t old_value, uint8_t new_value)> onDataChanged;
    std::function<void(const std::string&)> onStatusChanged;

private:
    // Internal helpers
    bool isValidAddress(uint32_t address) const;
    void updateCursor(uint32_t address, bool on_hex = true);
    void pushUndoOperation(const HexEditOperation& operation);
    void invalidateCache(uint32_t address, size_t size = 1);
    
    // Data caching
    std::vector<uint8_t> getCachedData(uint32_t address, size_t size);
    void cacheData(uint32_t address, const std::vector<uint8_t>& data);
    void clearCache();
    
    // Search helpers
    std::vector<uint32_t> searchHexPattern(const std::string& hex_pattern);
    std::vector<uint32_t> searchTextPattern(const std::string& text, uint16_t codepage);
    bool matchesPattern(uint32_t address, const std::vector<uint8_t>& pattern);
    
    // Rendering helpers
    std::string formatAddress(uint32_t address) const;
    std::string formatHexByte(uint8_t value) const;
    std::string formatHexWord(uint16_t value) const;
    std::string formatHexDWord(uint32_t value) const;
    char formatAsciiChar(uint8_t value) const;
    
    // Text encoding helpers
    std::string convertBytesToText(const std::vector<uint8_t>& bytes, uint16_t codepage) const;
    std::vector<uint8_t> convertTextToBytes(const std::string& text, uint16_t codepage) const;
    bool isValidSJISSequence(const uint8_t* data, size_t length) const;
    
    // Clipboard helpers
    std::string formatForClipboard(const std::vector<uint8_t>& data, bool as_hex) const;
    std::vector<uint8_t> parseFromClipboard(const std::string& text, bool as_hex) const;
    
    // File format helpers
    bool exportAsBinary(const std::string& filename) const;
    bool exportAsHexDump(const std::string& filename) const;
    bool exportAsC(const std::string& filename) const;
    
    // Performance optimization
    void optimizeForLargeFiles();
    void preloadData(uint32_t start_address, size_t size);
};

// Utility functions for hex editor integration
namespace HexUtils {
    std::string bytesToHexString(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> hexStringToBytes(const std::string& hex);
    bool isValidHexString(const std::string& hex);
    std::string formatFileSize(size_t size);
    std::string formatTimestamp(const std::chrono::steady_clock::time_point& time);
    
    // Boyer-Moore search algorithm for efficient pattern matching
    std::vector<uint32_t> boyerMooreSearch(const uint8_t* haystack, size_t haystack_len,
                                          const uint8_t* needle, size_t needle_len);
}

} // namespace LuaEngineHexEditor

#endif // HEX_EDITOR_H