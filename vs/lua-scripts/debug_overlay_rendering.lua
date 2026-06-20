-- Debug overlay rendering system
print("=== Debugging Overlay Rendering ===")

-- Check overlay status
print("Checking overlay system...")
if gui then
    print("GUI API available")
    
    -- Check current overlay status
    local enabled = gui.is_overlay_enabled()
    print("Overlay enabled:", enabled)
    
    -- Force enable overlay
    gui.enable_overlay(true)
    print("Force enabled overlay")
    
    -- Check again
    enabled = gui.is_overlay_enabled()
    print("Overlay enabled after force:", enabled)
    
    -- Get screen info
    local screen = gui.get_screen_size()
    print("Screen size:", screen.width, "x", screen.height)
    
    -- Get overlay stats
    local stats = gui.get_stats()
    print("Overlay stats:")
    for k, v in pairs(stats) do
        print("  " .. k .. ":", v)
    end
    
    -- Try to draw something simple
    print("Attempting to draw...")
    gui.clear_all()  -- Clear any existing overlay
    gui.text(100, 100, "TEST OVERLAY")
    gui.drawrect(50, 50, 200, 200, "red")
    print("Draw commands executed")
    
else
    print("❌ GUI API not available")
end

-- Check if there's a render trigger we need to call
print("\nChecking for render triggers...")

-- Look for global functions that might trigger rendering
for k, v in pairs(_G) do
    if type(v) == "function" and (
        string.find(string.lower(k), "render") or 
        string.find(string.lower(k), "overlay") or
        string.find(string.lower(k), "gui")
    ) then
        print("Found global function:", k)
    end
end

-- Check if LuaEngineGUI has render functions
if LuaEngineGUI then
    print("LuaEngineGUI available:")
    for k, v in pairs(LuaEngineGUI) do
        print("  " .. k .. ":", type(v))
    end
end

print("\n=== Debug Complete ===")
print("Check console output for overlay status and stats")