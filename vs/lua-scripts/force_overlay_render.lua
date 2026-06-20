-- Force overlay rendering to trigger window rendering
print("=== Forcing Overlay Render ===")

-- The issue is that renderWindowManagerWindows() is only called 
-- when the overlay manager's render() method is invoked
-- And that only happens when render.scale.outWrite is true

print("Setting up windows...")
if window then
    window.enableMultiViewport()
    
    local test_window = {
        render = function()
            print("🔥 WINDOW RENDER CALLED! " .. os.clock())
            gui.text(20, 20, "SUCCESS! Window rendering!")
            gui.text(20, 40, "Time: " .. string.format("%.2f", os.clock()))
            gui.drawrect(20, 60, 200, 30, "green")
            gui.text(30, 70, "Multi-viewport working!")
        end
    }
    
    local window_id = window.createLua("Force Test", test_window)
    print("✓ Window created:", window_id)
end

-- Now force the overlay manager to render
print("Forcing overlay manager render...")

if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
    print("✓ Found overlay manager")
    
    -- Enable overlay
    gui.enable_overlay(true)
    print("✓ Overlay enabled")
    
    -- Draw some overlay content
    gui.text(100, 100, "FORCED OVERLAY RENDER")
    gui.drawrect(100, 120, 200, 50, "red")
    
    -- Force render the overlay manager
    local success, error = pcall(function()
        LuaEngineGUI.g_overlay_manager:render()
    end)
    
    if success then
        print("✓ Overlay manager render forced!")
        print("Check console for window render messages")
        print("Check screen for overlay content")
    else
        print("❌ Failed to force overlay render:", error)
    end
    
else
    print("❌ Overlay manager not found")
end

-- Try to trigger DOS screen activity to make render.scale.outWrite true
print("\nTrying to trigger DOS screen activity...")
print("Type 'dir' in DOS prompt and press Enter")
print("This should trigger normal overlay rendering")

-- Set up a frame callback to keep trying
if event and event.onframestart then
    local frame_count = 0
    event.onframestart(function()
        frame_count = frame_count + 1
        if frame_count <= 5 then
            print("Frame", frame_count, "- trying to force render")
            if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
                LuaEngineGUI.g_overlay_manager:render()
            end
        end
    end)
    print("✓ Frame callback set to force rendering")
end

print("\n=== Force Render Test Complete ===")
print("1. Check for window render messages")
print("2. Try typing 'dir' in DOS to trigger screen update")
print("3. Look for overlay content on screen")