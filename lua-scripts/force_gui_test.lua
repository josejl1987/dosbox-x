-- Force GUI test - try multiple approaches
print("=== Forcing GUI Display ===")

-- Method 1: Direct GUI calls
print("Method 1: Direct GUI overlay")
gui.text("DIRECT GUI TEST - This should appear on screen")
gui.drawText(100, 100, "GUI OVERLAY TEST", "white")

-- Method 2: Try to force overlay enable
print("Method 2: Check overlay manager")
if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
    print("Overlay manager exists")
else
    print("No overlay manager found")
end

-- Method 3: Window creation with debug
print("Method 3: Window creation with verbose debug")
if window then
    print("Creating window with debug info...")
    
    local debug_window = {
        title = "FORCE TEST",
        render = function()
            print("RENDER FUNCTION CALLED!")  -- This should appear in console
            gui.text("WINDOW CONTENT RENDERING")
            gui.text("If you see this, windows work!")
            
            if gui.button("TEST BUTTON") then
                print("BUTTON CLICKED IN WINDOW!")
            end
        end
    }
    
    local id = window.createLua("FORCE_TEST", debug_window)
    print("Window created, ID:", id)
    
    -- Try to get window info
    if window.getWindowCount then
        print("Total windows:", window.getWindowCount())
    end
    
    if window.getWindowIds then
        local ids = window.getWindowIds()
        print("Window IDs:", table.concat(ids, ", "))
    end
else
    print("ERROR: No window API!")
end

-- Method 4: Check if we need to enable something
print("Method 4: Checking what needs to be enabled")

-- Try to enable overlay explicitly
if gui and gui.enable then
    gui.enable(true)
    print("GUI enabled")
end

-- Try to show overlay
if gui and gui.show then
    gui.show(true)
    print("GUI shown")
end

print("=== Test Complete ===")
print("Look for:")
print("1. Text on DOSBox-X screen")
print("2. 'RENDER FUNCTION CALLED!' in console")
print("3. Any visible windows or overlays")