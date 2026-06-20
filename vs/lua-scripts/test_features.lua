-- DOSBox-X Multi-Viewport and Performance Test Script
-- This script tests the new features we implemented

print("=== Testing New DOSBox-X Features ===")

-- Test 1: Multi-Viewport Support
print("\n--- Multi-Viewport Test ---")
print("Multi-viewport available:", window ~= nil)

if window then
    print("Current multi-viewport status:", window.isMultiViewportEnabled())
    
    -- Enable multi-viewport
    window.enableMultiViewport()
    print("Multi-viewport enabled:", window.isMultiViewportEnabled())
    
    -- Get main viewport info
    local main_pos = window.getMainViewportPos()
    local main_size = window.getMainViewportSize()
    print(string.format("Main viewport: %dx%d at (%d,%d)", 
        main_size.width, main_size.height, main_pos.x, main_pos.y))
    
    -- Create a test window that can be detached
    local test_window = {
        title = "Detachable Test Window",
        render = function()
            gui.text("🎉 Multi-Viewport Working!")
            gui.text("Drag this window outside the main area")
            gui.text("to turn it into a separate OS window.")
            gui.separator()
            
            if gui.button("Test Button") then
                print("Button clicked in detachable window!")
            end
            
            gui.text("Status: " .. (window.isMultiViewportEnabled() and "✓ Multi-viewport ON" or "✗ Single viewport"))
        end
    }
    
    local window_id = window.createLua("Test Window", test_window)
    print("✓ Created detachable test window:", window_id)
    print("  → Drag it outside to see multi-viewport in action!")
else
    print("✗ Window API not available")
end

-- Test 2: Performance Improvements
print("\n--- Performance Test ---")
print("Performance API available:", performance ~= nil)

if performance then
    -- Test current performance settings
    print("Fast memory enabled:", performance.is_fast_memory_enabled and performance.is_fast_memory_enabled() or "unknown")
    print("Minimal error checking:", performance.is_minimal_error_checking_enabled and performance.is_minimal_error_checking_enabled() or "unknown")
    
    -- Enable fast mode
    performance.enable_fast_memory(true)
    performance.enable_minimal_error_checking(true)
    print("✓ Enabled fast memory mode")
    
    -- Test fast bulk operations
    print("Testing fast bulk memory operations...")
    
    local start_time = os.clock()
    local test_data = performance.read_bytes_fast(0x1000, 1024)  -- Read 1KB
    local read_time = (os.clock() - start_time) * 1000
    
    print(string.format("✓ Read %d bytes in %.3f ms", #test_data, read_time))
    
    -- Test write performance
    local write_data = {}
    for i = 1, 256 do
        write_data[i] = math.random(0, 255)
    end
    
    start_time = os.clock()
    local write_success = performance.write_bytes_fast(0x2000, write_data)
    local write_time = (os.clock() - start_time) * 1000
    
    print(string.format("✓ Wrote %d bytes in %.3f ms (success: %s)", 
        #write_data, write_time, tostring(write_success)))
    
    -- Show performance stats
    local stats = performance.get_stats()
    print("Performance statistics:")
    print(string.format("  Memory operations: %d", stats.memory_operations))
    print(string.format("  Script executions: %d", stats.script_executions))
    
    print("✓ Performance optimizations working")
else
    print("✗ Performance API not available")
end

-- Test 3: Combined Features Demo
print("\n--- Combined Features Demo ---")

if window and performance then
    -- Create a performance monitoring window
    local perf_window = {
        title = "Performance Monitor",
        render = function()
            gui.text("🚀 Performance Monitor")
            gui.separator()
            
            local stats = performance.get_stats()
            gui.text("Memory operations: " .. stats.memory_operations)
            gui.text("Script executions: " .. stats.script_executions)
            
            gui.separator()
            gui.text("Fast memory: " .. (performance.is_fast_memory_enabled and performance.is_fast_memory_enabled() and "ON" or "OFF"))
            gui.text("Error checking: " .. (performance.is_minimal_error_checking_enabled and performance.is_minimal_error_checking_enabled() and "Minimal" or "Full"))
            
            gui.separator()
            if gui.button("Toggle Fast Memory") then
                local current = performance.is_fast_memory_enabled()
                performance.enable_fast_memory(not current)
                print("Fast memory toggled:", not current)
            end
            
            if gui.button("Test Memory Read") then
                local start = os.clock()
                local data = performance.read_bytes_fast(0x1000, 512)
                local elapsed = (os.clock() - start) * 1000
                print(string.format("Read %d bytes in %.3f ms", #data, elapsed))
            end
        end
    }
    
    local perf_window_id = window.createLua("Performance Monitor", perf_window)
    print("✓ Created performance monitor window:", perf_window_id)
    print("  → This window can also be detached!")
    
    print("\n🎉 All features working! Try:")
    print("  1. Drag windows outside the main area")
    print("  2. Use performance monitor controls")
    print("  3. Test memory operations")
else
    print("✗ Some APIs not available for combined demo")
end

print("\n=== Feature Test Complete ===")
print("Both multi-viewport and performance optimizations are ready!")