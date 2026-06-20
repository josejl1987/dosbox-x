#ifndef IMGUI_PERFORMANCE_CONFIG_H
#define IMGUI_PERFORMANCE_CONFIG_H

// ImGui Performance Configuration
// This header contains tunable parameters for ImGui performance optimizations

// Multi-viewport rendering optimizations
#define IMGUI_VIEWPORT_RENDER_INTERVAL 2        // Render additional viewports every N frames (2 = 30fps)
#define IMGUI_MIN_SWAP_INTERVAL_MS 33           // Minimum time between buffer swaps (33ms = ~30fps)
#define IMGUI_ENABLE_PERFORMANCE_LOGGING 1      // Enable performance logging in debug builds

// Context switching optimizations
#define IMGUI_ENABLE_CONTEXT_BATCHING 1         // Enable context switch batching
#define IMGUI_ENABLE_BUFFER_SWAP_BATCHING 1     // Enable buffer swap batching

// Auto-detection settings
#define IMGUI_AUTO_DETECT_HIGH_PERF_GPU 0       // Auto-enable viewports on dedicated GPUs (0=disabled)

// Performance thresholds
#define IMGUI_MAX_CONTEXT_SWITCHES_PER_FRAME 4  // Warning threshold for context switches
#define IMGUI_MAX_BUFFER_SWAPS_PER_FRAME 3      // Warning threshold for buffer swaps

// Environment variable names
#define IMGUI_VIEWPORT_ENV_VAR "DOSBOX_IMGUI_VIEWPORTS"
#define IMGUI_PERFORMANCE_ENV_VAR "DOSBOX_IMGUI_PERFORMANCE"

// Debug output settings
#define IMGUI_PERF_LOG_INTERVAL_FRAMES 300      // Log performance every N frames (300 = 5 seconds at 60fps)

#endif // IMGUI_PERFORMANCE_CONFIG_H
