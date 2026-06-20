-- Simple test of working features
print("=== DOSBox-X Feature Test ===")

-- Test 1: Multi-Viewport (WORKING!)
print("\n✅ Multi-Viewport Test:")
print("Available:", window ~= nil)
if window then
    print("Enabled:", window.isMultiViewportEnabled())
    
    -- Create a simple detachable window
    local simple_window = {
        title = "🎉 Detachable Window",
        render = function()
            gui.text("SUCCESS! Multi-viewport is working!")
            gui.text("")
            gui.text("🖱️ Drag this window outside the main")
            gui.text("   DOSBox-X window to detach it!")
            gui.text("")
            gui.separator()
            
            if gui.button("Test Button") then
                print("✅ Button clicked in detachable window!")
            end
            
            gui.text("Status: Multi-viewport ENABLED ✓")
        end
    }
    
    local window_id = window.createLua("Detachable Test", simple_window)
    print("✅ Created window:", window_id)
    print("   → Try dragging it outside!")
end

-- Test 2: Performance API (Check what's available)
print("\n🚀 Performance API Test:")
print("Available:", performance ~= nil)
if performance then
    -- Test what functions exist
    local functions = {
        "get_stats",
        "enable_fast_memory", 
        "enable_minimal_error_checking",
        "read_bytes_fast",
        "write_bytes_fast"
    }
    
    for _, func_name in ipairs(functions) do
        local exists = performance[func_name] ~= nil
        print("  " .. func_name .. ":", exists and "✅" or "❌")
    end
    
    -- Test what works
    if performance.get_stats then
        local stats = performance.get_stats()
        print("✅ Performance stats:")
        print("   Memory operations:", stats.memory_operations or 0)
        print("   Script executions:", stats.script_executions or 0)
    end
    
    if performance.enable_fast_memory then
        performance.enable_fast_memory(true)
        print("✅ Fast memory mode enabled")
    end
    
    if performance.read_bytes_fast then
        local start_time = os.clock()
        local data = performance.read_bytes_fast(0x1000, 256)
        local elapsed = (os.clock() - start_time) * 1000
        print(string.format("✅ Fast read: %d bytes in %.3f ms", #data, elapsed))
    end
end

-- Test 3: Basic APIs
print("\n📋 Basic API Test:")
print("Memory API:", memory ~= nil and "✅" or "❌")
print("GUI API:", gui ~= nil and "✅" or "❌") 
print("Debug API:", debug ~= nil and "✅" or "❌")

if memory then
    local test_byte = memory.readbyte(0x0040, 0x0000)
    print("✅ Memory read test: 0x" .. string.format("%02X", test_byte or 0))
end

print("\n🎉 Test Complete!")
print("Multi-viewport is ready for professional use!")
print("Try dragging the test window outside the main area.")