-- Test GUI overlay and windows in correct location
print("=== GUI Overlay Test (vs/lua-scripts) ===")

-- Enable GUI overlay
print("Enabling GUI overlay...")
if gui then
    gui.text("✅ GUI OVERLAY WORKING!")
    gui.drawText(100, 50, "Overlay text at (100,50)", "yellow")
    print("✓ GUI overlay commands executed")
else
    print("❌ GUI API not available")
end

-- Test window creation
print("Testing window creation...")
if window then
    -- Enable multi-viewport first
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled:", window.isMultiViewportEnabled())
    
    -- Create a simple test window
    local test_window = {
        title = "Test Window",
        render = function()
            gui.text("🎉 SUCCESS! Window is visible!")
            gui.text("Location: vs/lua-scripts/")
            gui.separator()
            
            if gui.button("Test Button") then
                print("✅ Button clicked in window!")
            end
            
            gui.text("Multi-viewport: " .. (window.isMultiViewportEnabled() and "ENABLED" or "DISABLED"))
            gui.text("Drag this window outside to detach it!")
        end
    }
    
    local window_id = window.createLua("VS Test Window", test_window)
    print("✓ Window created:", window_id)
    print("🎯 Look for the window on DOSBox-X screen!")
    
else
    print("❌ Window API not available")
end

print("=== Test Complete ===")
print("Check DOSBox-X screen for overlay text and window")