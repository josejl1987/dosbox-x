-- Test if the render integration is working
print("=== Testing Render Integration ===")

-- Check if the overlay manager is calling the window manager
print("Checking render integration...")

-- Check if overlay manager exists and is being called
if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
    print("✓ Overlay manager exists")
    
    -- Check if window manager exists
    if LuaEngineGUIWindows and LuaEngineGUIWindows.g_window_manager then
        print("✓ Window manager exists")
        
        -- Try to manually call the render chain
        print("Testing manual render chain...")
        
        -- 1. Test overlay manager render
        local success, error = pcall(function()
            LuaEngineGUI.g_overlay_manager:render()
            print("✓ Overlay manager render called")
        end)
        if not success then
            print("❌ Overlay manager render failed:", error)
        end
        
        -- 2. Test window manager render directly
        success, error = pcall(function()
            LuaEngineGUIWindows.g_window_manager:renderAllWindows()
            print("✓ Window manager renderAllWindows called")
        end)
        if not success then
            print("❌ Window manager renderAllWindows failed:", error)
        end
        
    else
        print("❌ Window manager not found")
        print("Available in LuaEngineGUIWindows:")
        if LuaEngineGUIWindows then
            for k, v in pairs(LuaEngineGUIWindows) do
                print("  " .. k .. ":", type(v))
            end
        else
            print("  LuaEngineGUIWindows namespace not found")
        end
    end
    
else
    print("❌ Overlay manager not found")
end

-- Create a test window to see if manual rendering works
if window then
    print("Creating test window for manual render test...")
    
    local manual_test_window = {
        render = function()
            print("🔥 MANUAL TEST RENDER CALLED")
            gui.text(50, 50, "Manual render test")
        end
    }
    
    local window_id = window.createLua("Manual Test", manual_test_window)
    print("✓ Manual test window created:", window_id)
    
    -- Try to manually trigger rendering
    print("Attempting manual render trigger...")
    if LuaEngineGUIWindows and LuaEngineGUIWindows.g_window_manager then
        LuaEngineGUIWindows.g_window_manager:renderAllWindows()
        print("Manual render triggered - check for render messages")
    end
    
else
    print("❌ Window API not available")
end

print("\n=== Integration Test Complete ===")
print("Check console for render messages after manual trigger")