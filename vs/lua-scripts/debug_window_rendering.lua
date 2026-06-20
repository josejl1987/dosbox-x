-- Debug window rendering to see why windows aren't rendering every frame
print("=== Debugging Window Rendering ===")

-- Check if window manager is properly set up
print("Checking window manager...")
if LuaEngineGUIWindows then
    print("✓ LuaEngineGUIWindows namespace exists")
    
    if LuaEngineGUIWindows.g_window_manager then
        print("✓ g_window_manager exists")
        
        local manager = LuaEngineGUIWindows.g_window_manager
        
        -- Check if renderAllWindows method exists
        if manager.renderAllWindows then
            print("✓ renderAllWindows method exists")
        else
            print("❌ renderAllWindows method missing")
        end
        
        -- Try to call renderAllWindows manually
        local success, error = pcall(function()
            manager:renderAllWindows()
        end)
        if success then
            print("✓ Manual renderAllWindows call succeeded")
        else
            print("❌ Manual renderAllWindows call failed:", error)
        end
        
    else
        print("❌ g_window_manager not found")
    end
else
    print("❌ LuaEngineGUIWindows namespace not found")
end

-- Create a test window with frame counting
print("\nCreating debug window with frame counter...")
if window then
    local frame_count = 0
    local last_time = os.clock()
    
    local debug_window = {
        render = function()
            frame_count = frame_count + 1
            local current_time = os.clock()
            
            if frame_count % 60 == 0 then  -- Print every 60 frames
                print(string.format("🔥 Frame %d - Window rendering at %.2f FPS", 
                    frame_count, 60 / (current_time - last_time)))
                last_time = current_time
            end
            
            -- Use GUI API to draw
            gui.text(10, 10, "Frame: " .. frame_count)
            gui.text(10, 30, "Time: " .. string.format("%.1f", current_time))
            gui.drawrect(10, 50, 100, 20, "green")
            gui.text(15, 55, "Rendering OK")
        end
    }
    
    local id = window.createLua("Debug Render", debug_window)
    print("✓ Debug window created:", id)
    
    -- Enable multi-viewport
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled")
    
    -- Check window count
    if window.getWindowCount then
        print("Total windows:", window.getWindowCount())
    end
    
else
    print("❌ Window API not available")
end

-- Check if overlay manager is calling window manager
print("\nChecking overlay manager integration...")
if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
    print("✓ Overlay manager exists")
    
    -- Try to manually trigger overlay render
    local success, error = pcall(function()
        LuaEngineGUI.g_overlay_manager:render()
    end)
    if success then
        print("✓ Manual overlay render succeeded")
    else
        print("❌ Manual overlay render failed:", error)
    end
else
    print("❌ Overlay manager not found")
end

print("\n=== Debug Window Rendering Complete ===")
print("Watch for frame counter messages every few seconds")
print("If you see frame messages, windows are rendering")
print("If not, the render loop integration is broken")