#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <fstream>
#include <chrono>
#include <functional>
#include <sstream>
#include <queue>
#include <atomic>
#include <set>
#include <thread>
#include <condition_variable>
#include <imgui/imgui.h>

namespace LuaEngineDebugLogger {

#undef ERROR   // Avoid conflict with other libraries

class DebugLogger;
// Global logger pointer managed by LuaEngine
extern DebugLogger* g_debug_logger;

// Log levels
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

// Log entry structure
struct LogEntry {
    std::chrono::steady_clock::time_point timestamp;
    LogLevel level;
    std::string category;
    std::string message;
    std::string file;
    int line;
    std::string function;
    
    LogEntry(LogLevel lvl, const std::string& cat, const std::string& msg,
             const std::string& f = "", int l = 0, const std::string& func = "")
        : timestamp(std::chrono::steady_clock::now()), level(lvl), category(cat), 
          message(msg), file(f), line(l), function(func) {}
    
    std::string toString() const;
    std::string getTimestampString() const;
    std::string getLevelString() const;
};

// Log sink interface
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void log(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

// Console log sink
class ConsoleLogSink : public LogSink {
private:
    LogLevel min_level_;
    bool use_colors_;
    
public:
    ConsoleLogSink(LogLevel min_level = LogLevel::INFO, bool use_colors = true);
    void log(const LogEntry& entry) override;
    void flush() override;
    
private:
    std::string getColorCode(LogLevel level) const;
    std::string getResetCode() const;
};

// File log sink
class FileLogSink : public LogSink {
private:
    std::string filename_;
    std::ofstream file_;
    LogLevel min_level_;
    bool auto_flush_;
    mutable std::mutex file_mutex_;
    
public:
    FileLogSink(const std::string& filename, LogLevel min_level = LogLevel::DEBUG, bool auto_flush = true);
    ~FileLogSink();
    
    void log(const LogEntry& entry) override;
    void flush() override;
    bool isOpen() const { return file_.is_open(); }
};

// Memory log sink (for UI display)
class MemoryLogSink : public LogSink {
private:
    std::vector<LogEntry> entries_;
    size_t max_entries_;
    LogLevel min_level_;
    mutable std::mutex entries_mutex_;
    
public:
    MemoryLogSink(size_t max_entries = 1000, LogLevel min_level = LogLevel::DEBUG);
    
    void log(const LogEntry& entry) override;
    void flush() override;
    
    std::vector<LogEntry> getEntries() const;
    std::vector<LogEntry> getEntriesForLevel(LogLevel level) const;
    std::vector<LogEntry> getEntriesForCategory(const std::string& category) const;
    void clear();
    
    size_t getEntryCount() const;
    size_t getMaxEntries() const { return max_entries_; }
    void setMaxEntries(size_t max_entries);
};

// Async log sink wrapper
class AsyncLogSink : public LogSink {
private:
    std::unique_ptr<LogSink> wrapped_sink_;
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> shutdown_;
    
    void workerLoop();
    
public:
    AsyncLogSink(std::unique_ptr<LogSink> sink);
    ~AsyncLogSink();
    
    void log(const LogEntry& entry) override;
    void flush() override;
};

// Main debug logger
class DebugLogger {
private:
    std::vector<std::unique_ptr<LogSink>> sinks_;
    LogLevel global_min_level_;
    mutable std::mutex sinks_mutex_;
    
    // Statistics
    std::atomic<size_t> total_logs_[6]; // One for each log level
    std::chrono::steady_clock::time_point start_time_;
    
    // Filtering
    std::set<std::string> filtered_categories_;
    mutable std::mutex filter_mutex_;
    
public:
    DebugLogger();
    ~DebugLogger();
    
    // Sink management
    void addSink(std::unique_ptr<LogSink> sink);
    void removeSink(LogSink* sink);
    void clearSinks();
    
    // Logging methods
    void log(LogLevel level, const std::string& category, const std::string& message,
             const std::string& file = "", int line = 0, const std::string& function = "");
    
    void trace(const std::string& category, const std::string& message);
    void debug(const std::string& category, const std::string& message);
    void info(const std::string& category, const std::string& message);
    void warning(const std::string& category, const std::string& message);
    void error(const std::string& category, const std::string& message);
    void critical(const std::string& category, const std::string& message);
    
    // Template methods for formatted logging
    template<typename... Args>
    void logf(LogLevel level, const std::string& category, const std::string& format, Args&&... args) {
        std::ostringstream oss;
        logf_impl(oss, format, std::forward<Args>(args)...);
        log(level, category, oss.str());
    }
    
    template<typename... Args>
    void tracef(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::TRACE, category, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void debugf(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::DEBUG, category, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void infof(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::INFO, category, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warningf(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::WARNING, category, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void errorf(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::ERROR, category, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void criticalf(const std::string& category, const std::string& format, Args&&... args) {
        logf(LogLevel::CRITICAL, category, format, std::forward<Args>(args)...);
    }
    
    // Configuration
    void setGlobalMinLevel(LogLevel level) { global_min_level_ = level; }
    LogLevel getGlobalMinLevel() const { return global_min_level_; }
    
    // Filtering
    void addCategoryFilter(const std::string& category);
    void removeCategoryFilter(const std::string& category);
    void clearCategoryFilters();
    bool isCategoryFiltered(const std::string& category) const;
    
    // Statistics
    size_t getTotalLogsForLevel(LogLevel level) const;
    size_t getTotalLogs() const;
    std::chrono::milliseconds getUptime() const;
    
    // Utility
    void flush();
    
private:
    template<typename T>
    void logf_impl(std::ostringstream& oss, const std::string& format, T&& value) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            oss << format.substr(0, pos) << value << format.substr(pos + 2);
        } else {
            oss << format;
        }
    }
    
    template<typename T, typename... Args>
    void logf_impl(std::ostringstream& oss, const std::string& format, T&& value, Args&&... args) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            std::string remaining = format.substr(pos + 2);
            oss << format.substr(0, pos) << value;
            logf_impl(oss, remaining, std::forward<Args>(args)...);
        } else {
            oss << format;
        }
    }
    
    bool shouldLog(LogLevel level, const std::string& category) const;
};

// Log viewer window for ImGui
class LogViewerWindow {
private:
    MemoryLogSink* memory_sink_;
    bool show_window_;
    bool auto_scroll_;
    bool show_timestamps_;
    bool show_categories_;
    bool show_levels_;
    
    // Filtering
    LogLevel filter_level_;
    char category_filter_[256];
    char message_filter_[256];
    
    // Virtual scrolling optimization
    mutable std::vector<LogEntry> filtered_entries_cache_;
    mutable bool filter_cache_dirty_;
    mutable std::string last_category_filter_;
    mutable std::string last_message_filter_;
    mutable LogLevel last_filter_level_;
    
    // Virtual scrolling state
    float scroll_position_;
    float item_height_;
    size_t visible_items_;
    size_t first_visible_item_;
    
    // Pagination support
    size_t current_page_;
    size_t entries_per_page_;
    
    // Colors for different log levels
    ImVec4 level_colors_[6];
    
    void renderFilterControls();
    void renderLogEntries();
    void renderLogEntriesVirtual();  // Virtual scrolling implementation
    void renderLogEntry(const LogEntry& entry, int index);
    void renderStatusBar();
    
    // Virtual scrolling helpers
    const std::vector<LogEntry>& getFilteredEntries() const;
    void updateFilteredEntriesCache() const;
    void calculateVisibleRange();
    
public:
    LogViewerWindow();
    ~LogViewerWindow();
    
    void initialize(MemoryLogSink* memory_sink);
    void render();
    
    void show() { show_window_ = true; }
    void hide() { show_window_ = false; }
    bool isVisible() const { return show_window_; }
    
    void setAutoScroll(bool auto_scroll) { auto_scroll_ = auto_scroll; }
    void jumpToBottom();
    void clearLogs();
    
    // Virtual scrolling control
    void scrollToTop();
    void scrollToBottom();
    void invalidateFilterCache();
};

} // namespace LuaEngineDebugLogger

#endif // DEBUG_LOGGER_H
