-- Enable GUI overlay and test windows
print("=== Enabling GUI Overlay ===")

-- First, let's check if the overlay manager exists and enable it
print("Checking overlay manager...")

-- Try to access the global overlay manager
local overlay_available = false
if gui then
    print("GUI API available")
    
    -- Try to enable overlay if there's an enable function
    if gui.enable then
        gui.enable(true)
        print("✓ GUI overlay enabled via gui.enable()")
        overlay_available = true
    end
    
    -- Try alternative enable methods
    if gui.setEnabled then
        gui.setEnabled(true)
        print("✓ GUI overlay enabled via gui.setEnabled()")
        overlay_available = true
    end
    
    -- Check if we can draw directly
    print("Testing direct drawing...")
    gui.text("OVERLAY TEST - You should see this on screen!")
    gui.drawText(50, 50, "Direct overlay text", "yellow")
    
    overlay_available = true
else
    print("❌ GUI API not available")
end

-- Now test window creation if overlay is working
if overlay_available and window then
    print("Creating test window...")
    
    local visible_window = {
        title = "VISIBLE TEST",
        render = function()
            gui.text("🎉 SUCCESS! Window is visible!")
            gui.text("Multi-viewport is working!")
            gui.separator()
            gui.text("Try dragging this window outside")
            gui.text("the main DOSBox-X area!")
            
            if gui.button("Click Me!") then
                print("✅ Button clicked! Window interaction works!")
            end
            
            gui.separator()
            gui.text("Window system: ACTIVE ✓")
            gui.text("Multi-viewport: " .. (window.isMultiViewportEnabled() and "ENABLED ✓" or "DISABLED"))
        end
    }
    
    local window_id = window.createLua("Visible Test", visible_window)
    print("✓ Window created:", window_id)
    
    -- Enable multi-viewport to make it detachable
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled")
    
    print("🎯 Look for the window on your DOSBox-X screen!")
    print("🖱️ Try dragging it outside to detach it!")
    
else
    print("❌ Cannot create window - overlay or window API not available")
end

-- Final status
print("\n=== Status Summary ===")
print("GUI API:", gui and "✓ Available" or "❌ Missing")
print("Window API:", window and "✓ Available" or "❌ Missing") 
print("Overlay enabled:", overlay_available and "✓ Yes" or "❌ No")

if overlay_available then
    print("\n🎉 GUI overlay should now be visible!")
    print("Look for text and windows on the DOSBox-X screen.")
else
    print("\n❌ GUI overlay could not be enabled.")
    print("Check DOSBox-X configuration for overlay settings.")
end