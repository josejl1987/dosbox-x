-- Debug window creation
print("=== Window Creation Debug ===")

-- Check if GUI overlay is working
print("Testing GUI overlay...")
gui.text("If you see this in console, GUI overlay is working")

-- Check window system
print("Window API available:", window ~= nil)
if window then
    print("Functions available:")
    for k, v in pairs(window) do
        print("  " .. k .. ":", type(v))
    end
    
    -- Try to create a very simple window
    print("Creating simple window...")
    
    local test_window = {
        render = function()
            gui.text("TEST WINDOW VISIBLE!")
            gui.text("This should appear somewhere")
        end
    }
    
    local window_id = window.createLua("DEBUG", test_window)
    print("Window created with ID:", window_id)
    
    -- Check window manager state
    print("Window count:", window.getWindowCount and window.getWindowCount() or "unknown")
    print("Window IDs:", window.getWindowIds and table.concat(window.getWindowIds(), ", ") or "unknown")
else
    print("ERROR: Window API not available!")
end

-- Check if overlay manager is active
print("Checking overlay system...")
if gui then
    print("GUI functions available:")
    for k, v in pairs(gui) do
        print("  " .. k .. ":", type(v))
    end
end

print("=== Debug Complete ===")
print("Check DOSBox-X screen for any visible windows")