#include "hex_editor.h"
#include "debug_utils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cctype>
#include <cstring>

namespace LuaEngineHexEditor {

//=============================================================================
// HexEditor Implementation
//=============================================================================

HexEditor::HexEditor() 
    : memory_manager_(nullptr), current_address_(0), viewport_rows_(25), viewport_cols_(80),
      cursor_address_(0), cursor_on_hex_(true), history_position_(0),
      current_search_result_(0), operation_cancelled_(false) {
    
    // Set default domain
    current_domain_ = "DOS_CONVENTIONAL";
}

HexEditor::~HexEditor() {
    shutdown();
}

bool HexEditor::initialize(MemoryDomainManager* memory_mgr) {
    if (!memory_mgr) return false;
    
    memory_manager_ = memory_mgr;
    
    // Initialize with first available domain
    auto domain_names = memory_manager_->getDomainNames();
    if (!domain_names.empty()) {
        current_domain_ = domain_names[0];
    }
    
    // Add initial position to history
    addToHistory(current_address_);
    
    return true;
}

void HexEditor::shutdown() {
    clearCache();
    clearUndoHistory();
    bookmarks_.clear();
    navigation_history_.clear();
    search_results_.clear();
    modified_data_.clear();
}

//=============================================================================
// Navigation Methods
//=============================================================================

void HexEditor::gotoAddress(uint32_t address, const std::string& domain) {
    if (!domain.empty() && domain != current_domain_) {
        current_domain_ = domain;
    }
    
    if (!isValidAddress(address)) return;
    
    addToHistory(current_address_);
    current_address_ = address;
    cursor_address_ = address;
    
    if (onAddressChanged) {
        onAddressChanged(current_address_);
    }
}

void HexEditor::gotoBookmark(size_t bookmark_index) {
    if (bookmark_index < bookmarks_.size()) {
        const auto& bookmark = bookmarks_[bookmark_index];
        gotoAddress(bookmark.address, bookmark.domain_name);
    }
}

void HexEditor::navigateUp(size_t rows) {
    uint32_t new_address = current_address_;
    if (new_address >= config_.bytes_per_row * rows) {
        new_address -= config_.bytes_per_row * rows;
    } else {
        new_address = 0;
    }
    gotoAddress(new_address);
}

void HexEditor::navigateDown(size_t rows) {
    uint32_t new_address = current_address_ + config_.bytes_per_row * rows;
    gotoAddress(new_address);
}

void HexEditor::navigateLeft() {
    if (current_address_ > 0) {
        gotoAddress(current_address_ - 1);
    }
}

void HexEditor::navigateRight() {
    gotoAddress(current_address_ + 1);
}

void HexEditor::pageUp() {
    navigateUp(viewport_rows_);
}

void HexEditor::pageDown() {
    navigateDown(viewport_rows_);
}

void HexEditor::goBack() {
    if (history_position_ > 0) {
        history_position_--;
        current_address_ = navigation_history_[history_position_];
        cursor_address_ = current_address_;
        
        if (onAddressChanged) {
            onAddressChanged(current_address_);
        }
    }
}

void HexEditor::goForward() {
    if (history_position_ < navigation_history_.size() - 1) {
        history_position_++;
        current_address_ = navigation_history_[history_position_];
        cursor_address_ = current_address_;
        
        if (onAddressChanged) {
            onAddressChanged(current_address_);
        }
    }
}

void HexEditor::addToHistory(uint32_t address) {
    // Remove any entries after current position
    if (history_position_ < navigation_history_.size() - 1) {
        navigation_history_.erase(navigation_history_.begin() + history_position_ + 1,
                                navigation_history_.end());
    }
    
    // Add new entry
    navigation_history_.push_back(address);
    history_position_ = navigation_history_.size() - 1;
    
    // Limit history size
    const size_t max_history = 100;
    if (navigation_history_.size() > max_history) {
        navigation_history_.erase(navigation_history_.begin());
        history_position_--;
    }
}

//=============================================================================
// Display and Rendering Methods
//=============================================================================

void HexEditor::setViewport(size_t rows, size_t cols) {
    viewport_rows_ = rows;
    viewport_cols_ = cols;
}

std::vector<std::string> HexEditor::renderView() {
    std::vector<std::string> lines;
    
    for (size_t row = 0; row < viewport_rows_; ++row) {
        uint32_t row_address = current_address_ + (row * config_.bytes_per_row);
        std::string line;
        
        // Address column
        if (config_.show_addresses) {
            line += renderAddressColumn(row_address) + " ";
        }
        
        // Hex column
        line += renderHexColumn(row_address, config_.bytes_per_row);
        
        // Text column
        if (config_.show_ascii) {
            line += " ";
            switch (config_.display_mode) {
                case HexDisplayMode::ASCII_SIDEBAR:
                    line += renderTextColumn(row_address, config_.bytes_per_row, 437);
                    break;
                case HexDisplayMode::SJIS_SIDEBAR:
                    line += renderTextColumn(row_address, config_.bytes_per_row, 932);
                    break;
                case HexDisplayMode::UTF8_SIDEBAR:
                    line += renderTextColumn(row_address, config_.bytes_per_row, config_.default_codepage);
                    break;
                default:
                    line += renderTextColumn(row_address, config_.bytes_per_row, 437);
                    break;
            }
        }
        
        lines.push_back(line);
    }
    
    return lines;
}

std::string HexEditor::renderAddressColumn(uint32_t address) {
    std::stringstream ss;
    if (config_.zero_pad_addresses) {
        ss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << address;
    } else {
        ss << std::hex << std::uppercase << address;
    }
    return ss.str();
}

std::string HexEditor::renderHexColumn(uint32_t address, size_t bytes) {
    std::stringstream ss;
    std::vector<uint8_t> data = readBytes(address, bytes);
    
    for (size_t i = 0; i < bytes; ++i) {
        if (i > 0) {
            switch (config_.grouping) {
                case HexGrouping::BYTE:
                    ss << " ";
                    break;
                case HexGrouping::WORD:
                    if (i % 2 == 0) ss << " ";
                    break;
                case HexGrouping::DWORD:
                    if (i % 4 == 0) ss << " ";
                    break;
                case HexGrouping::QWORD:
                    if (i % 8 == 0) ss << " ";
                    break;
            }
        }
        
        if (i < data.size()) {
            ss << formatHexByte(data[i]);
        } else {
            ss << "  ";
        }
    }
    
    return ss.str();
}

std::string HexEditor::renderTextColumn(uint32_t address, size_t bytes, uint16_t codepage) {
    std::vector<uint8_t> data = readBytes(address, bytes);
    
    if (codepage == 932) {
        // SJIS rendering
        std::string sjis_text = convertBytesToText(data, codepage);
        std::string result;
        
        for (char c : sjis_text) {
            if (std::isprint(static_cast<unsigned char>(c))) {
                result += c;
            } else {
                result += '.';
            }
        }
        
        return result;
    } else {
        // ASCII/other codepage rendering
        std::string result;
        for (uint8_t byte : data) {
            result += formatAsciiChar(byte);
        }
        return result;
    }
}

//=============================================================================
// Selection Methods
//=============================================================================

void HexEditor::startSelection(uint32_t address) {
    selection_.start_address = address;
    selection_.end_address = address;
    selection_.domain_name = current_domain_;
    selection_.active = true;
    
    if (onSelectionChanged) {
        onSelectionChanged(selection_);
    }
}

void HexEditor::extendSelection(uint32_t address) {
    if (selection_.active) {
        selection_.end_address = address;
        
        // Ensure start <= end
        if (selection_.start_address > selection_.end_address) {
            std::swap(selection_.start_address, selection_.end_address);
        }
        
        if (onSelectionChanged) {
            onSelectionChanged(selection_);
        }
    }
}

void HexEditor::clearSelection() {
    selection_.active = false;
    
    if (onSelectionChanged) {
        onSelectionChanged(selection_);
    }
}

void HexEditor::selectAll() {
    if (!memory_manager_) return;
    
    auto* domain = memory_manager_->getDomain(current_domain_);
    if (!domain) return;
    
    selection_.start_address = domain->getBaseAddress();
    selection_.end_address = domain->getBaseAddress() + domain->getSize() - 1;
    selection_.domain_name = current_domain_;
    selection_.active = true;
    
    if (onSelectionChanged) {
        onSelectionChanged(selection_);
    }
}

void HexEditor::selectRange(uint32_t start, uint32_t end) {
    selection_.start_address = std::min(start, end);
    selection_.end_address = std::max(start, end);
    selection_.domain_name = current_domain_;
    selection_.active = true;
    
    if (onSelectionChanged) {
        onSelectionChanged(selection_);
    }
}

//=============================================================================
// Data Access and Editing Methods
//=============================================================================

uint8_t HexEditor::readByte(uint32_t address) {
    if (!memory_manager_) return 0;
    
    return memory_manager_->readByte(current_domain_, address);
}

std::vector<uint8_t> HexEditor::readBytes(uint32_t address, size_t count) {
    std::vector<uint8_t> result;
    result.reserve(count);
    
    // Check cache first
    if (config_.cache_size > 0) {
        auto cached = getCachedData(address, count);
        if (!cached.empty()) {
            return cached;
        }
    }
    
    // Read from memory
    for (size_t i = 0; i < count; ++i) {
        result.push_back(readByte(address + i));
    }
    
    // Cache the data
    if (config_.cache_size > 0) {
        cacheData(address, result);
    }
    
    return result;
}

std::string HexEditor::readText(uint32_t address, size_t count, uint16_t codepage) {
    std::vector<uint8_t> data = readBytes(address, count);
    return convertBytesToText(data, codepage);
}

void HexEditor::writeByte(uint32_t address, uint8_t value) {
    if (!memory_manager_) return;
    
    // Create undo operation
    HexEditOperation operation(address, current_domain_);
    operation.old_data.push_back(readByte(address));
    operation.new_data.push_back(value);
    pushUndoOperation(operation);
    
    // Write the data
    memory_manager_->writeByte(current_domain_, address, value);
    
    // Track modification
    modified_data_[address] = {value};
    
    // Invalidate cache
    invalidateCache(address);
    
    if (onDataChanged) {
        onDataChanged(address, operation.old_data[0], value);
    }
}

void HexEditor::writeBytes(uint32_t address, const std::vector<uint8_t>& data) {
    if (!memory_manager_ || data.empty()) return;
    
    // Create undo operation
    HexEditOperation operation(address, current_domain_);
    operation.old_data = readBytes(address, data.size());
    operation.new_data = data;
    pushUndoOperation(operation);
    
    // Write the data
    for (size_t i = 0; i < data.size(); ++i) {
        memory_manager_->writeByte(current_domain_, address + i, data[i]);
        modified_data_[address + i] = {data[i]};
    }
    
    // Invalidate cache
    invalidateCache(address, data.size());
    
    if (onDataChanged) {
        onDataChanged(address, 0, 0); // Simplified notification for bulk changes
    }
}

void HexEditor::writeText(uint32_t address, const std::string& text, uint16_t codepage) {
    try {
        // Validate input parameters
        if (text.length() > 0x100000) {  // Max 1MB text write
            return;
        }
        
        std::vector<uint8_t> data = convertTextToBytes(text, codepage);
        if (!data.empty()) {
            writeBytes(address, data);
        }
    } catch (const std::exception&) {
        // Error during text conversion or writing, fail silently
    } catch (...) {
        // Any other exception, fail silently
    }
}

//=============================================================================
// Find and Replace Methods
//=============================================================================

bool HexEditor::findNext(const HexSearchOptions& options) {
    if (options.hex_pattern) {
        search_results_ = searchHexPattern(options.pattern);
    } else {
        search_results_ = searchTextPattern(options.pattern, options.codepage);
    }
    
    if (search_results_.empty()) return false;
    
    // Find next result after current address
    for (size_t i = current_search_result_; i < search_results_.size(); ++i) {
        if (search_results_[i] > current_address_) {
            current_search_result_ = i;
            gotoAddress(search_results_[i]);
            return true;
        }
    }
    
    // Wrap around if enabled
    if (options.wrap_around && !search_results_.empty()) {
        current_search_result_ = 0;
        gotoAddress(search_results_[0]);
        return true;
    }
    
    return false;
}

bool HexEditor::findPrevious(const HexSearchOptions& options) {
    if (search_results_.empty()) {
        findNext(options); // Initialize search results
        if (search_results_.empty()) return false;
    }
    
    // Find previous result before current address
    for (int i = current_search_result_ - 1; i >= 0; --i) {
        if (search_results_[i] < current_address_) {
            current_search_result_ = i;
            gotoAddress(search_results_[i]);
            return true;
        }
    }
    
    // Wrap around if enabled
    if (options.wrap_around && !search_results_.empty()) {
        current_search_result_ = search_results_.size() - 1;
        gotoAddress(search_results_[current_search_result_]);
        return true;
    }
    
    return false;
}

size_t HexEditor::findAll(const HexSearchOptions& options) {
    if (options.hex_pattern) {
        search_results_ = searchHexPattern(options.pattern);
    } else {
        search_results_ = searchTextPattern(options.pattern, options.codepage);
    }
    
    current_search_result_ = 0;
    return search_results_.size();
}

size_t HexEditor::replaceAll(const HexSearchOptions& find_options, 
                            const std::string& replace_pattern, 
                            bool replace_as_hex) {
    size_t count = findAll(find_options);
    if (count == 0) return 0;
    
    std::vector<uint8_t> replace_data;
    if (replace_as_hex) {
        replace_data = HexUtils::hexStringToBytes(replace_pattern);
    } else {
        replace_data = convertTextToBytes(replace_pattern, find_options.codepage);
    }
    
    // Replace from end to beginning to maintain address validity
    for (auto it = search_results_.rbegin(); it != search_results_.rend(); ++it) {
        writeBytes(*it, replace_data);
    }
    
    return count;
}

//=============================================================================
// Bookmark Methods
//=============================================================================

size_t HexEditor::addBookmark(uint32_t address, const std::string& label, 
                             const std::string& notes) {
    HexBookmark bookmark(address, current_domain_, label, notes);
    bookmarks_.push_back(bookmark);
    return bookmarks_.size() - 1;
}

void HexEditor::removeBookmark(size_t index) {
    if (index < bookmarks_.size()) {
        bookmarks_.erase(bookmarks_.begin() + index);
    }
}

void HexEditor::removeBookmarkByAddress(uint32_t address) {
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                          [address, this](const HexBookmark& bookmark) {
                              return bookmark.address == address && 
                                     bookmark.domain_name == current_domain_;
                          });
    
    if (it != bookmarks_.end()) {
        bookmarks_.erase(it);
    }
}

HexBookmark* HexEditor::getBookmark(size_t index) {
    return (index < bookmarks_.size()) ? &bookmarks_[index] : nullptr;
}

//=============================================================================
// Undo/Redo Methods
//=============================================================================

void HexEditor::undo() {
    if (undo_stack_.empty()) return;
    
    HexEditOperation operation = undo_stack_.back();
    undo_stack_.pop_back();
    
    // Restore old data
    for (size_t i = 0; i < operation.old_data.size(); ++i) {
        memory_manager_->writeByte(operation.domain_name, operation.address + i, operation.old_data[i]);
    }
    
    // Update modified data tracking
    for (size_t i = 0; i < operation.old_data.size(); ++i) {
        auto it = modified_data_.find(operation.address + i);
        if (it != modified_data_.end()) {
            modified_data_.erase(it);
        }
    }
    
    // Move to redo stack
    redo_stack_.push_back(operation);
    
    // Invalidate cache
    invalidateCache(operation.address, operation.old_data.size());
}

void HexEditor::redo() {
    if (redo_stack_.empty()) return;
    
    HexEditOperation operation = redo_stack_.back();
    redo_stack_.pop_back();
    
    // Restore new data
    for (size_t i = 0; i < operation.new_data.size(); ++i) {
        memory_manager_->writeByte(operation.domain_name, operation.address + i, operation.new_data[i]);
        modified_data_[operation.address + i] = {operation.new_data[i]};
    }
    
    // Move back to undo stack
    undo_stack_.push_back(operation);
    
    // Invalidate cache
    invalidateCache(operation.address, operation.new_data.size());
}

void HexEditor::clearUndoHistory() {
    undo_stack_.clear();
    redo_stack_.clear();
}

//=============================================================================
// Import/Export Methods
//=============================================================================

bool HexEditor::exportSelection(const std::string& filename, const std::string& format) {
    if (!selection_.active) return false;
    
    if (format == "bin") {
        return exportAsBinary(filename);
    } else if (format == "hex") {
        return exportAsHexDump(filename);
    } else if (format == "c") {
        return exportAsC(filename);
    }
    
    return false;
}

bool HexEditor::importFile(const std::string& filename, uint32_t address, bool insert) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();
    
    if (data.empty()) return false;
    
    writeBytes(address, data);
    return true;
}

bool HexEditor::exportToIntelHex(const std::string& filename) {
    if (!selection_.active) return false;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    const size_t line_size = 16;
    uint32_t address = selection_.start_address;
    size_t remaining = selection_.getSize();
    
    while (remaining > 0) {
        size_t chunk_size = std::min(remaining, line_size);
        std::vector<uint8_t> data = readBytes(address, chunk_size);
        
        // Intel HEX format: :LLAAAATT[DD...]CC
        file << ":";
        file << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << chunk_size;
        file << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << (address & 0xFFFF);
        file << "00"; // Data record type
        
        uint8_t checksum = chunk_size + ((address >> 8) & 0xFF) + (address & 0xFF);
        for (uint8_t byte : data) {
            file << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            checksum += byte;
        }
        
        checksum = (~checksum + 1) & 0xFF;
        file << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(checksum);
        file << "\n";
        
        address += chunk_size;
        remaining -= chunk_size;
    }
    
    // End of file record
    file << ":00000001FF\n";
    file.close();
    return true;
}

//=============================================================================
// Analysis Methods
//=============================================================================

std::map<uint8_t, size_t> HexEditor::getByteFrequency(uint32_t start, uint32_t end) {
    std::map<uint8_t, size_t> frequency;
    
    for (uint32_t addr = start; addr <= end; ++addr) {
        uint8_t byte = readByte(addr);
        frequency[byte]++;
    }
    
    return frequency;
}

std::vector<uint32_t> HexEditor::findPattern(const std::vector<uint8_t>& pattern) {
    if (!memory_manager_ || pattern.empty()) return {};
    
    return memory_manager_->searchBytes(current_domain_, pattern);
}

std::vector<uint32_t> HexEditor::findTextPattern(const std::string& text, uint16_t codepage) {
    std::vector<uint8_t> pattern = convertTextToBytes(text, codepage);
    return findPattern(pattern);
}

//=============================================================================
// File Operations
//=============================================================================

bool HexEditor::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    // Save configuration
    file << "[Config]\n";
    file << "display_mode=" << static_cast<int>(config_.display_mode) << "\n";
    file << "edit_mode=" << static_cast<int>(config_.edit_mode) << "\n";
    file << "bytes_per_row=" << config_.bytes_per_row << "\n";
    file << "current_address=" << std::hex << current_address_ << "\n";
    file << "current_domain=" << current_domain_ << "\n";
    
    // Save bookmarks
    file << "\n[Bookmarks]\n";
    for (const auto& bookmark : bookmarks_) {
        file << std::hex << bookmark.address << "|" << bookmark.domain_name 
             << "|" << bookmark.label << "|" << bookmark.notes << "\n";
    }
    
    file.close();
    return true;
}

bool HexEditor::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    std::string line;
    std::string section;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }
        
        if (section == "Config") {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                if (key == "current_address") {
                    current_address_ = std::stoul(value, nullptr, 16);
                } else if (key == "current_domain") {
                    current_domain_ = value;
                } else if (key == "bytes_per_row") {
                    config_.bytes_per_row = std::stoul(value);
                }
            }
        } else if (section == "Bookmarks") {
            std::stringstream ss(line);
            std::string item;
            
            if (std::getline(ss, item, '|')) {
                uint32_t address = std::stoul(item, nullptr, 16);
                std::string domain, label, notes;
                
                std::getline(ss, domain, '|');
                std::getline(ss, label, '|');
                std::getline(ss, notes);
                
                bookmarks_.push_back(HexBookmark(address, domain, label, notes));
            }
        }
    }
    
    file.close();
    return true;
}

//=============================================================================
// Status and Utility Methods
//=============================================================================

std::string HexEditor::getStatusText() const {
    std::stringstream ss;
    ss << "Address: " << formatAddress(current_address_);
    ss << " | Domain: " << current_domain_;
    ss << " | Mode: " << (config_.edit_mode == HexEditMode::READ_ONLY ? "Read-Only" : "Edit");
    
    if (selection_.active) {
        ss << " | Selection: " << selection_.getSize() << " bytes";
    }
    
    if (hasUnsavedChanges()) {
        ss << " | Modified";
    }
    
    return ss.str();
}

std::vector<std::string> HexEditor::getAvailableCodepages() const {
    return {
        "437 - MS-DOS Latin US",
        "932 - Japanese Shift JIS",
        "936 - Chinese Simplified (GBK)",
        "949 - Korean",
        "950 - Chinese Traditional (Big5)",
        "1252 - Western European (Latin-1)"
    };
}

std::string HexEditor::getCodepageName(uint16_t codepage) const {
    switch (codepage) {
        case 437: return "MS-DOS Latin US";
        case 932: return "Japanese Shift JIS";
        case 936: return "Chinese Simplified (GBK)";
        case 949: return "Korean";
        case 950: return "Chinese Traditional (Big5)";
        case 1252: return "Western European (Latin-1)";
        default: return "Unknown";
    }
}

bool HexEditor::isTextPrintable(const std::string& text) const {
    for (char c : text) {
        if (!std::isprint(static_cast<unsigned char>(c)) && c != '\n' && c != '\r' && c != '\t') {
            return false;
        }
    }
    return true;
}

//=============================================================================
// Private Helper Methods
//=============================================================================

bool HexEditor::isValidAddress(uint32_t address) const {
    if (!memory_manager_) return false;
    
    return memory_manager_->isValidAddress(current_domain_, address);
}

void HexEditor::updateCursor(uint32_t address, bool on_hex) {
    cursor_address_ = address;
    cursor_on_hex_ = on_hex;
}

void HexEditor::pushUndoOperation(const HexEditOperation& operation) {
    // Clear redo stack when new operation is added
    redo_stack_.clear();
    
    // Add to undo stack
    undo_stack_.push_back(operation);
    
    // Limit undo stack size
    if (undo_stack_.size() > config_.max_undo_operations) {
        undo_stack_.erase(undo_stack_.begin());
    }
}

void HexEditor::invalidateCache(uint32_t address, size_t size) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    for (size_t i = 0; i < size; ++i) {
        data_cache_.erase(address + i);
    }
}

std::vector<uint8_t> HexEditor::getCachedData(uint32_t address, size_t size) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::vector<uint8_t> result;
    result.reserve(size);
    
    for (size_t i = 0; i < size; ++i) {
        auto it = data_cache_.find(address + i);
        if (it != data_cache_.end()) {
            result.push_back(it->second.data[0]);
        } else {
            return {}; // Cache miss, return empty vector
        }
    }
    
    return result;
}

void HexEditor::cacheData(uint32_t address, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < data.size(); ++i) {
        CacheEntry entry;
        entry.data = {data[i]};
        entry.timestamp = now;
        data_cache_[address + i] = entry;
    }
    
    // Limit cache size
    if (data_cache_.size() > config_.cache_size) {
        // Remove oldest entries
        auto oldest = std::min_element(data_cache_.begin(), data_cache_.end(),
                                     [](const auto& a, const auto& b) {
                                         return a.second.timestamp < b.second.timestamp;
                                     });
        if (oldest != data_cache_.end()) {
            data_cache_.erase(oldest);
        }
    }
}

void HexEditor::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    data_cache_.clear();
}

std::vector<uint32_t> HexEditor::searchHexPattern(const std::string& hex_pattern) {
    std::vector<uint8_t> pattern = HexUtils::hexStringToBytes(hex_pattern);
    return findPattern(pattern);
}

std::vector<uint32_t> HexEditor::searchTextPattern(const std::string& text, uint16_t codepage) {
    return findTextPattern(text, codepage);
}

std::string HexEditor::formatAddress(uint32_t address) const {
    return LuaEngineDebugUtils::formatAddress(address);
}

std::string HexEditor::formatHexByte(uint8_t value) const {
    return LuaEngineDebugUtils::formatHexByte(value);
}

char HexEditor::formatAsciiChar(uint8_t value) const {
    return (value >= 32 && value <= 126) ? static_cast<char>(value) : '.';
}

std::string HexEditor::convertBytesToText(const std::vector<uint8_t>& bytes, uint16_t codepage) const {
    std::string result;
    
    if (codepage == 932) {
        // SJIS conversion (simplified)
        for (size_t i = 0; i < bytes.size(); ++i) {
            uint8_t byte = bytes[i];
            if (byte <= 0x7F) {
                // ASCII
                result += static_cast<char>(byte);
            } else if (byte >= 0xA1 && byte <= 0xDF) {
                // Half-width katakana
                result += static_cast<char>(byte);
            } else {
                // Multi-byte or unknown
                result += '.';
            }
        }
    } else {
        // ASCII/other codepages
        for (uint8_t byte : bytes) {
            if (byte >= 32 && byte <= 126) {
                result += static_cast<char>(byte);
            } else {
                result += '.';
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> HexEditor::convertTextToBytes(const std::string& text, uint16_t codepage) const {
    std::vector<uint8_t> result;
    
    // Simple conversion - just convert string to bytes
    for (char c : text) {
        result.push_back(static_cast<uint8_t>(c));
    }
    
    return result;
}

bool HexEditor::exportAsBinary(const std::string& filename) const {
    if (!selection_.active) return false;
    
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    size_t size = selection_.getSize();
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = const_cast<HexEditor*>(this)->readByte(selection_.start_address + i);
        file.write(reinterpret_cast<const char*>(&byte), 1);
    }
    
    file.close();
    return true;
}

bool HexEditor::exportAsC(const std::string& filename) const {
    if (!selection_.active) return false;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << "// Generated by DOSBox-X Hex Editor\n";
    file << "// Address range: " << formatAddress(selection_.start_address) 
         << " - " << formatAddress(selection_.end_address) << "\n\n";
    
    file << "const unsigned char data[] = {\n";
    
    size_t size = selection_.getSize();
    for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) {
            file << "    ";
        }
        
        uint8_t byte = const_cast<HexEditor*>(this)->readByte(selection_.start_address + i);
        file << "0x" << formatHexByte(byte);
        
        if (i < size - 1) {
            file << ",";
        }
        
        if (i % 16 == 15 || i == size - 1) {
            file << "\n";
        } else {
            file << " ";
        }
    }
    
    file << "};\n\n";
    file << "const size_t data_size = " << size << ";\n";
    
    file.close();
    return true;
}

//=============================================================================
// HexEditor Implementation continues
//=============================================================================

bool HexEditor::exportAsHexDump(const std::string& filename) const {
    if (!selection_.active) return false;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    // Write header
    file << "# DOSBox-X Hex Dump\n";
    file << "# Address range: " << formatAddress(selection_.start_address) 
         << " - " << formatAddress(selection_.end_address) << "\n";
    file << "# Size: " << selection_.getSize() << " bytes\n\n";
    
    size_t size = selection_.getSize();
    const size_t bytes_per_line = 16;
    
    for (size_t offset = 0; offset < size; offset += bytes_per_line) {
        uint32_t address = selection_.start_address + offset;
        
        // Address column
        file << formatAddress(address) << ": ";
        
        // Hex column
        size_t line_bytes = std::min(bytes_per_line, size - offset);
        for (size_t i = 0; i < line_bytes; ++i) {
            uint8_t byte = const_cast<HexEditor*>(this)->readByte(address + i);
            file << formatHexByte(byte) << " ";
        }
        
        // Padding for shorter lines
        for (size_t i = line_bytes; i < bytes_per_line; ++i) {
            file << "   ";
        }
        
        // ASCII column
        file << " |";
        for (size_t i = 0; i < line_bytes; ++i) {
            uint8_t byte = const_cast<HexEditor*>(this)->readByte(address + i);
            char ascii_char = formatAsciiChar(byte);
            file << ascii_char;
        }
        file << "|\n";
    }
    
    file.close();
    return true;
}

//=============================================================================
// HexUtils Implementation
//=============================================================================

namespace HexUtils {

std::string bytesToHexString(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (uint8_t byte : bytes) {
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

std::vector<uint8_t> hexStringToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 < hex.length()) {
            std::string byte_str = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            bytes.push_back(byte);
        }
    }
    return bytes;
}

bool isValidHexString(const std::string& hex) {
    if (hex.length() % 2 != 0) return false;
    
    for (char c : hex) {
        if (!std::isxdigit(c)) return false;
    }
    
    return true;
}

std::string formatFileSize(size_t size) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double fsize = static_cast<double>(size);
    
    while (fsize >= 1024.0 && unit < 3) {
        fsize /= 1024.0;
        unit++;
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << fsize << " " << units[unit];
    return ss.str();
}

std::string formatTimestamp(const std::chrono::steady_clock::time_point& time) {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - time);
    return std::to_string(duration.count()) + "s ago";
}

} // namespace HexUtils

} // namespace LuaEngineHexEditor