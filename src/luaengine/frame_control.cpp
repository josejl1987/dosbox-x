#include "frame_control.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <thread>
#include <cmath>

// DOSBox-X includes
#include "dosbox.h" // Bitu type needed by video.h included from sdlmain.h
#include "sdlmain.h"
#include "pic.h"
#include "setup.h"

namespace LuaEngineFrameControl {

// Global frame controller instance - now managed by LuaEngine
// This is initialized in LuaEngine::LUAENGINE_Init()
FrameController* g_frame_controller = nullptr;

// FrameController implementation
FrameController::FrameController() 
    : current_frame_(0), target_frame_(0), paused_(false), frame_stepping_(false),
      frame_advance_requested_(false), current_speed_mode_(SpeedMode::NORMAL),
      custom_speed_multiplier_(1.0), throttle_enabled_(true),
      target_frame_time_us_(16666.67), // ~60 FPS
      lag_frame_detection_enabled_(true), last_input_frame_(0),
      lag_frame_history_size_(1000), vsync_enabled_(false),
      max_fps_(1000.0), min_fps_(1.0), frame_skip_ratio_(0),
      frames_to_skip_(0), adaptive_frame_skip_(false),
      history_size_(300), last_stats_update_(0),
      lag_frame_history_index_(0), frame_time_history_index_(0), fps_history_index_(0) {
    
    lag_frame_history_.reserve(lag_frame_history_size_);
    frame_time_history_.reserve(history_size_);
    fps_history_.reserve(history_size_);
    
    // Initialize timing
    frame_start_time_ = std::chrono::high_resolution_clock::now();
    last_frame_time_ = frame_start_time_;
    last_throttle_time_ = frame_start_time_;
    
    // Initialize stats
    current_stats_ = {};
    current_stats_.target_fps = 60.0;
    current_stats_.speed_multiplier = 1.0;
}

FrameController::~FrameController() {
    shutdown();
}

bool FrameController::initialize() {
    std::cout << "[FrameControl] Initializing frame controller" << std::endl;
    
    // Reset state
    current_frame_ = 0;
    target_frame_ = 0;
    paused_ = false;
    frame_stepping_ = false;
    
    // Set default target FPS (60 FPS = ~16.67ms per frame)
    setTargetFPS(60.0);
    
    return true;
}

void FrameController::shutdown() {
    clearCallbacks();
    std::cout << "[FrameControl] Frame controller shutdown" << std::endl;
}

void FrameController::beginFrame() {
    frame_start_time_ = std::chrono::high_resolution_clock::now();
    current_frame_++;
    
    // Update frame statistics
    updateFrameStats();
    
    // Handle frame skipping
    handleFrameSkipping();
    
    // Call frame start callbacks
    callFrameStartCallbacks();
    
    // Detect lag frames
    if (lag_frame_detection_enabled_) {
        detectLagFrame();
    }
}

void FrameController::endFrame() {
    auto frame_end_time = std::chrono::high_resolution_clock::now();
    auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        frame_end_time - frame_start_time_);
    
    // Update timing statistics
    current_stats_.frame_time_us = frame_duration.count();
    current_stats_.actual_fps = 1000000.0 / frame_duration.count();
    
    // Add to history
    addFrameTimeToHistory(static_cast<double>(frame_duration.count()));
    addFPSToHistory(current_stats_.actual_fps);
    
    // Call frame end callbacks
    callFrameEndCallbacks();
    
    // Wait for next frame if throttling is enabled
    if (throttle_enabled_ && current_speed_mode_ != SpeedMode::MAXIMUM) {
        waitForNextFrame();
    }
    
    last_frame_time_ = frame_end_time;
}

bool FrameController::shouldSkipFrame() const {
    if (frames_to_skip_ > 0) {
        return true;
    }
    
    if (frame_skip_ratio_ > 0) {
        return (current_frame_ % (frame_skip_ratio_ + 1)) != 0;
    }
    
    return false;
}

void FrameController::waitForNextFrame() {
    if (current_speed_mode_ == SpeedMode::MAXIMUM || !throttle_enabled_) {
        return;
    }
    
    auto target_delay = calculateFrameDelay();
    if (target_delay.count() > 0) {
        sleepUntilNextFrame();
    }
}

void FrameController::setSpeedMode(SpeedMode mode) {
    SpeedMode old_mode = current_speed_mode_;
    current_speed_mode_ = mode;
    
    updateSpeedMultiplier();
    callSpeedChangeCallbacks(old_mode, mode);
    
    std::cout << "[FrameControl] Speed mode changed to: " << static_cast<int>(mode) << std::endl;
}

SpeedMode FrameController::getSpeedMode() const {
    return current_speed_mode_;
}

void FrameController::setCustomSpeedMultiplier(double multiplier) {
    custom_speed_multiplier_ = std::max(0.1, std::min(10.0, multiplier));
    if (current_speed_mode_ == SpeedMode::CUSTOM) {
        updateSpeedMultiplier();
    }
}

double FrameController::getCustomSpeedMultiplier() const {
    return custom_speed_multiplier_;
}

void FrameController::setThrottleEnabled(bool enabled) {
    throttle_enabled_ = enabled;
}

bool FrameController::isThrottleEnabled() const {
    return throttle_enabled_;
}

void FrameController::pause() {
    paused_ = true;
}

void FrameController::resume() {
    paused_ = false;
}

bool FrameController::isPaused() const {
    return paused_;
}

void FrameController::enableFrameStepping(bool enabled) {
    frame_stepping_ = enabled;
    if (enabled) {
        paused_ = true;
    }
}

bool FrameController::isFrameStepping() const {
    return frame_stepping_;
}

void FrameController::advanceFrame() {
    if (frame_stepping_) {
        frame_advance_requested_ = true;
        paused_ = false;
        // Will pause again after one frame in beginFrame()
    }
}

void FrameController::advanceFrames(uint64_t count) {
    target_frame_ = current_frame_ + count;
    if (frame_stepping_) {
        paused_ = false;
    }
}

void FrameController::seekToFrame(uint64_t frame) {
    target_frame_ = frame;
    // This would typically require save state functionality
    std::cout << "[FrameControl] Seek to frame " << frame << " requested" << std::endl;
}

uint64_t FrameController::getCurrentFrame() const {
    return current_frame_;
}

uint64_t FrameController::getTargetFrame() const {
    return target_frame_;
}

FrameTimingStats FrameController::getFrameStats() const {
    return current_stats_;
}

void FrameController::setLagFrameDetectionEnabled(bool enabled) {
    lag_frame_detection_enabled_ = enabled;
}

bool FrameController::isLagFrameDetectionEnabled() const {
    return lag_frame_detection_enabled_;
}

void FrameController::markInputFrame() {
    last_input_frame_ = current_frame_;
    current_stats_.input_frame_count++;
}

void FrameController::markLagFrame() {
    current_stats_.lag_frame_count++;
    current_stats_.is_lag_frame = true;
    
    // Add to lag frame history using circular buffer
    if (lag_frame_history_.size() < lag_frame_history_size_) {
        lag_frame_history_.push_back(true);
    } else {
        lag_frame_history_[lag_frame_history_index_] = true;
        lag_frame_history_index_ = (lag_frame_history_index_ + 1) % lag_frame_history_size_;
    }
    
    callLagFrameCallbacks();
}

bool FrameController::isLagFrame(uint64_t frame) const {
    if (frame >= current_frame_) {
        return current_stats_.is_lag_frame;
    }
    
    // Check history for past frames
    uint64_t history_index = current_frame_ - frame;
    if (history_index < lag_frame_history_.size()) {
        return lag_frame_history_[lag_frame_history_.size() - 1 - history_index];
    }
    
    return false;
}

uint64_t FrameController::getLagFrameCount() const {
    return current_stats_.lag_frame_count;
}

double FrameController::getLagFramePercentage() const {
    if (current_stats_.frame_count == 0) return 0.0;
    return (static_cast<double>(current_stats_.lag_frame_count) / current_stats_.frame_count) * 100.0;
}

void FrameController::setVSyncEnabled(bool enabled) {
    vsync_enabled_ = enabled;
}

bool FrameController::isVSyncEnabled() const {
    return vsync_enabled_;
}

void FrameController::setMaxFPS(double fps) {
    max_fps_ = std::max(1.0, fps);
}

double FrameController::getMaxFPS() const {
    return max_fps_;
}

void FrameController::setMinFPS(double fps) {
    min_fps_ = std::max(0.1, fps);
}

double FrameController::getMinFPS() const {
    return min_fps_;
}

void FrameController::setTargetFPS(double fps) {
    current_stats_.target_fps = std::max(1.0, fps);
    target_frame_time_us_ = 1000000.0 / current_stats_.target_fps;
}

double FrameController::getTargetFPS() const {
    return current_stats_.target_fps;
}

void FrameController::setFrameSkipRatio(uint32_t ratio) {
    frame_skip_ratio_ = ratio;
}

uint32_t FrameController::getFrameSkipRatio() const {
    return frame_skip_ratio_;
}

void FrameController::setAdaptiveFrameSkip(bool enabled) {
    adaptive_frame_skip_ = enabled;
}

bool FrameController::isAdaptiveFrameSkip() const {
    return adaptive_frame_skip_;
}

void FrameController::addFrameStartCallback(FrameStartCallback callback) {
    frame_start_callbacks_.push_back(callback);
}

void FrameController::addFrameEndCallback(FrameEndCallback callback) {
    frame_end_callbacks_.push_back(callback);
}

void FrameController::addLagFrameCallback(LagFrameCallback callback) {
    lag_frame_callbacks_.push_back(callback);
}

void FrameController::addSpeedChangeCallback(SpeedChangeCallback callback) {
    speed_change_callbacks_.push_back(callback);
}

void FrameController::clearCallbacks() {
    frame_start_callbacks_.clear();
    frame_end_callbacks_.clear();
    lag_frame_callbacks_.clear();
    speed_change_callbacks_.clear();
}

std::vector<double> FrameController::getFrameTimeHistory() const {
    return frame_time_history_;
}

std::vector<double> FrameController::getFPSHistory() const {
    return fps_history_;
}

double FrameController::getAverageFPS(uint64_t frames) const {
    if (fps_history_.empty()) return 0.0;
    
    size_t count = std::min(static_cast<size_t>(frames), fps_history_.size());
    double sum = 0.0;
    
    for (size_t i = fps_history_.size() - count; i < fps_history_.size(); i++) {
        sum += fps_history_[i];
    }
    
    return sum / count;
}

double FrameController::getAverageFrameTime(uint64_t frames) const {
    if (frame_time_history_.empty()) return 0.0;
    
    size_t count = std::min(static_cast<size_t>(frames), frame_time_history_.size());
    double sum = 0.0;
    
    for (size_t i = frame_time_history_.size() - count; i < frame_time_history_.size(); i++) {
        sum += frame_time_history_[i];
    }
    
    return sum / count;
}

std::string FrameController::getPerformanceReport() const {
    std::ostringstream oss;
    oss << "Frame Control Performance Report:\n";
    oss << "  Current Frame: " << current_frame_ << "\n";
    oss << "  Target FPS: " << current_stats_.target_fps << "\n";
    oss << "  Actual FPS: " << current_stats_.actual_fps << "\n";
    oss << "  Average FPS (60 frames): " << getAverageFPS(60) << "\n";
    oss << "  Speed Mode: " << static_cast<int>(current_speed_mode_) << "\n";
    oss << "  Speed Multiplier: " << current_stats_.speed_multiplier << "\n";
    oss << "  Lag Frame Count: " << current_stats_.lag_frame_count << "\n";
    oss << "  Lag Frame Percentage: " << getLagFramePercentage() << "%\n";
    oss << "  Frame Skip Ratio: " << frame_skip_ratio_ << "\n";
    oss << "  Throttle Enabled: " << (throttle_enabled_ ? "Yes" : "No") << "\n";
    oss << "  VSync Enabled: " << (vsync_enabled_ ? "Yes" : "No") << "\n";
    oss << "  Paused: " << (paused_ ? "Yes" : "No") << "\n";
    oss << "  Frame Stepping: " << (frame_stepping_ ? "Yes" : "No") << "\n";
    return oss.str();
}

FrameController::FrameRangeStats FrameController::analyzeFrameRange(uint64_t start_frame, uint64_t end_frame) const {
    FrameRangeStats stats = {};
    stats.start_frame = start_frame;
    stats.end_frame = end_frame;
    stats.total_frames = end_frame - start_frame + 1;
    
    // This would require more detailed frame history tracking
    // For now, return basic stats
    stats.average_fps = getAverageFPS(stats.total_frames);
    stats.min_fps = stats.average_fps * 0.8; // Estimate
    stats.max_fps = stats.average_fps * 1.2; // Estimate
    stats.total_time_ms = static_cast<uint64_t>(stats.total_frames * (1000.0 / stats.average_fps));
    
    return stats;
}

void FrameController::resetFrameCounter() {
    current_frame_ = 0;
    current_stats_.frame_count = 0;
    current_stats_.lag_frame_count = 0;
    current_stats_.input_frame_count = 0;
}

void FrameController::setFrameCounter(uint64_t frame) {
    current_frame_ = frame;
    current_stats_.frame_count = frame;
}

std::chrono::microseconds FrameController::getFrameTimeTarget() const {
    return std::chrono::microseconds(static_cast<uint64_t>(target_frame_time_us_));
}

std::chrono::microseconds FrameController::getActualFrameTime() const {
    return std::chrono::microseconds(current_stats_.frame_time_us);
}

// Private helper methods
void FrameController::updateFrameStats() {
    current_stats_.frame_count = current_frame_;
    current_stats_.is_lag_frame = false;
    
    // Update average FPS every 60 frames
    if (current_frame_ % 60 == 0) {
        current_stats_.average_fps = getAverageFPS(60);
        last_stats_update_ = current_frame_;
    }
}

void FrameController::updateSpeedMultiplier() {
    switch (current_speed_mode_) {
        case SpeedMode::NORMAL:
            current_stats_.speed_multiplier = 1.0;
            break;
        case SpeedMode::NOTHROTTLE:
            current_stats_.speed_multiplier = 0.0; // No throttling
            break;
        case SpeedMode::TURBO:
            current_stats_.speed_multiplier = 2.0;
            break;
        case SpeedMode::MAXIMUM:
            current_stats_.speed_multiplier = 0.0; // Unlimited
            break;
        case SpeedMode::CUSTOM:
            current_stats_.speed_multiplier = custom_speed_multiplier_;
            break;
        case SpeedMode::FRAME_BY_FRAME:
            current_stats_.speed_multiplier = 0.0; // Manual control
            break;
    }
    
    // Update target frame time based on speed multiplier
    if (current_stats_.speed_multiplier > 0.0) {
        target_frame_time_us_ = (1000000.0 / current_stats_.target_fps) / current_stats_.speed_multiplier;
    }
}

void FrameController::handleFrameSkipping() {
    if (frames_to_skip_ > 0) {
        frames_to_skip_--;
        current_stats_.frame_skip_count++;
    }
    
    // Adaptive frame skipping based on performance
    if (adaptive_frame_skip_ && current_frame_ % 60 == 0) {
        double avg_fps = getAverageFPS(60);
        if (avg_fps < current_stats_.target_fps * 0.9) {
            // Performance is poor, increase frame skipping
            frame_skip_ratio_ = std::min(frame_skip_ratio_ + 1, 5U);
        } else if (avg_fps > current_stats_.target_fps * 0.95) {
            // Performance is good, decrease frame skipping
            frame_skip_ratio_ = frame_skip_ratio_ > 0 ? frame_skip_ratio_ - 1 : 0;
        }
    }
}

void FrameController::detectLagFrame() {
    // Simple lag frame detection: if no input for multiple frames
    if (current_frame_ - last_input_frame_ > 2) {
        markLagFrame();
    }
}

void FrameController::callFrameStartCallbacks() {
    for (auto& callback : frame_start_callbacks_) {
        callback(current_frame_);
    }
}

void FrameController::callFrameEndCallbacks() {
    for (auto& callback : frame_end_callbacks_) {
        callback(current_frame_, current_stats_);
    }
}

void FrameController::callLagFrameCallbacks() {
    for (auto& callback : lag_frame_callbacks_) {
        callback(current_frame_);
    }
}

void FrameController::callSpeedChangeCallbacks(SpeedMode old_mode, SpeedMode new_mode) {
    for (auto& callback : speed_change_callbacks_) {
        callback(old_mode, new_mode);
    }
}

void FrameController::sleepUntilNextFrame() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start_time_);
    auto target_time = std::chrono::microseconds(static_cast<uint64_t>(target_frame_time_us_));
    
    if (elapsed < target_time) {
        auto sleep_time = target_time - elapsed;
        
        // Performance optimization: Use adaptive sleep strategy
        if (sleep_time.count() > 2000) {  // > 2ms: use standard sleep
            // Sleep for most of the time, leaving 1ms for precision
            auto coarse_sleep = sleep_time - std::chrono::microseconds(1000);
            std::this_thread::sleep_for(coarse_sleep);
        }
        
        // Precision timing: busy-wait with yield for remaining time
        while (true) {
            now = std::chrono::high_resolution_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start_time_);
            if (elapsed >= target_time) {
                break;
            }
            
            // Yield CPU to other threads to avoid 100% CPU usage
            std::this_thread::yield();
        }
    }
}

std::chrono::microseconds FrameController::calculateFrameDelay() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start_time_);
    auto target_time = std::chrono::microseconds(static_cast<uint64_t>(target_frame_time_us_));
    
    if (elapsed < target_time) {
        return target_time - elapsed;
    }
    
    return std::chrono::microseconds(0);
}

bool FrameController::shouldThrottleFrame() const {
    return throttle_enabled_ && 
           current_speed_mode_ != SpeedMode::MAXIMUM && 
           current_speed_mode_ != SpeedMode::NOTHROTTLE;
}

void FrameController::addFrameTimeToHistory(double frame_time) {
    if (frame_time_history_.size() < history_size_) {
        frame_time_history_.push_back(frame_time);
    } else {
        frame_time_history_[frame_time_history_index_] = frame_time;
        frame_time_history_index_ = (frame_time_history_index_ + 1) % history_size_;
    }
}

void FrameController::addFPSToHistory(double fps) {
    if (fps_history_.size() < history_size_) {
        fps_history_.push_back(fps);
    } else {
        fps_history_[fps_history_index_] = fps;
        fps_history_index_ = (fps_history_index_ + 1) % history_size_;
    }
}

void FrameController::trimHistory() {
    if (frame_time_history_.size() > history_size_) {
        frame_time_history_.resize(history_size_);
    }
    if (fps_history_.size() > history_size_) {
        fps_history_.resize(history_size_);
    }
}

// DOSBox Integration
namespace DOSBoxIntegration {

bool initializeFrameControl() {
    // Frame controller is now initialized by LuaEngine
    if (!g_frame_controller) {
        return false;  // Not initialized yet
    }

    return g_frame_controller->initialize();
}

void shutdownFrameControl() {
    if (g_frame_controller) {
        g_frame_controller->shutdown();
    }
}

void onFrameStart() {
    if (g_frame_controller) {
        g_frame_controller->beginFrame();
    }
}

void onFrameEnd() {
    if (g_frame_controller) {
        g_frame_controller->endFrame();
    }
}

void onCPUCycleComplete() {
    // Could be used for more precise timing measurements
}

void onRenderComplete() {
    // Mark render timing
}

void setDOSBoxSpeed(double multiplier) {
    if (g_frame_controller) {
        g_frame_controller->setCustomSpeedMultiplier(multiplier);
        g_frame_controller->setSpeedMode(SpeedMode::CUSTOM);
    }
}

double getDOSBoxSpeed() {
    return g_frame_controller ? g_frame_controller->getCustomSpeedMultiplier() : 1.0;
}

void pauseDOSBox() {
    if (g_frame_controller) {
        g_frame_controller->pause();
    }
}

void resumeDOSBox() {
    if (g_frame_controller) {
        g_frame_controller->resume();
    }
}

void checkInputActivity() {
    if (g_frame_controller) {
        g_frame_controller->markInputFrame();
    }
}

void checkScreenUpdates() {
    // Monitor screen changes for lag detection
}

void checkAudioActivity() {
    // Monitor audio output for lag detection
}

DOSBoxPerformance getPerformanceMetrics() {
    DOSBoxPerformance perf = {};
    perf.cpu_usage_percent = 0.0; // Would need actual CPU monitoring
    perf.gpu_usage_percent = 0.0; // Would need actual GPU monitoring
    perf.memory_usage_kb = 0;     // Would need actual memory monitoring
    perf.emulation_efficiency = g_frame_controller ? 
        (g_frame_controller->getFrameStats().actual_fps / g_frame_controller->getFrameStats().target_fps) * 100.0 : 0.0;
    perf.cycles_per_second = 0;   // Would need CPU cycle counting
    return perf;
}

} // namespace DOSBoxIntegration

// BizHawk Compatibility
namespace BizHawkCompat {

void pauseEmulation() {
    if (g_frame_controller) {
        g_frame_controller->pause();
    }
}

void unpauseEmulation() {
    if (g_frame_controller) {
        g_frame_controller->resume();
    }
}

bool isEmulationPaused() {
    return g_frame_controller ? g_frame_controller->isPaused() : false;
}

void frameAdvance() {
    if (g_frame_controller) {
        g_frame_controller->advanceFrame();
    }
}

void seekFrame(uint64_t frame) {
    if (g_frame_controller) {
        g_frame_controller->seekToFrame(frame);
    }
}

void setSpeedPercent(int percent) {
    if (g_frame_controller) {
        double multiplier = percent / 100.0;
        g_frame_controller->setCustomSpeedMultiplier(multiplier);
        g_frame_controller->setSpeedMode(SpeedMode::CUSTOM);
    }
}

int getSpeedPercent() {
    if (g_frame_controller) {
        return static_cast<int>(g_frame_controller->getCustomSpeedMultiplier() * 100);
    }
    return 100;
}

void setMinimumSpeedPercent(int percent) {
    if (g_frame_controller) {
        double fps = (percent / 100.0) * g_frame_controller->getTargetFPS();
        g_frame_controller->setMinFPS(fps);
    }
}

void setMaximumSpeedPercent(int percent) {
    if (g_frame_controller) {
        double fps = (percent / 100.0) * g_frame_controller->getTargetFPS();
        g_frame_controller->setMaxFPS(fps);
    }
}

uint64_t getFrameCount() {
    return g_frame_controller ? g_frame_controller->getCurrentFrame() : 0;
}

uint64_t getLagCount() {
    return g_frame_controller ? g_frame_controller->getLagFrameCount() : 0;
}

bool isLagged() {
    return g_frame_controller ? g_frame_controller->getFrameStats().is_lag_frame : false;
}

bool isLagged(uint64_t frame) {
    return g_frame_controller ? g_frame_controller->isLagFrame(frame) : false;
}

double getFPS() {
    return g_frame_controller ? g_frame_controller->getFrameStats().actual_fps : 0.0;
}

double getTargetFPS() {
    return g_frame_controller ? g_frame_controller->getTargetFPS() : 60.0;
}

std::string getSystemId() {
    return "DOSBox-X";
}

std::string getDisplayType() {
    return "DOS";
}

void throttleSpeed(bool enabled) {
    if (g_frame_controller) {
        g_frame_controller->setThrottleEnabled(enabled);
    }
}

void setFrameSkip(int skip) {
    if (g_frame_controller) {
        g_frame_controller->setFrameSkipRatio(static_cast<uint32_t>(skip));
    }
}

int getFrameSkip() {
    return g_frame_controller ? static_cast<int>(g_frame_controller->getFrameSkipRatio()) : 0;
}

} // namespace BizHawkCompat

} // namespace LuaEngineFrameControl