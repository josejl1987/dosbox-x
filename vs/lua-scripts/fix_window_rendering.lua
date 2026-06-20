-- Fix window rendering by properly setting up render callbacks
print("=== Fixing Window Rendering ===")

-- The issue is that CustomLuaWindow needs the render function properly connected
-- Let's create a window with the correct structure

if window then
    print("Creating properly structured window...")
    
    -- This is the correct way to create a Lua window
    local working_window = {
        -- The render function should be called by the window system
        render = function()
            print("🔥 RENDER FUNCTION CALLED - " .. os.clock())
            
            -- Use the correct GUI API
            gui.text(20, 20, "✅ WINDOW RENDERING!")
            gui.text(20, 40, "Frame: " .. os.clock())
            gui.drawrect(20, 60, 150, 30, "green")
            gui.text(25, 70, "Multi-viewport ON")
            
            -- Test if this is being called every frame
            if math.floor(os.clock()) % 2 == 0 then
                gui.text(20, 100, "Even second")
            else
                gui.text(20, 100, "Odd second")
            end
        end
    }
    
    -- Enable multi-viewport first
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled:", window.isMultiViewportEnabled())
    
    -- Create the window
    local window_id = window.createLua("Working Render", working_window)
    print("✓ Window created:", window_id)
    
    -- Check if the window manager is properly set up
    if LuaEngineGUIWindows and LuaEngineGUIWindows.g_window_manager then
        print("✓ Window manager exists")
        
        -- Try to manually call renderAllWindows to test
        local success, error = pcall(function()
            LuaEngineGUIWindows.g_window_manager:renderAllWindows()
        end)
        if success then
            print("✓ Manual renderAllWindows succeeded")
        else
            print("❌ Manual renderAllWindows failed:", error)
        end
        
        -- Check window count
        if window.getWindowCount then
            print("Window count:", window.getWindowCount())
        end
        
    else
        print("❌ Window manager not accessible")
    end
    
    -- Create a second test window
    local test_window2 = {
        render = function()
            print("🔥 SECOND WINDOW RENDER - " .. os.clock())
            gui.text(10, 10, "Second window")
            gui.text(10, 30, "Time: " .. string.format("%.1f", os.clock()))
        end
    }
    
    local window_id2 = window.createLua("Second Test", test_window2)
    print("✓ Second window created:", window_id2)
    
else
    print("❌ Window API not available")
end

print("\n=== Fix Complete ===")
print("Watch for render function messages every frame")
print("If you see them, windows are working but overlay isn't visible")
print("If not, the render callback setup is broken")