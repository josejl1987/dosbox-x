-- Simple overlay test with correct GUI syntax
print("=== Simple Overlay Test ===")

-- Test basic GUI drawing with correct syntax
print("Testing GUI overlay with correct function calls...")

-- Use drawText instead of text (which expects coordinates)
gui.drawText(10, 10, "OVERLAY TEST - YOU SHOULD SEE THIS!", "white")
gui.drawText(10, 30, "If you see this text, GUI overlay works!", "yellow")
gui.drawText(10, 50, "Try pressing Ctrl+F1 to see mapper", "green")

print("✓ GUI overlay commands executed")

-- Create a simple window with correct syntax
print("Creating test window...")
if window then
    local simple_window = {
        render = function()
            print("🔥 Window render function called!")
            -- Use drawText in window context too
            gui.drawText(50, 50, "Window content!", "red")
            gui.drawText(50, 70, "This is inside a window", "blue")
        end
    }
    
    local window_id = window.createLua("Simple Test", simple_window)
    print("✓ Window created:", window_id)
else
    print("❌ Window API not available")
end

print("\n=== Test Instructions ===")
print("1. Look at the DOSBox-X screen for white/yellow/green text")
print("2. Try pressing Ctrl+F1 to see if ImGui mapper appears")
print("3. Check console for 'Window render function called!' messages")
print("4. If you see render messages, windows work but aren't visible")

print("\n=== Test Complete ===")
print("The overlay should now be visible on screen!")