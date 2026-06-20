-- Force GUI overlay to be visible
print("=== Forcing GUI Overlay Visibility ===")

-- Check if overlay manager exists and try to enable it
print("Checking overlay manager...")

-- Try to access the global overlay manager directly
if LuaEngineGUI then
    print("LuaEngineGUI namespace available")
    if LuaEngineGUI.g_overlay_manager then
        print("Overlay manager exists")
        -- Try to enable it
        LuaEngineGUI.g_overlay_manager:setEnabled(true)
        print("✓ Overlay manager enabled")
    else
        print("❌ No overlay manager found")
    end
else
    print("❌ LuaEngineGUI namespace not available")
end

-- Try alternative methods to show overlay
print("Trying alternative overlay activation...")

-- Method 1: Check if there's a show overlay function
if gui and gui.show then
    gui.show()
    print("✓ gui.show() called")
end

-- Method 2: Try to force overlay rendering
if gui and gui.setVisible then
    gui.setVisible(true)
    print("✓ gui.setVisible(true) called")
end

-- Method 3: Draw something that should definitely be visible
print("Drawing test overlay...")
gui.text("🔴 OVERLAY TEST - YOU SHOULD SEE THIS!")
gui.drawText(10, 10, "RED TEXT AT (10,10)", "red")
gui.drawText(10, 30, "YELLOW TEXT AT (10,30)", "yellow")
gui.drawText(10, 50, "WHITE TEXT AT (10,50)", "white")

-- Method 4: Create a very simple window with debug
print("Creating debug window...")
local debug_window = {
    render = function()
        print("🔥 RENDER FUNCTION IS BEING CALLED!")  -- This should appear in console
        gui.text("🟢 WINDOW IS RENDERING!")
        gui.text("If you see this, windows work!")
        gui.text("The overlay just needs to be visible.")
        
        if gui.button("CLICK ME") then
            print("🎯 BUTTON CLICKED - INTERACTION WORKS!")
        end
    end
}

local window_id = window.createLua("DEBUG VISIBILITY", debug_window)
print("✓ Debug window created:", window_id)

-- Check if render function gets called
print("Waiting for render function calls...")
print("Look for '🔥 RENDER FUNCTION IS BEING CALLED!' in console")

print("=== Overlay Force Complete ===")
print("If you see render function calls in console,")
print("then windows work but overlay isn't visible.")
print("Check DOSBox-X overlay settings!")