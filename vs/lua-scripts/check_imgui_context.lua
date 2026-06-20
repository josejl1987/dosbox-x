-- Check if ImGui context and rendering is working
print("=== Checking ImGui Context ===")

-- The issue is that our ImGui overlay isn't rendering
-- Let's check if ImGui is properly initialized and accessible

print("Checking ImGui availability...")

-- Check if we can access ImGui functions directly
local imgui_available = false
if ImGui then
    print("✓ ImGui global available")
    imgui_available = true
    
    -- Try to get ImGui context info
    if ImGui.GetCurrentContext then
        local context = ImGui.GetCurrentContext()
        print("ImGui context:", context and "exists" or "nil")
    end
    
    if ImGui.GetIO then
        local io = ImGui.GetIO()
        print("ImGui IO available:", io and "yes" or "no")
        if io then
            print("Display size:", io.DisplaySize.x, "x", io.DisplaySize.y)
        end
    end
else
    print("❌ ImGui global not available")
end

-- Check if our overlay manager exists and is properly initialized
print("\nChecking overlay manager...")
if LuaEngineGUI then
    print("✓ LuaEngineGUI namespace exists")
    
    if LuaEngineGUI.g_overlay_manager then
        print("✓ g_overlay_manager exists")
        
        local manager = LuaEngineGUI.g_overlay_manager
        
        -- Check if manager has the methods we expect
        local methods = {"render", "setScreenSize", "isEnabled", "setEnabled"}
        for _, method in ipairs(methods) do
            if manager[method] then
                print("✓ Manager has", method, "method")
            else
                print("❌ Manager missing", method, "method")
            end
        end
        
        -- Try to check if overlay is enabled
        local success, result = pcall(function()
            return manager:isEnabled()
        end)
        if success then
            print("Manager enabled:", result)
            if not result then
                print("Trying to enable manager...")
                manager:setEnabled(true)
            end
        else
            print("Could not check manager status:", result)
        end
        
    else
        print("❌ g_overlay_manager not found")
    end
else
    print("❌ LuaEngineGUI namespace not found")
end

-- Check if the window manager is working
print("\nChecking window manager...")
if window then
    print("✓ Window API available")
    
    -- Create a simple test window
    local test_window = {
        render = function()
            print("🔥 Test window render called at", os.clock())
            
            -- Try to use ImGui directly if available
            if ImGui and ImGui.Text then
                ImGui.Text("Direct ImGui call")
                if ImGui.Button then
                    if ImGui.Button("Direct ImGui Button") then
                        print("Direct ImGui button clicked!")
                    end
                end
            end
        end
    }
    
    local id = window.createLua("ImGui Test", test_window)
    print("✓ Test window created:", id)
    
    -- Check window count
    if window.getWindowCount then
        print("Total windows:", window.getWindowCount())
    end
    
else
    print("❌ Window API not available")
end

print("\n=== ImGui Context Check Complete ===")
print("Look for render messages and check if ImGui is accessible")