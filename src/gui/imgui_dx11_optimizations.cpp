// DirectX 11 ImGui Optimizations - Phase 1 Implementation
// This file demonstrates the recommended incremental optimization approach
// instead of complex multi-threading

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>

// Forward declaration of existing backend data
extern struct DX11BackendData {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGIFactory* factory;
    bool initialized;
} g_dx11_backend;

// Phase 1 Optimization: Multi-Viewport Batching
class D3D11ViewportBatcher {
private:
    struct ViewportBatch {
        std::vector<ImGuiViewport*> viewports;
        ID3D11RenderTargetView* target_rtv;
        D3D11_VIEWPORT d3d_viewport;
        bool needs_clear;
    };
    
    std::vector<ViewportBatch> batches;
    std::unordered_map<ImGuiID, ID3D11RenderTargetView*> viewport_rtvs;
    
public:
    void OptimizeViewportRendering() {
        if (!g_dx11_backend.initialized) return;
        
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        batches.clear();
        
        // Group viewports by similar properties to minimize state changes
        GroupViewportsByProperties(platform_io);
        
        // Render each batch with minimal state changes
        for (const auto& batch : batches) {
            RenderViewportBatch(batch);
        }
        
        // Batch all present operations for efficiency
        BatchPresentOperations();
    }
    
private:
    void GroupViewportsByProperties(ImGuiPlatformIO& platform_io) {
        std::unordered_map<size_t, ViewportBatch> batch_map;
        
        for (int i = 1; i < platform_io.Viewports.Size; i++) {
            ImGuiViewport* viewport = platform_io.Viewports[i];
            if (!viewport->DrawData || viewport->DrawData->CmdListsCount == 0) continue;
            
            // Create hash based on viewport properties for batching
            size_t batch_key = HashViewportProperties(viewport);
            
            auto& batch = batch_map[batch_key];
            batch.viewports.push_back(viewport);
            
            // Set batch properties from first viewport
            if (batch.viewports.size() == 1) {
                batch.target_rtv = GetOrCreateRTV(viewport);
                batch.d3d_viewport = CreateD3DViewport(viewport);
                batch.needs_clear = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0;
            }
        }
        
        // Convert map to vector for iteration
        batches.reserve(batch_map.size());
        for (auto& pair : batch_map) {
            batches.push_back(std::move(pair.second));
        }
        
        // Sort batches by render target to minimize switches
        std::sort(batches.begin(), batches.end(), 
            [](const ViewportBatch& a, const ViewportBatch& b) {
                return a.target_rtv < b.target_rtv;
            });
    }
    
    void RenderViewportBatch(const ViewportBatch& batch) {
        ID3D11DeviceContext* context = g_dx11_backend.context;
        
        // Set render target once per batch (major optimization)
        context->OMSetRenderTargets(1, &batch.target_rtv, nullptr);
        context->RSSetViewports(1, &batch.d3d_viewport);
        
        // Clear once per batch if needed
        if (batch.needs_clear) {
            float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            context->ClearRenderTargetView(batch.target_rtv, clear_color);
        }
        
        // Render all viewports in this batch
        for (ImGuiViewport* viewport : batch.viewports) {
            // Set viewport-specific scissor rect if needed
            if (viewport->DrawData->CmdListsCount > 0) {
                SetViewportScissor(viewport);
                ImGui_ImplDX11_RenderDrawData(viewport->DrawData);
            }
        }
    }
    
    size_t HashViewportProperties(ImGuiViewport* viewport) {
        // Hash based on properties that affect batching
        size_t hash = 0;
        hash ^= std::hash<float>{}(viewport->Size.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(viewport->Size.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(viewport->Flags) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
    
    ID3D11RenderTargetView* GetOrCreateRTV(ImGuiViewport* viewport) {
        auto it = viewport_rtvs.find(viewport->ID);
        if (it != viewport_rtvs.end()) {
            return it->second;
        }
        
        // Create new RTV for this viewport
        // Implementation would get swap chain back buffer and create RTV
        // This is a simplified version
        return nullptr; // Placeholder
    }
    
    D3D11_VIEWPORT CreateD3DViewport(ImGuiViewport* viewport) {
        D3D11_VIEWPORT d3d_viewport = {};
        d3d_viewport.Width = viewport->Size.x;
        d3d_viewport.Height = viewport->Size.y;
        d3d_viewport.MinDepth = 0.0f;
        d3d_viewport.MaxDepth = 1.0f;
        d3d_viewport.TopLeftX = 0.0f;
        d3d_viewport.TopLeftY = 0.0f;
        return d3d_viewport;
    }
    
    void SetViewportScissor(ImGuiViewport* viewport) {
        // Set scissor rectangle for this specific viewport
        D3D11_RECT scissor_rect;
        scissor_rect.left = 0;
        scissor_rect.top = 0;
        scissor_rect.right = (LONG)viewport->Size.x;
        scissor_rect.bottom = (LONG)viewport->Size.y;
        
        g_dx11_backend.context->RSSetScissorRects(1, &scissor_rect);
    }
    
    void BatchPresentOperations() {
        // Collect all swap chains that need presenting
        std::vector<IDXGISwapChain*> swap_chains_to_present;
        
        for (const auto& batch : batches) {
            for (ImGuiViewport* viewport : batch.viewports) {
                IDXGISwapChain* swap_chain = GetSwapChainForViewport(viewport);
                if (swap_chain) {
                    swap_chains_to_present.push_back(swap_chain);
                }
            }
        }
        
        // Present all swap chains with optimal timing
        static auto last_present_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_present_time);
        
        // Adaptive present timing based on viewport count
        int min_interval = swap_chains_to_present.size() > 2 ? 33 : 16; // 30fps vs 60fps
        
        if (time_since_last.count() >= min_interval) {
            for (IDXGISwapChain* swap_chain : swap_chains_to_present) {
                swap_chain->Present(1, 0); // VSync enabled for smooth presentation
            }
            last_present_time = current_time;
        }
    }
    
    IDXGISwapChain* GetSwapChainForViewport(ImGuiViewport* viewport) {
        // Implementation would retrieve swap chain associated with viewport
        // This is a placeholder
        return nullptr;
    }
};

// Phase 1 Optimization: DirectX 11 State Management
class D3D11StateManager {
private:
    struct CachedState {
        ID3D11RenderTargetView* current_rtv = nullptr;
        ID3D11DepthStencilView* current_dsv = nullptr;
        D3D11_VIEWPORT current_viewport = {};
        ID3D11BlendState* current_blend_state = nullptr;
        ID3D11RasterizerState* current_rasterizer_state = nullptr;
        ID3D11DepthStencilState* current_depth_stencil_state = nullptr;
        UINT current_stencil_ref = 0;
        bool viewport_dirty = true;
        bool render_targets_dirty = true;
    };
    
    CachedState cached_state;
    
public:
    void SetRenderTarget(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv = nullptr) {
        if (cached_state.current_rtv != rtv || cached_state.current_dsv != dsv) {
            g_dx11_backend.context->OMSetRenderTargets(1, &rtv, dsv);
            cached_state.current_rtv = rtv;
            cached_state.current_dsv = dsv;
            cached_state.render_targets_dirty = false;
        }
    }
    
    void SetViewport(const D3D11_VIEWPORT& viewport) {
        if (cached_state.viewport_dirty || 
            memcmp(&cached_state.current_viewport, &viewport, sizeof(D3D11_VIEWPORT)) != 0) {
            g_dx11_backend.context->RSSetViewports(1, &viewport);
            cached_state.current_viewport = viewport;
            cached_state.viewport_dirty = false;
        }
    }
    
    void SetBlendState(ID3D11BlendState* blend_state) {
        if (cached_state.current_blend_state != blend_state) {
            float blend_factor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            g_dx11_backend.context->OMSetBlendState(blend_state, blend_factor, 0xFFFFFFFF);
            cached_state.current_blend_state = blend_state;
        }
    }
    
    void SetRasterizerState(ID3D11RasterizerState* rasterizer_state) {
        if (cached_state.current_rasterizer_state != rasterizer_state) {
            g_dx11_backend.context->RSSetState(rasterizer_state);
            cached_state.current_rasterizer_state = rasterizer_state;
        }
    }
    
    void SetDepthStencilState(ID3D11DepthStencilState* depth_stencil_state, UINT stencil_ref) {
        if (cached_state.current_depth_stencil_state != depth_stencil_state || 
            cached_state.current_stencil_ref != stencil_ref) {
            g_dx11_backend.context->OMSetDepthStencilState(depth_stencil_state, stencil_ref);
            cached_state.current_depth_stencil_state = depth_stencil_state;
            cached_state.current_stencil_ref = stencil_ref;
        }
    }
    
    void InvalidateCache() {
        // Call this when external code might have changed D3D11 state
        cached_state = CachedState{};
        cached_state.viewport_dirty = true;
        cached_state.render_targets_dirty = true;
    }
    
    void BeginFrame() {
        // Reset dirty flags at the beginning of each frame
        cached_state.viewport_dirty = false;
        cached_state.render_targets_dirty = false;
    }
};

// Global instances for optimization systems
static D3D11ViewportBatcher g_viewport_batcher;
static D3D11StateManager g_state_manager;

// Public interface functions to integrate with existing imgui_window.cpp
extern "C" {
    void OptimizeD3D11MultiViewportRendering() {
        g_viewport_batcher.OptimizeViewportRendering();
    }
    
    void BeginD3D11OptimizedFrame() {
        g_state_manager.BeginFrame();
    }
    
    void InvalidateD3D11StateCache() {
        g_state_manager.InvalidateCache();
    }
}

// Performance monitoring for Phase 1 optimizations
class D3D11PerformanceMonitor {
private:
    struct FrameMetrics {
        std::chrono::high_resolution_clock::time_point frame_start;
        int viewport_count = 0;
        int state_changes = 0;
        int present_calls = 0;
        float frame_time_ms = 0.0f;
    };
    
    FrameMetrics current_frame;
    std::vector<FrameMetrics> frame_history;
    int frame_counter = 0;
    
public:
    void BeginFrame() {
        current_frame = FrameMetrics{};
        current_frame.frame_start = std::chrono::high_resolution_clock::now();
    }
    
    void RecordViewport() { current_frame.viewport_count++; }
    void RecordStateChange() { current_frame.state_changes++; }
    void RecordPresent() { current_frame.present_calls++; }
    
    void EndFrame() {
        auto end_time = std::chrono::high_resolution_clock::now();
        current_frame.frame_time_ms = std::chrono::duration<float, std::milli>(
            end_time - current_frame.frame_start).count();
        
        frame_history.push_back(current_frame);
        if (frame_history.size() > 300) { // Keep 5 seconds of history at 60fps
            frame_history.erase(frame_history.begin());
        }
        
        frame_counter++;
        
        // Log performance every 5 seconds
        if (frame_counter % 300 == 0) {
            LogPerformanceMetrics();
        }
    }
    
private:
    void LogPerformanceMetrics() {
        if (frame_history.empty()) return;
        
        float avg_frame_time = 0.0f;
        float avg_viewport_count = 0.0f;
        float avg_state_changes = 0.0f;
        
        for (const auto& frame : frame_history) {
            avg_frame_time += frame.frame_time_ms;
            avg_viewport_count += frame.viewport_count;
            avg_state_changes += frame.state_changes;
        }
        
        size_t history_size = frame_history.size();
        avg_frame_time /= history_size;
        avg_viewport_count /= history_size;
        avg_state_changes /= history_size;
        
        printf("D3D11 Optimization Performance (Frame %d):\n", frame_counter);
        printf("  Average Frame Time: %.2fms\n", avg_frame_time);
        printf("  Average Viewport Count: %.1f\n", avg_viewport_count);
        printf("  Average State Changes: %.1f\n", avg_state_changes);
        printf("  Optimization Efficiency: %.1f%% (lower state changes = better)\n", 
               100.0f * (1.0f - avg_state_changes / (avg_viewport_count * 10.0f)));
    }
};

static D3D11PerformanceMonitor g_performance_monitor;

// Additional public interface for performance monitoring
extern "C" {
    void BeginD3D11PerformanceFrame() {
        g_performance_monitor.BeginFrame();
    }
    
    void EndD3D11PerformanceFrame() {
        g_performance_monitor.EndFrame();
    }
    
    void RecordD3D11Viewport() {
        g_performance_monitor.RecordViewport();
    }
    
    void RecordD3D11StateChange() {
        g_performance_monitor.RecordStateChange();
    }
}

#endif // _WIN32
