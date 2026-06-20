#ifndef HEX_EDITOR_WINDOW_H
#define HEX_EDITOR_WINDOW_H

#include <string>
#include <vector>
#include <set>
#include <functional>
#include <cstdint>
#include "lua_memory_domains.h"
#include "ram_search_engine.h"  // For WatchSize

namespace LuaEngineHexEditor {

// Text encoding types
enum class TextEncoding {
    ASCII,
    UTF8,
    CP932,      // Japanese Shift-JIS
    CP437,      // Original IBM PC
    CP850,      // Western European
    CP866,      // Russian
    LATIN1      // ISO-8859-1
};

// Selection structure
struct MemorySelection {
    uint32_t start_address;
    uint32_t end_address;
    bool active;
    
    MemorySelection() : start_address(0), end_address(0), active(false) {}
    MemorySelection(uint32_t start, uint32_t end) : start_address(start), end_address(end), active(true) {}
    
    bool contains(uint32_t address) const {
        return active && address >= start_address && address <= end_address;
    }
    
    uint32_t size() const {
        return active ? (end_address - start_address + 1) : 0;
    }
};

// Bookmark structure
struct MemoryBookmark {
    uint32_t address;
    std::string name;
    std::string description;
    
    MemoryBookmark(uint32_t addr, const std::string& n, const std::string& desc = "")
        : address(addr), name(n), description(desc) {}
};

// Find/Replace structure
struct FindReplaceData {
    std::vector<uint8_t> find_pattern;
    std::vector<uint8_t> replace_pattern;
    bool case_sensitive;
    bool whole_bytes;
    LuaEngineRamSearch::WatchSize data_size;
    
    FindReplaceData() : case_sensitive(false), whole_bytes(true), data_size(LuaEngineRamSearch::WatchSize::BYTE_1) {}
};

// Hex editor window class
class HexEditorWindow {
private:
    // Core components
    LuaEngineMemoryDomains::MemoryDomainManager* memory_manager_;
    
    // Display settings
    std::string current_domain_;
    uint32_t base_address_;
    uint32_t bytes_per_row_;
    uint32_t visible_rows_;
    TextEncoding text_encoding_;
    bool show_ascii_;
    bool show_addresses_;
    bool show_ruler_;
    
    // Window state
    bool show_window_;
    bool show_goto_dialog_;
    bool show_find_dialog_;
    bool show_bookmark_dialog_;
    bool show_export_dialog_;
    
    // Navigation
    uint32_t current_address_;
    uint32_t scroll_address_;
    
    // Selection and editing
    MemorySelection selection_;
    int editing_address_;
    int editing_nibble_;    // 0 or 1 for hex editing
    char edit_buffer_[3];   // For hex input
    bool edit_mode_ascii_;  // True if editing ASCII, false for hex
    
    // Frozen/watched addresses
    std::set<uint32_t> frozen_addresses_;
    std::set<uint32_t> watched_addresses_;
    
    // Bookmarks
    std::vector<MemoryBookmark> bookmarks_;
    
    // Find/Replace
    FindReplaceData find_replace_;
    std::vector<uint32_t> find_results_;
    size_t current_find_index_;
    
    // Dialog input buffers
    char goto_input_[32];
    char find_input_[256];
    char replace_input_[256];
    char bookmark_name_input_[64];
    char bookmark_desc_input_[256];
    char export_filename_input_[256];
    
    // Clipboard
    std::vector<uint8_t> clipboard_data_;
    
    // Encoding support
    std::string decodeText(const uint8_t* data, size_t length, TextEncoding encoding);
    std::string cp932ToUtf8(const uint8_t* data, size_t length);
    std::string cp437ToUtf8(const uint8_t* data, size_t length);
    std::string cp850ToUtf8(const uint8_t* data, size_t length);
    std::string cp866ToUtf8(const uint8_t* data, size_t length);
    bool isValidCP932Lead(uint8_t byte);
    bool isValidCP932Trail(uint8_t byte);
    
    // Rendering methods
    void renderMenuBar();
    void renderToolbar();
    void renderAddressRuler();
    void renderHexView();
    void renderHexRow(uint32_t address, int row);
    void renderAddressColumn(uint32_t address);
    void renderHexColumn(uint32_t address);
    void renderTextColumn(uint32_t address);
    void renderDataInspector();
    void renderStatusBar();
    
    // Dialog rendering
    void renderGotoDialog();
    void renderFindDialog();
    void renderBookmarkDialog();
    void renderExportDialog();
    
    // Input handling
    void handleHexInput(char c, uint32_t address);
    void handleAsciiInput(char c, uint32_t address);
    void handleKeyboardInput();
    void handleMouseInput();
    
    // Navigation
    void gotoAddress(uint32_t address);
    void scrollToAddress(uint32_t address);
    uint32_t getAddressFromPosition(int x, int y);
    
    // Selection
    void startSelection(uint32_t address);
    void updateSelection(uint32_t address);
    void clearSelection();
    void selectAll();
    
    // Editing operations
    void writeByteAt(uint32_t address, uint8_t value);
    void insertBytes(uint32_t address, const std::vector<uint8_t>& data);
    void deleteBytes(uint32_t address, uint32_t count);
    void copySelection();
    void pasteAtCursor();
    
    // Search operations
    void findNext();
    void findPrevious();
    void replaceNext();
    void replaceAll();
    std::vector<uint32_t> searchMemory(const std::vector<uint8_t>& pattern);
    
    // Bookmarks
    void addBookmark(uint32_t address, const std::string& name, const std::string& description = "");
    void removeBookmark(uint32_t address);
    void gotoBookmark(uint32_t address);
    
    // File operations
    void exportSelection(const std::string& filename);
    void importFile(const std::string& filename, uint32_t address);
    
    // Utility methods
    uint32_t parseAddress(const std::string& input);
    std::vector<uint8_t> parseHexString(const std::string& input);
    std::string formatHexByte(uint8_t value);
    std::string formatAddress(uint32_t address);
    bool isAddressVisible(uint32_t address);
    
    // Color helpers
    uint32_t getByteColor(uint32_t address);
    uint32_t getTextColor(uint32_t address);
    
public:
    HexEditorWindow();
    ~HexEditorWindow();
    
    // Initialization
    void initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr);
    
    // Main render method
    void render();
    
    // Window control
    void show();
    void hide();
    bool isVisible() const;
    
    // Navigation
    void gotoAddress(uint32_t address, const std::string& domain);
    void setDomain(const std::string& domain);
    void setBytesPerRow(uint32_t bytes);
    void setBytesPerLine(int bytes_per_line);  // Called by DebugToolsManager
    void setTextEncoding(TextEncoding encoding);
    
    // Settings
    void setShowAscii(bool show) { show_ascii_ = show; }
    void setShowAddresses(bool show) { show_addresses_ = show; }
    void setShowRuler(bool show) { show_ruler_ = show; }
    
    // Frozen/watched addresses
    void addFrozenAddress(uint32_t address) { frozen_addresses_.insert(address); }
    void removeFrozenAddress(uint32_t address) { frozen_addresses_.erase(address); }
    void addWatchedAddress(uint32_t address) { watched_addresses_.insert(address); }
    void removeWatchedAddress(uint32_t address) { watched_addresses_.erase(address); }
    
    // Getters
    const std::string& getCurrentDomain() const { return current_domain_; }
    uint32_t getCurrentAddress() const { return current_address_; }
    uint32_t getBytesPerRow() const { return bytes_per_row_; }
    TextEncoding getTextEncoding() const { return text_encoding_; }
    
    // Callbacks for integration
    std::function<void(uint32_t address, const std::string& domain)> onAddToWatchCallback;
    std::function<void(uint32_t address, uint8_t value, const std::string& domain)> onFreezeValueCallback;
    std::function<void(uint32_t address, const std::string& domain)> onUnfreezeValueCallback;
};

} // namespace LuaEngineHexEditor

#endif // HEX_EDITOR_WINDOW_H