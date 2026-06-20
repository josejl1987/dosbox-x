#include "debug_logger.h"
#include <imgui/imgui.h>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <set>
#include <atomic>

namespace LuaEngineDebugLogger {

// Global logger instance (owned/managed by LuaEngine initialization)
DebugLogger* g_debug_logger = nullptr;

//=============================================================================
// LogEntry Implementation
//=============================================================================

std::string LogEntry::toString() const {
    std::stringstream ss;
    ss << "[" << getTimestampString() << "] ";
    ss << "[" << getLevelString() << "] ";
    ss << "[" << category << "] ";
    ss << message;
    
    if (!file.empty() && line > 0) {
        ss << " (" << file << ":" << line;
        if (!function.empty()) {
            ss << " in " << function;
        }
        ss << ")";
    }
    
    return ss.str();
}

std::string LogEntry::getTimestampString() const {
    auto time_since_epoch = timestamp.time_since_epoch();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch);
    
    // Use current time for display (simpler approach)
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    std::tm* tm_value = std::localtime(&time_t_value);
    
    std::stringstream ss;
    ss << std::put_time(tm_value, "%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << (milliseconds.count() % 1000);
    
    return ss.str();
}

std::string LogEntry::getLevelString() const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default: return "UNKNOWN";
    }
}

//=============================================================================
// ConsoleLogSink Implementation
//=============================================================================

ConsoleLogSink::ConsoleLogSink(LogLevel min_level, bool use_colors)
    : min_level_(min_level), use_colors_(use_colors) {
}

void ConsoleLogSink::log(const LogEntry& entry) {
    if (entry.level < min_level_) return;
    
    if (use_colors_) {
        std::cout << getColorCode(entry.level);
    }
    
    std::cout << entry.toString();
    
    if (use_colors_) {
        std::cout << getResetCode();
    }
    
    std::cout << std::endl;
}

void ConsoleLogSink::flush() {
    std::cout.flush();
}

std::string ConsoleLogSink::getColorCode(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "\033[90m";    // Dark gray
        case LogLevel::DEBUG: return "\033[36m";    // Cyan
        case LogLevel::INFO: return "\033[32m";     // Green
        case LogLevel::WARNING: return "\033[33m";  // Yellow
        case LogLevel::ERROR: return "\033[31m";    // Red
        case LogLevel::CRITICAL: return "\033[35m"; // Magenta
        default: return "";
    }
}

std::string ConsoleLogSink::getResetCode() const {
    return "\033[0m";
}

//=============================================================================
// FileLogSink Implementation
//=============================================================================

FileLogSink::FileLogSink(const std::string& filename, LogLevel min_level, bool auto_flush)
    : filename_(filename), min_level_(min_level), auto_flush_(auto_flush) {
    file_.open(filename_, std::ios::out | std::ios::app);
    if (file_.is_open()) {
        file_ << "=== Debug Log Started ===" << std::endl;
    }
}

FileLogSink::~FileLogSink() {
    if (file_.is_open()) {
        file_ << "=== Debug Log Ended ===" << std::endl;
        file_.close();
    }
}

void FileLogSink::log(const LogEntry& entry) {
    if (entry.level < min_level_) return;
    
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (file_.is_open()) {
        file_ << entry.toString() << std::endl;
        if (auto_flush_) {
            file_.flush();
        }
    }
}

void FileLogSink::flush() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

//=============================================================================
// MemoryLogSink Implementation
//=============================================================================

MemoryLogSink::MemoryLogSink(size_t max_entries, LogLevel min_level)
    : max_entries_(max_entries), min_level_(min_level) {
    entries_.reserve(max_entries);
}

void MemoryLogSink::log(const LogEntry& entry) {
    if (entry.level < min_level_) return;
    
    std::lock_guard<std::mutex> lock(entries_mutex_);
    
    entries_.push_back(entry);
    
    // Remove oldest entries if we exceed the limit
    while (entries_.size() > max_entries_) {
        entries_.erase(entries_.begin());
    }
}

void MemoryLogSink::flush() {
    // Nothing to flush for memory sink
}

std::vector<LogEntry> MemoryLogSink::getEntries() const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    return entries_;
}

std::vector<LogEntry> MemoryLogSink::getEntriesForLevel(LogLevel level) const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    std::vector<LogEntry> filtered;
    
    for (const auto& entry : entries_) {
        if (entry.level == level) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

std::vector<LogEntry> MemoryLogSink::getEntriesForCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    std::vector<LogEntry> filtered;
    
    for (const auto& entry : entries_) {
        if (entry.category == category) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

void MemoryLogSink::clear() {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    entries_.clear();
}

size_t MemoryLogSink::getEntryCount() const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    return entries_.size();
}

void MemoryLogSink::setMaxEntries(size_t max_entries) {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    max_entries_ = max_entries;
    
    // Trim if necessary
    while (entries_.size() > max_entries_) {
        entries_.erase(entries_.begin());
    }
}

//=============================================================================
// AsyncLogSink Implementation
//=============================================================================

AsyncLogSink::AsyncLogSink(std::unique_ptr<LogSink> sink)
    : wrapped_sink_(std::move(sink)), shutdown_(false) {
    worker_thread_ = std::thread(&AsyncLogSink::workerLoop, this);
}

AsyncLogSink::~AsyncLogSink() {
    shutdown_.store(true);
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void AsyncLogSink::log(const LogEntry& entry) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
    }
    queue_cv_.notify_one();
}

void AsyncLogSink::flush() {
    // Wait for queue to be empty
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return log_queue_.empty() || shutdown_.load(); });
    
    if (wrapped_sink_) {
        wrapped_sink_->flush();
    }
}

void AsyncLogSink::workerLoop() {
    while (!shutdown_.load()) {
        std::queue<LogEntry> local_queue;
        
        // Move entries from main queue to local queue
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !log_queue_.empty() || shutdown_.load(); });
            
            if (shutdown_.load()) break;
            
            local_queue = std::move(log_queue_);
            log_queue_ = std::queue<LogEntry>();
        }
        
        // Process all entries in local queue
        while (!local_queue.empty() && wrapped_sink_) {
            wrapped_sink_->log(local_queue.front());
            local_queue.pop();
        }
    }
}

//=============================================================================
// DebugLogger Implementation
//=============================================================================

DebugLogger::DebugLogger() 
    : global_min_level_(LogLevel::DEBUG), start_time_(std::chrono::steady_clock::now()) {
    
    // Initialize statistics
    for (int i = 0; i < 6; ++i) {
        total_logs_[i].store(0);
    }
}

DebugLogger::~DebugLogger() {
    flush();
    clearSinks();
}

void DebugLogger::addSink(std::unique_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back(std::move(sink));
}

void DebugLogger::removeSink(LogSink* sink) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.erase(
        std::remove_if(sinks_.begin(), sinks_.end(),
                      [sink](const std::unique_ptr<LogSink>& s) { return s.get() == sink; }),
        sinks_.end()
    );
}

void DebugLogger::clearSinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();
}

void DebugLogger::log(LogLevel level, const std::string& category, const std::string& message,
                     const std::string& file, int line, const std::string& function) {
    if (!shouldLog(level, category)) return;
    
    LogEntry entry(level, category, message, file, line, function);
    
    // Update statistics
    total_logs_[static_cast<int>(level)].fetch_add(1);
    
    // Send to all sinks
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
        sink->log(entry);
    }
}

void DebugLogger::trace(const std::string& category, const std::string& message) {
    log(LogLevel::TRACE, category, message);
}

void DebugLogger::debug(const std::string& category, const std::string& message) {
    log(LogLevel::DEBUG, category, message);
}

void DebugLogger::info(const std::string& category, const std::string& message) {
    log(LogLevel::INFO, category, message);
}

void DebugLogger::warning(const std::string& category, const std::string& message) {
    log(LogLevel::WARNING, category, message);
}

void DebugLogger::error(const std::string& category, const std::string& message) {
    log(LogLevel::ERROR, category, message);
}

void DebugLogger::critical(const std::string& category, const std::string& message) {
    log(LogLevel::CRITICAL, category, message);
}

void DebugLogger::addCategoryFilter(const std::string& category) {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    filtered_categories_.insert(category);
}

void DebugLogger::removeCategoryFilter(const std::string& category) {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    filtered_categories_.erase(category);
}

void DebugLogger::clearCategoryFilters() {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    filtered_categories_.clear();
}

bool DebugLogger::isCategoryFiltered(const std::string& category) const {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    return filtered_categories_.find(category) != filtered_categories_.end();
}

size_t DebugLogger::getTotalLogsForLevel(LogLevel level) const {
    return total_logs_[static_cast<int>(level)].load();
}

size_t DebugLogger::getTotalLogs() const {
    size_t total = 0;
    for (int i = 0; i < 6; ++i) {
        total += total_logs_[i].load();
    }
    return total;
}

std::chrono::milliseconds DebugLogger::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
}

void DebugLogger::flush() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
        sink->flush();
    }
}

bool DebugLogger::shouldLog(LogLevel level, const std::string& category) const {
    if (level < global_min_level_) return false;
    if (isCategoryFiltered(category)) return false;
    return true;
}

//=============================================================================
// LogViewerWindow Implementation
//=============================================================================

LogViewerWindow::LogViewerWindow() 
    : memory_sink_(nullptr), show_window_(false), auto_scroll_(true), 
      show_timestamps_(true), show_categories_(true), show_levels_(true),
      filter_level_(LogLevel::TRACE), filter_cache_dirty_(true),
      scroll_position_(0.0f), item_height_(20.0f), visible_items_(0), first_visible_item_(0),
      current_page_(0), entries_per_page_(100) {
    
    category_filter_[0] = '\0';
    message_filter_[0] = '\0';
    
    // Initialize colors for different log levels
    level_colors_[static_cast<int>(LogLevel::TRACE)] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);    // Gray
    level_colors_[static_cast<int>(LogLevel::DEBUG)] = ImVec4(0.0f, 0.8f, 0.8f, 1.0f);    // Cyan
    level_colors_[static_cast<int>(LogLevel::INFO)] = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);     // Green
    level_colors_[static_cast<int>(LogLevel::WARNING)] = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
    level_colors_[static_cast<int>(LogLevel::ERROR)] = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);    // Red
    level_colors_[static_cast<int>(LogLevel::CRITICAL)] = ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta
}

LogViewerWindow::~LogViewerWindow() {
}

void LogViewerWindow::initialize(MemoryLogSink* memory_sink) {
    memory_sink_ = memory_sink;
}

void LogViewerWindow::render() {
    if (!show_window_ || !memory_sink_) return;
    
    if (ImGui::Begin("Debug Log Viewer", &show_window_, ImGuiWindowFlags_MenuBar)) {
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Timestamps", nullptr, &show_timestamps_);
                ImGui::MenuItem("Show Categories", nullptr, &show_categories_);
                ImGui::MenuItem("Show Levels", nullptr, &show_levels_);
                ImGui::MenuItem("Auto Scroll", nullptr, &auto_scroll_);
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Actions")) {
                if (ImGui::MenuItem("Clear Logs")) {
                    clearLogs();
                }
                if (ImGui::MenuItem("Jump to Bottom")) {
                    jumpToBottom();
                }
                ImGui::EndMenu();
            }
            
            ImGui::EndMenuBar();
        }
        
        renderFilterControls();
        ImGui::Separator();
        renderLogEntries();
        ImGui::Separator();
        renderStatusBar();
    }
    ImGui::End();
}

void LogViewerWindow::renderFilterControls() {
    // Log level filter
    ImGui::Text("Min Level:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    const char* level_names[] = {"TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};
    int current_level = static_cast<int>(filter_level_);
    if (ImGui::Combo("##MinLevel", &current_level, level_names, IM_ARRAYSIZE(level_names))) {
        filter_level_ = static_cast<LogLevel>(current_level);
    }
    
    ImGui::SameLine();
    
    // Category filter
    ImGui::Text("Category:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##CategoryFilter", category_filter_, sizeof(category_filter_));
    
    ImGui::SameLine();
    
    // Message filter
    ImGui::Text("Message:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##MessageFilter", message_filter_, sizeof(message_filter_));
    
    ImGui::SameLine();
    
    if (ImGui::Button("Clear Filters")) {
        filter_level_ = LogLevel::TRACE;
        category_filter_[0] = '\0';
        message_filter_[0] = '\0';
    }
}

void LogViewerWindow::renderLogEntries() {
    // Use virtual scrolling for better performance
    renderLogEntriesVirtual();
}

void LogViewerWindow::renderLogEntry(const LogEntry& entry, int index) {
    ImGui::TableNextRow();
    ImGui::PushID(index);
    
    ImVec4 level_color = level_colors_[static_cast<int>(entry.level)];
    
    // Timestamp
    ImGui::TableNextColumn();
    if (show_timestamps_) {
        ImGui::TextColored(level_color, "%s", entry.getTimestampString().c_str());
    }
    
    // Level
    ImGui::TableNextColumn();
    if (show_levels_) {
        ImGui::TextColored(level_color, "%s", entry.getLevelString().c_str());
    }
    
    // Category
    ImGui::TableNextColumn();
    if (show_categories_) {
        ImGui::TextColored(level_color, "%s", entry.category.c_str());
    }
    
    // Message
    ImGui::TableNextColumn();
    ImGui::TextColored(level_color, "%s", entry.message.c_str());
    
    // Context menu
    if (ImGui::BeginPopupContextItem("DebugLoggerContextMenu")) {
        if (ImGui::MenuItem("Copy Message")) {
            if (!entry.message.empty()) {
                ImGui::SetClipboardText(entry.message.c_str());
            }
        }
        if (ImGui::MenuItem("Copy Full Line")) {
            std::string full_line = entry.toString();
            if (!full_line.empty()) {
                ImGui::SetClipboardText(full_line.c_str());
            }
        }
        ImGui::EndPopup();
    }
    
    ImGui::PopID();
}

void LogViewerWindow::renderStatusBar() {
    if (memory_sink_) {
        size_t total_entries = memory_sink_->getEntryCount();
        size_t max_entries = memory_sink_->getMaxEntries();
        
        ImGui::Text("Entries: %zu/%zu", total_entries, max_entries);
        
        if (g_debug_logger) {
            ImGui::SameLine();
            ImGui::Text("| Total Logs: %zu", g_debug_logger->getTotalLogs());
            ImGui::SameLine();
            ImGui::Text("| Uptime: %lld ms", g_debug_logger->getUptime().count());
        }
    }
}

void LogViewerWindow::jumpToBottom() {
    if (memory_sink_) {
        size_t total_entries = memory_sink_->getEntryCount();
        size_t total_pages = (total_entries + entries_per_page_ - 1) / entries_per_page_;
        if (total_pages > 0) {
            current_page_ = total_pages - 1;
        }
    }
}

void LogViewerWindow::clearLogs() {
    if (memory_sink_) {
        memory_sink_->clear();
        current_page_ = 0;
    }
}

void LogViewerWindow::scrollToTop() {
    current_page_ = 0;
    scroll_position_ = 0.0f;
    first_visible_item_ = 0;
}

void LogViewerWindow::scrollToBottom() {
    jumpToBottom();
}

void LogViewerWindow::invalidateFilterCache() {
    filter_cache_dirty_ = true;
}

const std::vector<LogEntry>& LogViewerWindow::getFilteredEntries() const {
    if (filter_cache_dirty_) {
        updateFilteredEntriesCache();
    }
    return filtered_entries_cache_;
}

void LogViewerWindow::updateFilteredEntriesCache() const {
    if (!memory_sink_) return;
    
    auto entries = memory_sink_->getEntries();
    
    // Clear and reserve space
    filtered_entries_cache_.clear();
    filtered_entries_cache_.reserve(entries.size());
    
    // Apply filters
    std::string category_filter = category_filter_;
    std::string message_filter = message_filter_;
    std::transform(category_filter.begin(), category_filter.end(), category_filter.begin(), ::tolower);
    std::transform(message_filter.begin(), message_filter.end(), message_filter.begin(), ::tolower);
    
    for (const auto& entry : entries) {
        if (entry.level < filter_level_) continue;
        
        if (!category_filter.empty()) {
            std::string entry_category = entry.category;
            std::transform(entry_category.begin(), entry_category.end(), entry_category.begin(), ::tolower);
            if (entry_category.find(category_filter) == std::string::npos) continue;
        }
        
        if (!message_filter.empty()) {
            std::string entry_message = entry.message;
            std::transform(entry_message.begin(), entry_message.end(), entry_message.begin(), ::tolower);
            if (entry_message.find(message_filter) == std::string::npos) continue;
        }
        
        filtered_entries_cache_.push_back(entry);
    }
    
    // Update cache state
    filter_cache_dirty_ = false;
    last_category_filter_ = category_filter_;
    last_message_filter_ = message_filter_;
    last_filter_level_ = filter_level_;
}

void LogViewerWindow::calculateVisibleRange() {
    // Calculate visible range based on scroll position and item height
    float visible_height = ImGui::GetContentRegionAvail().y;
    visible_items_ = static_cast<size_t>(visible_height / item_height_) + 2; // +2 for partial items
    first_visible_item_ = static_cast<size_t>(scroll_position_ / item_height_);
}

void LogViewerWindow::renderLogEntriesVirtual() {
    const auto& filtered_entries = getFilteredEntries();
    
    if (filtered_entries.empty()) {
        ImGui::Text("No log entries to display.");
        return;
    }
    
    calculateVisibleRange();
    
    // Calculate total height for scrolling
    float total_height = static_cast<float>(filtered_entries.size()) * item_height_;
    
    // Create invisible button for scrolling
    ImGui::InvisibleButton("scroll_area", ImVec2(0, total_height));
    
    // Get current scroll position
    scroll_position_ = ImGui::GetScrollY();
    
    // Calculate visible range
    size_t start_idx = std::min(first_visible_item_, filtered_entries.size());
    size_t end_idx = std::min(start_idx + visible_items_, filtered_entries.size());
    
    // Render visible entries
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + static_cast<float>(start_idx) * item_height_);
    
    for (size_t i = start_idx; i < end_idx; ++i) {
        renderLogEntry(filtered_entries[i], static_cast<int>(i));
    }
    
    // Auto-scroll to bottom if enabled
    if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
}

} // namespace LuaEngineDebugLogger
