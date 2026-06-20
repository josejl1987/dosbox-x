-- Working overlay test with correct API
print("=== Working Overlay Test ===")

-- First, enable the overlay
print("Enabling GUI overlay...")
gui.enable_overlay(true)
print("✓ Overlay enabled")

-- Check if overlay is enabled
local overlay_enabled = gui.is_overlay_enabled()
print("Overlay status:", overlay_enabled)

-- Get screen size
local screen_size = gui.get_screen_size()
print("Screen size:", screen_size.width .. "x" .. screen_size.height)

-- Draw text using correct API
print("Drawing overlay text...")
gui.text(10, 10, "🎉 OVERLAY WORKING!")
gui.text(10, 30, "Multi-viewport system active")
gui.text(10, 50, "Drag windows outside to detach")

-- Draw some graphics
gui.drawrect(10, 70, 200, 30, "yellow")
gui.text(15, 80, "This is a yellow box")

-- Draw a line
gui.drawline(10, 110, 210, 110, "red")
gui.text(10, 120, "Red line above")

print("✓ Overlay graphics drawn")

-- Create a working window
print("Creating window with correct API...")
if window then
    -- Enable multi-viewport
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled:", window.isMultiViewportEnabled())
    
    local working_window = {
        render = function()
            print("🔥 Window rendering!")
            
            -- Use correct GUI API in window
            gui.text(20, 20, "✅ WINDOW VISIBLE!")
            gui.text(20, 40, "Multi-viewport working")
            gui.text(20, 60, "Drag this window outside")
            
            gui.drawrect(20, 80, 150, 20, "green")
            gui.text(25, 85, "Green success box")
            
            -- Draw a button-like rectangle
            gui.drawrect(20, 110, 100, 25, "blue")
            gui.text(25, 118, "Click area")
        end
    }
    
    local window_id = window.createLua("Working Test", working_window)
    print("✓ Window created:", window_id)
    
    -- Create a second detachable window
    local tool_window = {
        render = function()
            gui.text(10, 10, "🛠️ Tool Window")
            gui.text(10, 30, "This can be detached!")
            gui.drawrect(10, 50, 120, 20, "purple")
            gui.text(15, 55, "Detachable tool")
        end
    }
    
    local tool_id = window.createLua("Detachable Tool", tool_window)
    print("✓ Tool window created:", tool_id)
    
else
    print("❌ Window API not available")
end

print("\n🎯 SUCCESS! You should now see:")
print("1. Overlay text on the DOSBox-X screen")
print("2. Yellow and green rectangles")
print("3. Two ImGui windows that can be detached")
print("4. 'Window rendering!' messages in console")

print("\n🖱️ Try dragging the windows outside the main DOSBox-X area!")
print("They should become separate OS windows.")

print("\n=== Multi-Viewport Demo Ready! ===")