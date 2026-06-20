-- Try to manually trigger overlay rendering
print("=== Triggering Overlay Render ===")

-- Check if we can access the overlay manager directly
print("Checking overlay manager access...")

-- Try different ways to trigger rendering
if gui then
    gui.enable_overlay(true)
    print("✓ Overlay enabled")
    
    -- Draw something
    gui.text(50, 50, "MANUAL RENDER TEST")
    gui.drawrect(50, 70, 200, 30, "red")
    
    -- Try to find and call render functions
    local render_functions = {}
    for k, v in pairs(gui) do
        if type(v) == "function" and string.find(string.lower(k), "render") then
            render_functions[k] = v
            print("Found render function:", k)
        end
    end
    
    -- Try calling any render functions we found
    for name, func in pairs(render_functions) do
        local success, error = pcall(func)
        if success then
            print("✓ Called", name, "successfully")
        else
            print("❌ Failed to call", name, ":", error)
        end
    end
    
    -- Try to access global overlay manager
    if _G.LuaEngineGUI then
        print("Found LuaEngineGUI global")
        if _G.LuaEngineGUI.g_overlay_manager then
            print("Found overlay manager")
            -- Try to call render on it
            local success, error = pcall(function()
                _G.LuaEngineGUI.g_overlay_manager:render()
            end)
            if success then
                print("✓ Called overlay manager render")
            else
                print("❌ Overlay manager render failed:", error)
            end
        end
    end
    
    -- Check if there's a frame callback we can use
    if emu and emu.frameadvance then
        print("Trying frame advance to trigger render...")
        emu.frameadvance()
    end
    
else
    print("❌ GUI not available")
end

-- Create a window that tries to force rendering
if window then
    print("Creating window that tries to force rendering...")
    
    local render_window = {
        render = function()
            print("🔥 Window render - trying to force overlay")
            
            -- Try to trigger overlay rendering from within window
            gui.text(10, 10, "FORCE RENDER")
            
            -- Try to access overlay manager from window context
            if LuaEngineGUI and LuaEngineGUI.g_overlay_manager then
                LuaEngineGUI.g_overlay_manager:render()
            end
        end
    }
    
    local id = window.createLua("Force Render", render_window)
    print("✓ Force render window created:", id)
end

print("\n=== Manual Trigger Complete ===")
print("Check if any overlay content appeared")
print("Look for render function call messages")