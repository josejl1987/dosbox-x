-- Final overlay test - try everything
print("=== Final Overlay Test ===")

-- Run the debug script first
print("Running debug overlay script...")
if gui then
    print("1. Enabling overlay...")
    gui.enable_overlay(true)
    
    print("2. Checking status...")
    print("   Overlay enabled:", gui.is_overlay_enabled())
    
    print("3. Getting screen info...")
    local screen = gui.get_screen_size()
    print("   Screen:", screen.width .. "x" .. screen.height)
    
    print("4. Drawing overlay...")
    gui.clear_all()
    
    -- Try drawing at different positions
    gui.text(0, 0, "TOP LEFT")
    gui.text(100, 100, "CENTER")
    gui.text(200, 200, "LOWER")
    
    -- Try bright colors
    gui.drawrect(0, 20, 300, 50, "red")
    gui.text(10, 35, "RED RECTANGLE")
    
    gui.drawrect(0, 80, 300, 50, "yellow") 
    gui.text(10, 95, "YELLOW RECTANGLE")
    
    gui.drawrect(0, 140, 300, 50, "white")
    gui.text(10, 155, "WHITE RECTANGLE")
    
    print("5. Overlay drawing complete")
    
    -- Try to force a render
    if gui.render then
        gui.render()
        print("   Forced render")
    end
    
else
    print("❌ GUI not available")
end

-- Try creating windows anyway
print("\n6. Testing windows...")
if window then
    window.enableMultiViewport()
    
    local test_window = {
        render = function()
            print("🔥 WINDOW RENDER CALLED - " .. os.clock())
        end
    }
    
    local id = window.createLua("Test", test_window)
    print("   Window created:", id)
    print("   Multi-viewport:", window.isMultiViewportEnabled())
    
    -- Get window info
    if window.getWindowCount then
        print("   Window count:", window.getWindowCount())
    end
    
else
    print("❌ Window not available")
end

print("\n=== INSTRUCTIONS ===")
print("1. Run: lua_load lua-scripts/debug_overlay_rendering.lua")
print("2. Look for 'Window render called' messages")
print("3. Try pressing Ctrl+F1 in DOSBox-X")
print("4. Check if any ImGui windows appear")
print("5. Look at the entire DOSBox-X screen carefully")

print("\nIf you see render messages but no visuals,")
print("the overlay system works but isn't visible.")
print("This might be a DOSBox-X configuration issue.")