#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <atomic>

namespace LuaEngineFrameControl {

// Frame speed modes
enum class SpeedMode {
    NORMAL,          // Normal emulation speed
    NOTHROTTLE,      // No speed limiting
    TURBO,           // 2x speed
    MAXIMUM,         // Unlimited speed
    CUSTOM,          // Custom speed multiplier
    FRAME_BY_FRAME   // Manual frame stepping
};

// Frame timing statistics
struct FrameTimingStats {
    uint64_t frame_count;
    uint64_t lag_frame_count;
    uint64_t input_frame_count;
    double average_fps;
    double target_fps;
    double actual_fps;
    uint64_t total_time_us;
    uint64_t frame_time_us;
    uint64_t render_time_us;
    uint64_t cpu_time_us;
    bool is_lag_frame;
    uint64_t frame_skip_count;
    double speed_multiplier;
};

// Frame callback types
using FrameStartCallback = std::function<void(uint64_t frame_number)>;
using FrameEndCallback = std::function<void(uint64_t frame_number, const FrameTimingStats& stats)>;
using LagFrameCallback = std::function<void(uint64_t frame_number)>;
using SpeedChangeCallback = std::function<void(SpeedMode old_mode, SpeedMode new_mode)>;

// Advanced frame controller
class FrameController {
private:
    // Frame state
    std::atomic<uint64_t> current_frame_;
    std::atomic<uint64_t> target_frame_;
    std::atomic<bool> paused_;
    std::atomic<bool> frame_stepping_;
    std::atomic<bool> frame_advance_requested_;
    
    // Speed control
    SpeedMode current_speed_mode_;
    double custom_speed_multiplier_;
    std::atomic<bool> throttle_enabled_;
    
    // Timing
    std::chrono::high_resolution_clock::time_point frame_start_time_;
    std::chrono::high_resolution_clock::time_point last_frame_time_;
    double target_frame_time_us_;
    FrameTimingStats current_stats_;
    
    // Lag frame detection
    std::atomic<bool> lag_frame_detection_enabled_;
    uint64_t last_input_frame_;
    std::vector<bool> lag_frame_history_;
    size_t lag_frame_history_size_;
    
    // Frame rate limiting
    bool vsync_enabled_;
    double max_fps_;
    double min_fps_;
    std::chrono::high_resolution_clock::time_point last_throttle_time_;
    
    // Callbacks
    std::vector<FrameStartCallback> frame_start_callbacks_;
    std::vector<FrameEndCallback> frame_end_callbacks_;
    std::vector<LagFrameCallback> lag_frame_callbacks_;
    std::vector<SpeedChangeCallback> speed_change_callbacks_;
    
    // Performance tracking
    std::vector<double> frame_time_history_;
    std::vector<double> fps_history_;
    size_t history_size_;
    uint64_t last_stats_update_;
    
    // Circular buffer indices for performance optimization
    size_t lag_frame_history_index_;
    size_t frame_time_history_index_;
    size_t fps_history_index_;
    
    // Frame skipping
    std::atomic<uint32_t> frame_skip_ratio_;
    std::atomic<uint64_t> frames_to_skip_;
    bool adaptive_frame_skip_;
    
public:
    FrameController();
    ~FrameController();
    
    // Initialization and shutdown
    bool initialize();
    void shutdown();
    
    // Frame control
    void beginFrame();
    void endFrame();
    bool shouldSkipFrame() const;
    void waitForNextFrame();
    
    // Speed control
    void setSpeedMode(SpeedMode mode);
    SpeedMode getSpeedMode() const;
    void setCustomSpeedMultiplier(double multiplier);
    double getCustomSpeedMultiplier() const;
    void setThrottleEnabled(bool enabled);
    bool isThrottleEnabled() const;
    
    // Frame stepping
    void pause();
    void resume();
    bool isPaused() const;
    void enableFrameStepping(bool enabled);
    bool isFrameStepping() const;
    void advanceFrame();
    void advanceFrames(uint64_t count);
    void seekToFrame(uint64_t frame);
    
    // Frame information
    uint64_t getCurrentFrame() const;
    uint64_t getTargetFrame() const;
    FrameTimingStats getFrameStats() const;
    
    // Lag frame detection
    void setLagFrameDetectionEnabled(bool enabled);
    bool isLagFrameDetectionEnabled() const;
    void markInputFrame();
    void markLagFrame();
    bool isLagFrame(uint64_t frame) const;
    uint64_t getLagFrameCount() const;
    double getLagFramePercentage() const;
    
    // Frame rate limiting
    void setVSyncEnabled(bool enabled);
    bool isVSyncEnabled() const;
    void setMaxFPS(double fps);
    double getMaxFPS() const;
    void setMinFPS(double fps);
    double getMinFPS() const;
    void setTargetFPS(double fps);
    double getTargetFPS() const;
    
    // Frame skipping
    void setFrameSkipRatio(uint32_t ratio);
    uint32_t getFrameSkipRatio() const;
    void setAdaptiveFrameSkip(bool enabled);
    bool isAdaptiveFrameSkip() const;
    
    // Callbacks
    void addFrameStartCallback(FrameStartCallback callback);
    void addFrameEndCallback(FrameEndCallback callback);
    void addLagFrameCallback(LagFrameCallback callback);
    void addSpeedChangeCallback(SpeedChangeCallback callback);
    void clearCallbacks();
    
    // Performance analysis
    std::vector<double> getFrameTimeHistory() const;
    std::vector<double> getFPSHistory() const;
    double getAverageFPS(uint64_t frames = 60) const;
    double getAverageFrameTime(uint64_t frames = 60) const;
    std::string getPerformanceReport() const;
    
    // Frame range analysis
    struct FrameRangeStats {
        uint64_t start_frame;
        uint64_t end_frame;
        uint64_t total_frames;
        uint64_t lag_frames;
        double average_fps;
        double min_fps;
        double max_fps;
        uint64_t total_time_ms;
    };
    
    FrameRangeStats analyzeFrameRange(uint64_t start_frame, uint64_t end_frame) const;
    
    // Timing utilities
    void resetFrameCounter();
    void setFrameCounter(uint64_t frame);
    std::chrono::microseconds getFrameTimeTarget() const;
    std::chrono::microseconds getActualFrameTime() const;
    
private:
    // Internal helpers
    void updateFrameStats();
    void updateSpeedMultiplier();
    void handleFrameSkipping();
    void detectLagFrame();
    void callFrameStartCallbacks();
    void callFrameEndCallbacks();
    void callLagFrameCallbacks();
    void callSpeedChangeCallbacks(SpeedMode old_mode, SpeedMode new_mode);
    
    // Timing helpers
    void sleepUntilNextFrame();
    std::chrono::microseconds calculateFrameDelay() const;
    bool shouldThrottleFrame() const;
    
    // Statistics helpers
    void addFrameTimeToHistory(double frame_time);
    void addFPSToHistory(double fps);
    void trimHistory();
};

// Global frame controller instance - managed by LuaEngine
// This pointer is set by LuaEngine to point to its member
extern FrameController* g_frame_controller;

// Utility functions for DOSBox-X integration
namespace DOSBoxIntegration {
    // Initialize frame controller with DOSBox-X
    bool initializeFrameControl();
    void shutdownFrameControl();
    
    // Hook into DOSBox-X timing
    void onFrameStart();
    void onFrameEnd();
    void onCPUCycleComplete();
    void onRenderComplete();
    
    // Speed control integration
    void setDOSBoxSpeed(double multiplier);
    double getDOSBoxSpeed();
    void pauseDOSBox();
    void resumeDOSBox();
    
    // Lag frame detection for DOS games
    void checkInputActivity();
    void checkScreenUpdates();
    void checkAudioActivity();
    
    // Performance monitoring
    struct DOSBoxPerformance {
        double cpu_usage_percent;
        double gpu_usage_percent;
        uint64_t memory_usage_kb;
        double emulation_efficiency;
        uint64_t cycles_per_second;
    };
    
    DOSBoxPerformance getPerformanceMetrics();
}

// BizHawk-compatible timing functions
namespace BizHawkCompat {
    // Frame control
    void pauseEmulation();
    void unpauseEmulation();
    bool isEmulationPaused();
    void frameAdvance();
    void seekFrame(uint64_t frame);
    
    // Speed control
    void setSpeedPercent(int percent);
    int getSpeedPercent();
    void setMinimumSpeedPercent(int percent);
    void setMaximumSpeedPercent(int percent);
    
    // Frame information
    uint64_t getFrameCount();
    uint64_t getLagCount();
    bool isLagged();
    bool isLagged(uint64_t frame);
    
    // Timing utilities
    double getFPS();
    double getTargetFPS();
    std::string getSystemId();
    std::string getDisplayType();
    
    // Frame rate control
    void throttleSpeed(bool enabled);
    void setFrameSkip(int skip);
    int getFrameSkip();
}

} // namespace LuaEngineFrameControl