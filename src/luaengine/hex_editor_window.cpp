#include "hex_editor_window.h"
#include "debug_utils.h"
#include "symbol_manager.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <bitset>

// Using declarations for memory domain and symbol manager functions
using LuaEngineMemoryDomains::GetGlobalMemoryDomainManager;
using LuaEngineMemoryDomains::MemoryDomainManager;
using LuaEngineSymbols::g_symbol_manager;

namespace LuaEngineHexEditor {

void HexEditorWindow::setBytesPerLine(int bytes_per_line) {
    if (bytes_per_line > 0) {
        bytes_per_row_ = static_cast<uint32_t>(bytes_per_line);
    }
}

//=============================================================================
// Color Constants
//=============================================================================

static const uint32_t COLOR_NORMAL = IM_COL32(255, 255, 255, 255);
static const uint32_t COLOR_SELECTED = IM_COL32(100, 150, 255, 255);
static const uint32_t COLOR_FROZEN = IM_COL32(100, 255, 100, 255);
static const uint32_t COLOR_WATCHED = IM_COL32(255, 255, 100, 255);
static const uint32_t COLOR_CHANGED = IM_COL32(255, 100, 100, 255);
static const uint32_t COLOR_EDITING = IM_COL32(255, 200, 100, 255);

//=============================================================================
// HexEditorWindow Implementation
//=============================================================================

HexEditorWindow::HexEditorWindow()
    : memory_manager_(nullptr), current_domain_("DOS Conventional"),
      base_address_(0), bytes_per_row_(16), visible_rows_(24),
      text_encoding_(TextEncoding::ASCII), show_ascii_(true),
      show_addresses_(true), show_ruler_(true),
      show_window_(true), show_goto_dialog_(false), show_find_dialog_(false),
      show_bookmark_dialog_(false), show_export_dialog_(false),
      current_address_(0), scroll_address_(0),
      editing_address_(-1), editing_nibble_(0), edit_mode_ascii_(false),
      current_find_index_(0) {
    
    // Initialize input buffers
    goto_input_[0] = '\0';
    find_input_[0] = '\0';
    replace_input_[0] = '\0';
    bookmark_name_input_[0] = '\0';
    bookmark_desc_input_[0] = '\0';
    export_filename_input_[0] = '\0';
    edit_buffer_[0] = '\0';
}

HexEditorWindow::~HexEditorWindow() {
}

void HexEditorWindow::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    memory_manager_ = memory_mgr;
    if (!memory_manager_) {
        // Auto-initialize with global manager if none provided
        memory_manager_ = GetGlobalMemoryDomainManager();
    }

    // Pick a sensible default domain if available.
    if (memory_manager_) {
        auto domain_names = memory_manager_->getDomainNames();
        if (!domain_names.empty()) {
            const std::string preferred = "DOS Conventional";
            auto it = std::find(domain_names.begin(), domain_names.end(), preferred);
            current_domain_ = (it != domain_names.end()) ? *it : domain_names.front();
        } else {
            current_domain_.clear();
        }
    } else {
        current_domain_.clear();
    }
}

void HexEditorWindow::render() {
    if (!show_window_) return;
    
    // Auto-initialize memory manager if not set
    if (!memory_manager_) {
        memory_manager_ = GetGlobalMemoryDomainManager();
    }
    
    if (ImGui::Begin("Hex Editor", &show_window_, ImGuiWindowFlags_MenuBar)) {
        // Check if memory manager is available
        if (!memory_manager_) {
            ImGui::TextUnformatted("ERROR: Memory manager not initialized");
            ImGui::End();
            return;
        }

        auto domain_names = memory_manager_->getDomainNames();
        if (domain_names.empty()) {
            ImGui::TextUnformatted("ERROR: No memory domains are available in this build.");
            ImGui::End();
            return;
        }

        // Initialize or repair the current domain selection.
        if (current_domain_.empty()) {
            const std::string preferred = "DOS Conventional";
            auto it = std::find(domain_names.begin(), domain_names.end(), preferred);
            current_domain_ = (it != domain_names.end()) ? *it : domain_names.front();
        }

        auto* domain = memory_manager_->getDomain(current_domain_);
        if (!domain) {
            ImGui::Text("ERROR: Memory domain '%s' not found", current_domain_.c_str());
            ImGui::TextUnformatted("Available domains:");
            for (const auto& name : domain_names) {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::End();
            return;
        }
        
        renderMenuBar();
        renderToolbar();
        
        if (show_ruler_) {
            renderAddressRuler();
        }
        
        // Create a horizontal layout for hex view and data inspector
        ImGui::Columns(2, "HexEditorLayout", true);
        
        // First column: Hex view (takes ~2/3 of available space)
        float content_width = ImGui::GetContentRegionAvail().x;
        ImGui::SetColumnWidth(0, content_width * 0.67f);
        renderHexView();
        ImGui::NextColumn();
        
        // Second column: Data inspector (takes remaining space)
        renderDataInspector();
        
        ImGui::Columns(1); // Reset to single column layout
        renderStatusBar();
    }
    ImGui::End();
    
    // Render dialogs
    if (show_goto_dialog_) {
        renderGotoDialog();
    }
    
    if (show_find_dialog_) {
        renderFindDialog();
    }
    
    if (show_bookmark_dialog_) {
        renderBookmarkDialog();
    }
    
    if (show_export_dialog_) {
        renderExportDialog();
    }
}

void HexEditorWindow::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Export Selection...", "Ctrl+E")) {
                show_export_dialog_ = true;
                ImGui::OpenPopup("Export Data");
            }
            if (ImGui::MenuItem("Import File...", "Ctrl+I")) {
                // TODO: File dialog
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                copySelection();
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                pasteAtCursor();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                selectAll();
            }
            if (ImGui::MenuItem("Clear Selection", "Escape")) {
                clearSelection();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Search")) {
            if (ImGui::MenuItem("Find...", "Ctrl+F")) {
                show_find_dialog_ = true;
                ImGui::OpenPopup("Find");
            }
            if (ImGui::MenuItem("Find Next", "F3")) {
                findNext();
            }
            if (ImGui::MenuItem("Find Previous", "Shift+F3")) {
                findPrevious();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Navigate")) {
            if (ImGui::MenuItem("Go to Address...", "Ctrl+G")) {
                show_goto_dialog_ = true;
                ImGui::OpenPopup("Go to Address");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Bookmark...", "Ctrl+B")) {
                show_bookmark_dialog_ = true;
                ImGui::OpenPopup("Add Bookmark");
            }
            if (ImGui::BeginMenu("Bookmarks")) {
                for (const auto& bookmark : bookmarks_) {
                    if (ImGui::MenuItem(bookmark.name.c_str())) {
                        gotoBookmark(bookmark.address);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Show ASCII", nullptr, show_ascii_)) {
                show_ascii_ = !show_ascii_;
            }
            if (ImGui::MenuItem("Show Addresses", nullptr, show_addresses_)) {
                show_addresses_ = !show_addresses_;
            }
            if (ImGui::MenuItem("Show Ruler", nullptr, show_ruler_)) {
                show_ruler_ = !show_ruler_;
            }
            ImGui::Separator();
            
            // Bytes per row
            if (ImGui::BeginMenu("Bytes per Row")) {
                if (ImGui::MenuItem("8", nullptr, bytes_per_row_ == 8)) setBytesPerRow(8);
                if (ImGui::MenuItem("16", nullptr, bytes_per_row_ == 16)) setBytesPerRow(16);
                if (ImGui::MenuItem("32", nullptr, bytes_per_row_ == 32)) setBytesPerRow(32);
                ImGui::EndMenu();
            }
            
            // Text encoding
            if (ImGui::BeginMenu("Text Encoding")) {
                if (ImGui::MenuItem("ASCII", nullptr, text_encoding_ == TextEncoding::ASCII)) {
                    setTextEncoding(TextEncoding::ASCII);
                }
                if (ImGui::MenuItem("CP932 (Shift-JIS)", nullptr, text_encoding_ == TextEncoding::CP932)) {
                    setTextEncoding(TextEncoding::CP932);
                }
                if (ImGui::MenuItem("CP437 (IBM PC)", nullptr, text_encoding_ == TextEncoding::CP437)) {
                    setTextEncoding(TextEncoding::CP437);
                }
                if (ImGui::MenuItem("CP850 (Western)", nullptr, text_encoding_ == TextEncoding::CP850)) {
                    setTextEncoding(TextEncoding::CP850);
                }
                if (ImGui::MenuItem("CP866 (Russian)", nullptr, text_encoding_ == TextEncoding::CP866)) {
                    setTextEncoding(TextEncoding::CP866);
                }
                if (ImGui::MenuItem("Latin-1", nullptr, text_encoding_ == TextEncoding::LATIN1)) {
                    setTextEncoding(TextEncoding::LATIN1);
                }
                ImGui::EndMenu();
            }
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
}

void HexEditorWindow::renderToolbar() {
    // Domain selection
    ImGui::Text("Domain:");
    ImGui::SameLine();
    
    if (memory_manager_) {
        auto domains = memory_manager_->getDomainNames();
        if (ImGui::BeginCombo("##Domain", current_domain_.c_str())) {
            for (const auto& domain : domains) {
                bool is_selected = (current_domain_ == domain);
                if (ImGui::Selectable(domain.c_str(), is_selected)) {
                    setDomain(domain);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    
    ImGui::SameLine();
    ImGui::Text("Address:");
    ImGui::SameLine();
    std::string current_addr_str = formatAddress(current_address_);
    ImGui::Text("%s", current_addr_str.c_str());
    
    ImGui::SameLine();
    if (ImGui::Button("Go To...")) {
        show_goto_dialog_ = true;
        ImGui::OpenPopup("Go to Address");
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Find...")) {
        show_find_dialog_ = true;
        ImGui::OpenPopup("Find");
    }
}

void HexEditorWindow::renderAddressRuler() {
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Monospace font
    
    std::string ruler;
    if (show_addresses_) {
        ruler += "Address   ";
    }
    
    // Hex column headers
    for (uint32_t i = 0; i < bytes_per_row_; ++i) {
        ruler += formatHexByte(i) + " ";
    }
    
    if (show_ascii_) {
        ruler += " ASCII";
    }
    
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", ruler.c_str());
    ImGui::Separator();
    
    ImGui::PopFont();
}

void HexEditorWindow::renderHexView() {
    if (!memory_manager_) {
        ImGui::Text("Memory manager not available");
        return;
    }
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) {
        ImGui::Text("Memory domain '%s' not found", current_domain_.c_str());
        return;
    }
    
    // Check font availability before using
    bool use_font = ImGui::GetIO().Fonts->Fonts.Size > 0;
    if (use_font) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Monospace font
    }
    
    // Calculate visible range
    uint32_t start_address = scroll_address_;
    uint32_t end_address = start_address + (visible_rows_ * bytes_per_row_);
    
    // Use ImGui clipper for efficient rendering of large memory ranges
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_rows_));
    
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            uint32_t row_address = start_address + (row * bytes_per_row_);
            
            // Check if we're within the domain bounds
            if (row_address >= domain->getBaseAddress() + domain->getSize()) {
                break;
            }
            
            // Render this row using the proper rendering function
            renderHexRow(row_address, row);
        }
    }
    
    if (use_font) {
        ImGui::PopFont();
    }
}

void HexEditorWindow::renderHexRow(uint32_t address, int row) {
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    ImGui::PushID(row);
    
    // Address column
    if (show_addresses_) {
        renderAddressColumn(address);
        ImGui::SameLine();
    }
    
    // Hex columns
    renderHexColumn(address);
    
    // ASCII column
    if (show_ascii_) {
        ImGui::SameLine();
        renderTextColumn(address);
    }
    
    ImGui::PopID();
}

void HexEditorWindow::renderAddressColumn(uint32_t address) {
    std::string addr_str = formatAddress(address);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", addr_str.c_str());
}

void HexEditorWindow::renderHexColumn(uint32_t address) {
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    for (uint32_t i = 0; i < bytes_per_row_; ++i) {
        uint32_t byte_address = address + i;
        
        // Ensure each byte cell has a unique ID
        ImGui::PushID(static_cast<int>(i));
        
        if (i > 0) ImGui::SameLine();
        
        // Check if address is valid
        if (!domain->isAddressValid(byte_address)) {
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "XX");
        } else {
            // Read byte
            uint8_t value = domain->readByte(byte_address);
            
            // Determine color
            uint32_t color = getByteColor(byte_address);
            
            // Check if editing this byte
            bool is_selected = false;
            if (static_cast<int>(byte_address) == editing_address_ && !edit_mode_ascii_) {
                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_EDITING);
                is_selected = ImGui::Selectable(edit_buffer_, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                is_selected = ImGui::Selectable(formatHexByte(value).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0));
                ImGui::PopStyleColor();
            }
            
            // Handle input
            if (is_selected) {
                editing_address_ = byte_address;
                editing_nibble_ = 0;
                edit_mode_ascii_ = false;
                snprintf(edit_buffer_, sizeof(edit_buffer_), "%02X", value);
            }
            
            // Context menu
            if (ImGui::BeginPopupContextItem("HexEditorContextMenu")) {
                if (ImGui::MenuItem("Add to Watch List")) {
                    if (onAddToWatchCallback) {
                        onAddToWatchCallback(byte_address, current_domain_);
                    }
                }
                if (ImGui::MenuItem("Freeze Value")) {
                    frozen_addresses_.insert(byte_address);
                    if (onFreezeValueCallback) {
                        onFreezeValueCallback(byte_address, value, current_domain_);
                    }
                }
                if (ImGui::MenuItem("Unfreeze Value")) {
                    frozen_addresses_.erase(byte_address);
                    if (onUnfreezeValueCallback) {
                        onUnfreezeValueCallback(byte_address, current_domain_);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Bookmark")) {
                    addBookmark(byte_address, "Bookmark");
                }
                ImGui::EndPopup();
            }
        }
        
        ImGui::PopID();
    }
}

void HexEditorWindow::renderTextColumn(uint32_t address) {
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    // Read bytes for this row
    std::vector<uint8_t> row_data(bytes_per_row_);
    for (uint32_t i = 0; i < bytes_per_row_; ++i) {
        uint32_t byte_address = address + i;
        if (domain->isAddressValid(byte_address)) {
            row_data[i] = domain->readByte(byte_address);
        } else {
            row_data[i] = 0;
        }
    }
    
    // Decode text
    std::string text = decodeText(row_data.data(), row_data.size(), text_encoding_);
    
    // Render text with proper colors
    ImGui::PushStyleColor(ImGuiCol_Text, COLOR_NORMAL);
    ImGui::Text("%s", text.c_str());
    ImGui::PopStyleColor();
}

void HexEditorWindow::renderDataInspector() {
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    ImGui::BeginChild("Data Inspector", ImVec2(200, 0), true);
    ImGui::Text("Address: %s", formatAddress(current_address_).c_str());
    ImGui::Separator();
    
    // Read up to 8 bytes for data inspection
    uint8_t data_buffer[8] = {0};
    uint32_t valid_bytes = 0;
    
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t addr = current_address_ + i;
        if (domain->isAddressValid(addr)) {
            data_buffer[i] = domain->readByte(addr);
            valid_bytes++;
        } else {
            break;
        }
    }
    
    if (valid_bytes == 0) {
        ImGui::Text("Invalid address");
        ImGui::EndChild();
        return;
    }
    
    // Display various interpretations
    ImGui::Text("Raw Bytes:");
    for (uint32_t i = 0; i < valid_bytes; ++i) {
        ImGui::SameLine();
        ImGui::Text("%02X", data_buffer[i]);
    }
    
    ImGui::Separator();
    
    // 8-bit interpretations
    if (valid_bytes >= 1) {
        ImGui::Text("int8: %d", static_cast<int8_t>(data_buffer[0]));
        ImGui::Text("uint8: %u", data_buffer[0]);
        ImGui::Text("Binary: %s", std::bitset<8>(data_buffer[0]).to_string().c_str());
    }
    
    // 16-bit interpretations
    if (valid_bytes >= 2) {
        uint16_t val16 = (static_cast<uint16_t>(data_buffer[0]) << 8) | data_buffer[1];
        ImGui::Separator();
        ImGui::Text("int16: %d", static_cast<int16_t>(val16));
        ImGui::Text("uint16: %u", val16);
        
        // Little endian
        uint16_t val16_le = data_buffer[0] | (static_cast<uint16_t>(data_buffer[1]) << 8);
        ImGui::Text("uint16 (LE): %u", val16_le);
    }
    
    // 32-bit interpretations
    if (valid_bytes >= 4) {
        uint32_t val32 = (static_cast<uint32_t>(data_buffer[0]) << 24) |
                       (static_cast<uint32_t>(data_buffer[1]) << 16) |
                       (static_cast<uint32_t>(data_buffer[2]) << 8) |
                       data_buffer[3];
        ImGui::Separator();
        ImGui::Text("int32: %d", static_cast<int32_t>(val32));
        ImGui::Text("uint32: %u", val32);
        ImGui::Text("float: %f", *reinterpret_cast<float*>(&val32));
        
        // Little endian
        uint32_t val32_le = data_buffer[0] |
                           (static_cast<uint32_t>(data_buffer[1]) << 8) |
                           (static_cast<uint32_t>(data_buffer[2]) << 16) |
                           (static_cast<uint32_t>(data_buffer[3]) << 24);
        ImGui::Text("uint32 (LE): %u", val32_le);
        ImGui::Text("float (LE): %f", *reinterpret_cast<float*>(&val32_le));
    }
    
    // 64-bit interpretations
    if (valid_bytes >= 8) {
        uint64_t val64 = (static_cast<uint64_t>(data_buffer[0]) << 56) |
                       (static_cast<uint64_t>(data_buffer[1]) << 48) |
                       (static_cast<uint64_t>(data_buffer[2]) << 40) |
                       (static_cast<uint64_t>(data_buffer[3]) << 32) |
                       (static_cast<uint64_t>(data_buffer[4]) << 24) |
                       (static_cast<uint64_t>(data_buffer[5]) << 16) |
                       (static_cast<uint64_t>(data_buffer[6]) << 8) |
                       data_buffer[7];
        ImGui::Separator();
        ImGui::Text("int64: %lld", static_cast<int64_t>(val64));
        ImGui::Text("uint64: %llu", val64);
        ImGui::Text("double: %f", *reinterpret_cast<double*>(&val64));
        
        // Little endian
        uint64_t val64_le = static_cast<uint64_t>(data_buffer[0]) |
                           (static_cast<uint64_t>(data_buffer[1]) << 8) |
                           (static_cast<uint64_t>(data_buffer[2]) << 16) |
                           (static_cast<uint64_t>(data_buffer[3]) << 24) |
                           (static_cast<uint64_t>(data_buffer[4]) << 32) |
                           (static_cast<uint64_t>(data_buffer[5]) << 40) |
                           (static_cast<uint64_t>(data_buffer[6]) << 48) |
                           (static_cast<uint64_t>(data_buffer[7]) << 56);
        ImGui::Text("double (LE): %f", *reinterpret_cast<double*>(&val64_le));
    }
    
    // DOS Date/Time format if we have 4 bytes
    if (valid_bytes >= 4) {
        uint16_t dos_date = (static_cast<uint16_t>(data_buffer[0]) << 8) | data_buffer[1];
        uint16_t dos_time = (static_cast<uint16_t>(data_buffer[2]) << 8) | data_buffer[3];
        
        ImGui::Separator();
        ImGui::Text("DOS Date: %04X", dos_date);
        ImGui::Text("DOS Time: %04X", dos_time);
        
        // Decode DOS date (bits: YYYYYYYMMMMDDDDD)
        if (dos_date != 0) {
            uint16_t year = ((dos_date >> 9) & 0x7F) + 1980;
            uint16_t month = (dos_date >> 5) & 0x0F;
            uint16_t day = dos_date & 0x1F;
            ImGui::Text("Date: %04d-%02d-%02d", year, month, day);
        }
        
        // Decode DOS time (bits: HHHHHMMMMMMSSSS)
        if (dos_time != 0) {
            uint16_t hour = (dos_time >> 11) & 0x1F;
            uint16_t minute = (dos_time >> 5) & 0x3F;
            uint16_t second = (dos_time & 0x1F) * 2;  // 2-second intervals
            ImGui::Text("Time: %02d:%02d:%02d", hour, minute, second);
        }
    }
    
    ImGui::EndChild();
}

void HexEditorWindow::renderStatusBar() {
    ImGui::Separator();
    
    if (selection_.active) {
        std::string start_addr_str = formatAddress(selection_.start_address);
        std::string end_addr_str = formatAddress(selection_.end_address);
        ImGui::Text("Selection: %s - %s (%u bytes)", 
                   start_addr_str.c_str(),
                   end_addr_str.c_str(),
                   selection_.size());
    } else {
        ImGui::Text("Address: %s", formatAddress(current_address_).c_str());
    }
    
    ImGui::SameLine();
    ImGui::Text("| Domain: %s", current_domain_.c_str());
    
    ImGui::SameLine();
    ImGui::Text("| Encoding: %s", 
                text_encoding_ == TextEncoding::ASCII ? "ASCII" :
                text_encoding_ == TextEncoding::CP932 ? "CP932" :
                text_encoding_ == TextEncoding::CP437 ? "CP437" :
                text_encoding_ == TextEncoding::CP850 ? "CP850" :
                text_encoding_ == TextEncoding::CP866 ? "CP866" :
                text_encoding_ == TextEncoding::LATIN1 ? "Latin-1" : "Unknown");
    
    ImGui::SameLine();
    ImGui::Text("| Frozen: %zu", frozen_addresses_.size());
}

//=============================================================================
// Dialog Rendering
//=============================================================================

void HexEditorWindow::renderGotoDialog() {
    if (ImGui::BeginPopupModal("Go to Address", &show_goto_dialog_)) {
        // Tab-based interface for different goto methods
        if (ImGui::BeginTabBar("GotoTabs")) {
            // Address Input Tab
            if (ImGui::BeginTabItem("Address")) {
                ImGui::Text("Enter address (hex or linear):");
                ImGui::InputText("##Address", goto_input_, sizeof(goto_input_));

                ImGui::Separator();

                if (ImGui::Button("Go", ImVec2(120, 0))) {
                    uint32_t address = parseAddress(goto_input_);
                    gotoAddress(address);
                    show_goto_dialog_ = false;
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    show_goto_dialog_ = false;
                }

                ImGui::EndTabItem();
            }

            // Symbol Jump Tab
            if (ImGui::BeginTabItem("Symbol")) {
                ImGui::Text("Select symbol to jump to:");

                // Static variables for symbol selection
                static std::string selected_symbol = "";
                static std::vector<std::string> available_symbols;

                // Refresh symbol list if needed
                if (g_symbol_manager && available_symbols.empty()) {
                    available_symbols = g_symbol_manager->getSymbolNames();
                }

                // Symbol search and combo box
                static char symbol_search[256] = "";
                ImGui::InputText("Search symbols##symbol_search", symbol_search, sizeof(symbol_search));

                if (ImGui::BeginCombo("##symbol_combo", selected_symbol.empty() ? "Select symbol..." : selected_symbol.c_str())) {
                    for (const auto& symbol : available_symbols) {
                        bool matches = true;
                        if (strlen(symbol_search) > 0) {
                            matches = symbol.find(symbol_search) != std::string::npos;
                        }

                        if (matches && ImGui::Selectable(symbol.c_str(), selected_symbol == symbol)) {
                            selected_symbol = symbol;
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();

                bool can_jump = !selected_symbol.empty() && g_symbol_manager;
                if (!can_jump) {
                    ImGui::TextColored(ImVec4(1.0f, 0.39f, 0.39f, 1.0f), "No symbols available or no symbol selected");
                }

                if (ImGui::Button("Jump to Symbol", ImVec2(120, 0)) && can_jump) {
                    uint32_t symbol_addr = g_symbol_manager->getSymbolAddress(selected_symbol);
                    if (symbol_addr != LuaEngineSymbols::SymbolManager::INVALID_ADDRESS) {
                        // Convert linear address to display format
                        std::snprintf(goto_input_, sizeof(goto_input_), "%08X", symbol_addr);
                        gotoAddress(symbol_addr);
                        show_goto_dialog_ = false;
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Refresh Symbols", ImVec2(120, 0)) && g_symbol_manager) {
                    available_symbols.clear();
                    available_symbols = g_symbol_manager->getSymbolNames();
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    show_goto_dialog_ = false;
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndPopup();
    }
}

void HexEditorWindow::renderFindDialog() {
    if (ImGui::BeginPopupModal("Find", &show_find_dialog_)) {
        ImGui::Text("Find:");
        ImGui::InputText("##Find", find_input_, sizeof(find_input_));
        
        ImGui::Text("Replace:");
        ImGui::InputText("##Replace", replace_input_, sizeof(replace_input_));
        
        ImGui::Checkbox("Case Sensitive", &find_replace_.case_sensitive);
        ImGui::Checkbox("Whole Bytes", &find_replace_.whole_bytes);
        
        ImGui::Separator();
        
        if (ImGui::Button("Find Next", ImVec2(120, 0))) {
            find_replace_.find_pattern = parseHexString(find_input_);
            findNext();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Replace", ImVec2(120, 0))) {
            find_replace_.replace_pattern = parseHexString(replace_input_);
            replaceNext();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Replace All", ImVec2(120, 0))) {
            find_replace_.replace_pattern = parseHexString(replace_input_);
            replaceAll();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_find_dialog_ = false;
        }
        
        ImGui::EndPopup();
    }
}

void HexEditorWindow::renderBookmarkDialog() {
    if (ImGui::BeginPopupModal("Add Bookmark", &show_bookmark_dialog_)) {
        ImGui::Text("Address: %s", formatAddress(current_address_).c_str());
        
        ImGui::Text("Name:");
        ImGui::InputText("##Name", bookmark_name_input_, sizeof(bookmark_name_input_));
        
        ImGui::Text("Description:");
        ImGui::InputTextMultiline("##Description", bookmark_desc_input_, 
                                  sizeof(bookmark_desc_input_), ImVec2(0, 60));
        
        ImGui::Separator();
        
        if (ImGui::Button("Add", ImVec2(120, 0))) {
            addBookmark(current_address_, bookmark_name_input_, bookmark_desc_input_);
            show_bookmark_dialog_ = false;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_bookmark_dialog_ = false;
        }
        
        ImGui::EndPopup();
    }
}

void HexEditorWindow::renderExportDialog() {
    if (ImGui::BeginPopupModal("Export Data", &show_export_dialog_)) {
        ImGui::Text("Export selection to file:");
        
        if (selection_.active) {
            ImGui::Text("Range: %s - %s (%u bytes)", 
                       formatAddress(selection_.start_address).c_str(),
                       formatAddress(selection_.end_address).c_str(),
                       selection_.size());
        } else {
            ImGui::Text("No selection active");
        }
        
        ImGui::Text("Filename:");
        ImGui::InputText("##Filename", export_filename_input_, sizeof(export_filename_input_));
        
        ImGui::Separator();
        
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            if (selection_.active) {
                exportSelection(export_filename_input_);
            }
            show_export_dialog_ = false;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_export_dialog_ = false;
        }
        
        ImGui::EndPopup();
    }
}

//=============================================================================
// Encoding Support
//=============================================================================

std::string HexEditorWindow::decodeText(const uint8_t* data, size_t length, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::ASCII:
            {
                std::string result;
                for (size_t i = 0; i < length; ++i) {
                    if (data[i] >= 32 && data[i] < 127) {
                        result += static_cast<char>(data[i]);
                    } else {
                        result += '.';
                    }
                }
                return result;
            }
        case TextEncoding::CP932:
            return cp932ToUtf8(data, length);
        case TextEncoding::CP437:
            return cp437ToUtf8(data, length);
        case TextEncoding::CP850:
            return cp850ToUtf8(data, length);
        case TextEncoding::CP866:
            return cp866ToUtf8(data, length);
        case TextEncoding::LATIN1:
            {
                std::string result;
                for (size_t i = 0; i < length; ++i) {
                    if (data[i] >= 32) {
                        result += static_cast<char>(data[i]);
                    } else {
                        result += '.';
                    }
                }
                return result;
            }
        default:
            return std::string(length, '?');
    }
}

std::string HexEditorWindow::cp932ToUtf8(const uint8_t* data, size_t length) {
    std::string result;
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        
        if (byte < 0x80) {
            // ASCII
            if (byte >= 32) {
                result += static_cast<char>(byte);
            } else {
                result += '.';
            }
        } else if (byte >= 0xA1 && byte <= 0xDF) {
            // Half-width katakana
            // Convert to full-width katakana in UTF-8
            // This is a simplified conversion
            result += "カ";  // Placeholder for katakana
        } else if (isValidCP932Lead(byte) && i + 1 < length) {
            // Double-byte character
            uint8_t lead = byte;
            uint8_t trail = data[i + 1];
            
            if (isValidCP932Trail(trail)) {
                // Convert Shift-JIS to UTF-8
                // This is a simplified conversion - in a real implementation,
                // you would use a full conversion table
                result += "日";  // Placeholder for Japanese characters
                ++i;  // Skip the trail byte
            } else {
                result += '.';
            }
        } else {
            result += '.';
        }
    }
    
    return result;
}

std::string HexEditorWindow::cp437ToUtf8(const uint8_t* data, size_t length) {
    // CP437 to UTF-8 conversion table (simplified)
    static const char* cp437_table[256] = {
        ".", "☺", "☻", "♥", "♦", "♣", "♠", "•", "◘", "○", "◙", "♂", "♀", "♪", "♫", "☼",
        "►", "◄", "↕", "‼", "¶", "§", "▬", "↨", "↑", "↓", "→", "←", "∟", "↔", "▲", "▼",
        " ", "!", "\"", "#", "$", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
        "@", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
        "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", "^", "_",
        "`", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
        "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "{", "|", "}", "~", "⌂",
        "Ç", "ü", "é", "â", "ä", "à", "å", "ç", "ê", "ë", "è", "ï", "î", "ì", "Ä", "Å",
        "É", "æ", "Æ", "ô", "ö", "ò", "û", "ù", "ÿ", "Ö", "Ü", "¢", "£", "¥", "₧", "ƒ",
        "á", "í", "ó", "ú", "ñ", "Ñ", "ª", "º", "¿", "⌐", "¬", "½", "¼", "¡", "«", "»",
        "░", "▒", "▓", "│", "┤", "╡", "╢", "╖", "╕", "╣", "║", "╗", "╝", "╜", "╛", "┐",
        "└", "┴", "┬", "├", "─", "┼", "╞", "╟", "╚", "╔", "╩", "╦", "╠", "═", "╬", "╧",
        "╨", "╤", "╥", "╙", "╘", "╒", "╓", "╫", "╪", "┘", "┌", "█", "▄", "▌", "▐", "▀",
        "α", "ß", "Γ", "π", "Σ", "σ", "µ", "τ", "Φ", "Θ", "Ω", "δ", "∞", "φ", "ε", "∩",
        "≡", "±", "≥", "≤", "⌠", "⌡", "÷", "≈", "°", "∙", "·", "√", "ⁿ", "²", "■", " "
    };
    
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        result += cp437_table[data[i]];
    }
    return result;
}

std::string HexEditorWindow::cp850ToUtf8(const uint8_t* data, size_t length) {
    // Simplified CP850 conversion
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        if (byte >= 32 && byte < 127) {
            result += static_cast<char>(byte);
        } else {
            result += '.';
        }
    }
    return result;
}

std::string HexEditorWindow::cp866ToUtf8(const uint8_t* data, size_t length) {
    // Simplified CP866 conversion
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        if (byte >= 32 && byte < 127) {
            result += static_cast<char>(byte);
        } else if (byte >= 0x80 && byte <= 0xAF) {
            // Cyrillic characters - simplified
            result += "Р";  // Placeholder for Russian characters
        } else {
            result += '.';
        }
    }
    return result;
}

bool HexEditorWindow::isValidCP932Lead(uint8_t byte) {
    return (byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC);
}

bool HexEditorWindow::isValidCP932Trail(uint8_t byte) {
    return (byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFC);
}

//=============================================================================
// Utility Methods
//=============================================================================

uint32_t HexEditorWindow::parseAddress(const std::string& input) {
    return LuaEngineDebugUtils::parseAddress(input);
}

std::vector<uint8_t> HexEditorWindow::parseHexString(const std::string& input) {
    return LuaEngineDebugUtils::parseHexString(input);
}

std::string HexEditorWindow::formatHexByte(uint8_t value) {
    return LuaEngineDebugUtils::formatHexByte(value);
}

std::string HexEditorWindow::formatAddress(uint32_t address) {
    return LuaEngineDebugUtils::formatAddress(address);
}

uint32_t HexEditorWindow::getByteColor(uint32_t address) {
    if (selection_.contains(address)) {
        return COLOR_SELECTED;
    }
    
    if (frozen_addresses_.find(address) != frozen_addresses_.end()) {
        return COLOR_FROZEN;
    }
    
    if (watched_addresses_.find(address) != watched_addresses_.end()) {
        return COLOR_WATCHED;
    }
    
    return COLOR_NORMAL;
}

uint32_t HexEditorWindow::getTextColor(uint32_t address) {
    return getByteColor(address);
}

//=============================================================================
// Navigation and Operations
//=============================================================================

void HexEditorWindow::gotoAddress(uint32_t address) {
    current_address_ = address;
    scrollToAddress(address);
}

void HexEditorWindow::gotoAddress(uint32_t address, const std::string& domain) {
    setDomain(domain);
    gotoAddress(address);
}

void HexEditorWindow::scrollToAddress(uint32_t address) {
    scroll_address_ = (address / bytes_per_row_) * bytes_per_row_;
}

void HexEditorWindow::setDomain(const std::string& domain) {
    if (memory_manager_ && memory_manager_->getDomain(domain)) {
        current_domain_ = domain;
        // Reset to domain base address
        auto* dom = memory_manager_->getDomain(domain);
        if (dom) {
            gotoAddress(dom->getBaseAddress());
        }
    }
}

void HexEditorWindow::setBytesPerRow(uint32_t bytes) {
    bytes_per_row_ = bytes;
}

void HexEditorWindow::setTextEncoding(TextEncoding encoding) {
    text_encoding_ = encoding;
}

void HexEditorWindow::clearSelection() {
    selection_.active = false;
}

void HexEditorWindow::selectAll() {
    if (memory_manager_) {
        auto* domain = memory_manager_->getDomain(current_domain_);
        if (domain) {
            selection_.start_address = domain->getBaseAddress();
            selection_.end_address = domain->getBaseAddress() + domain->getSize() - 1;
            selection_.active = true;
        }
    }
}

void HexEditorWindow::copySelection() {
    if (!selection_.active || !memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    clipboard_data_.clear();
    for (uint32_t addr = selection_.start_address; addr <= selection_.end_address; ++addr) {
        if (domain->isAddressValid(addr)) {
            clipboard_data_.push_back(domain->readByte(addr));
        }
    }
}

void HexEditorWindow::pasteAtCursor() {
    if (clipboard_data_.empty() || !memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    for (size_t i = 0; i < clipboard_data_.size(); ++i) {
        uint32_t addr = current_address_ + i;
        if (domain->isAddressValid(addr)) {
            domain->writeByte(addr, clipboard_data_[i]);
        }
    }
}

void HexEditorWindow::addBookmark(uint32_t address, const std::string& name, const std::string& description) {
    bookmarks_.emplace_back(address, name, description);
}

void HexEditorWindow::removeBookmark(uint32_t address) {
    bookmarks_.erase(
        std::remove_if(bookmarks_.begin(), bookmarks_.end(),
                      [address](const MemoryBookmark& bookmark) {
                          return bookmark.address == address;
                      }),
        bookmarks_.end()
    );
}

void HexEditorWindow::gotoBookmark(uint32_t address) {
    gotoAddress(address);
}

void HexEditorWindow::findNext() {
    if (find_replace_.find_pattern.empty()) return;
    
    find_results_ = searchMemory(find_replace_.find_pattern);
    if (!find_results_.empty()) {
        if (current_find_index_ < find_results_.size()) {
            gotoAddress(find_results_[current_find_index_]);
            current_find_index_ = (current_find_index_ + 1) % find_results_.size();
        }
    }
}

void HexEditorWindow::findPrevious() {
    if (find_results_.empty()) return;
    
    if (current_find_index_ > 0) {
        current_find_index_--;
    } else {
        current_find_index_ = find_results_.size() - 1;
    }
    
    gotoAddress(find_results_[current_find_index_]);
}

void HexEditorWindow::replaceNext() {
    if (find_replace_.find_pattern.empty() || find_replace_.replace_pattern.empty()) return;
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    // Find the next occurrence starting from current address
    std::vector<uint32_t> results = searchMemory(find_replace_.find_pattern);
    if (results.empty()) return;
    
    // Find the first result at or after current address
    uint32_t target_address = UINT32_MAX;
    for (uint32_t addr : results) {
        if (addr >= current_address_) {
            target_address = addr;
            break;
        }
    }
    
    // If no result found after current address, wrap to beginning
    if (target_address == UINT32_MAX && !results.empty()) {
        target_address = results[0];
    }
    
    if (target_address != UINT32_MAX) {
        // Replace the pattern at the target address
        for (size_t i = 0; i < find_replace_.replace_pattern.size(); ++i) {
            domain->writeByte(target_address + i, find_replace_.replace_pattern[i]);
        }
        
        // Move to the next position after the replacement
        gotoAddress(target_address + find_replace_.replace_pattern.size());
        
        // Update find results to exclude the replaced occurrence
        find_results_.erase(std::remove(find_results_.begin(), find_results_.end(), target_address), 
                           find_results_.end());
    }
}

void HexEditorWindow::replaceAll() {
    if (find_replace_.find_pattern.empty() || find_replace_.replace_pattern.empty()) return;
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    // Find all occurrences
    std::vector<uint32_t> results = searchMemory(find_replace_.find_pattern);
    if (results.empty()) return;
    
    size_t replaced_count = 0;
    
    // Replace from the end to the beginning to maintain address validity
    // This is important if replacement pattern has different size than find pattern
    for (auto it = results.rbegin(); it != results.rend(); ++it) {
        uint32_t address = *it;
        
        // Replace the pattern at this address
        for (size_t i = 0; i < find_replace_.replace_pattern.size(); ++i) {
            domain->writeByte(address + i, find_replace_.replace_pattern[i]);
        }
        
        replaced_count++;
    }
    
    // Clear find results since all were replaced
    find_results_.clear();
    current_find_index_ = 0;
    
    // Update status or show notification about replacement count
    // (Could add a callback here to notify UI about replacement count)
}

std::vector<uint32_t> HexEditorWindow::searchMemory(const std::vector<uint8_t>& pattern) {
    std::vector<uint32_t> results;
    
    if (!memory_manager_ || pattern.empty()) return results;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return results;
    
    uint32_t domain_size = domain->getSize();
    uint32_t base_address = domain->getBaseAddress();
    
    for (uint32_t addr = base_address; addr <= base_address + domain_size - pattern.size(); ++addr) {
        bool match = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (!domain->isAddressValid(addr + i) || 
                domain->readByte(addr + i) != pattern[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            results.push_back(addr);
        }
    }
    
    return results;
}

void HexEditorWindow::exportSelection(const std::string& filename) {
    if (!selection_.active || !memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return;
    
    for (uint32_t addr = selection_.start_address; addr <= selection_.end_address; ++addr) {
        if (domain->isAddressValid(addr)) {
            uint8_t byte = domain->readByte(addr);
            file.write(reinterpret_cast<const char*>(&byte), 1);
        }
    }
}

//=============================================================================
// Public Interface
//=============================================================================

void HexEditorWindow::show() {
    printf("DEBUG: HexEditorWindow::show() called, setting show_window_ = true\n");
    show_window_ = true;
    
    // Ensure memory manager is initialized when showing the window
    if (!memory_manager_) {
        printf("DEBUG: Memory manager not set, auto-initializing in show()\n");
        memory_manager_ = GetGlobalMemoryDomainManager();
        if (memory_manager_) {
            printf("DEBUG: Memory manager successfully initialized in show()\n");
        } else {
            printf("DEBUG: Failed to initialize memory manager in show()\n");
        }
    }
}

void HexEditorWindow::hide() {
    printf("DEBUG: HexEditorWindow::hide() called, setting show_window_ = false\n");
    show_window_ = false;
}

bool HexEditorWindow::isVisible() const {
    printf("DEBUG: HexEditorWindow::isVisible() called - show_window_ = %s\n", show_window_ ? "true" : "false");
    return show_window_;
}

} // namespace LuaEngineHexEditor
