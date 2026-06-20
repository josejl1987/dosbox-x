-- Force screen activity to trigger overlay rendering
print("=== Forcing Screen Activity ===")

-- The overlay only renders when render.scale.outWrite is true
-- This happens when DOSBox-X is actively drawing to the screen
-- We need to force some screen activity

print("Setting up overlay...")
if gui then
    gui.enable_overlay(true)
    gui.clear_all()
    
    -- Draw overlay content
    gui.text(50, 50, "🎉 OVERLAY ACTIVE!")
    gui.text(50, 70, "Multi-viewport ready")
    gui.drawrect(50, 90, 200, 30, "green")
    gui.text(60, 100, "Success indicator")
    
    print("✓ Overlay content prepared")
end

-- Create windows
if window then
    window.enableMultiViewport()
    
    local main_window = {
        render = function()
            gui.text(20, 20, "✅ DETACHABLE WINDOW")
            gui.text(20, 40, "Drag me outside!")
            gui.drawrect(20, 60, 150, 25, "blue")
            gui.text(30, 68, "Multi-viewport ON")
        end
    }
    
    local tool_window = {
        render = function()
            gui.text(10, 10, "🛠️ Tool Panel")
            gui.text(10, 30, "Professional workflow")
            gui.drawrect(10, 50, 120, 20, "purple")
            gui.text(15, 55, "Detachable tool")
        end
    }
    
    local window1 = window.createLua("Main Window", main_window)
    local window2 = window.createLua("Tool Panel", tool_window)
    
    print("✓ Windows created:", window1, window2)
end

-- Now force screen activity to trigger overlay rendering
print("Forcing screen activity...")

-- Method 1: Try to trigger DOS screen updates
if emu then
    print("Trying emu.frameadvance()...")
    for i = 1, 5 do
        emu.frameadvance()
    end
end

-- Method 2: Try to access DOS screen directly
print("Trying to trigger DOS output...")

-- Method 3: Use console commands that might trigger screen updates
print("Try typing in the DOS prompt or running a command")
print("The overlay should appear when DOS updates the screen")

-- Method 4: Create a frame callback to keep trying
if event and event.onframestart then
    local frame_count = 0
    event.onframestart(function()
        frame_count = frame_count + 1
        if frame_count <= 10 then
            print("Frame", frame_count, "- overlay should be visible now")
        end
    end)
    print("✓ Frame callback registered")
end

print("\n🎯 INSTRUCTIONS:")
print("1. Type something in the DOS prompt (like 'dir')")
print("2. Press Enter to execute the command")
print("3. The screen activity should trigger overlay rendering")
print("4. Look for green overlay text and detachable windows")
print("5. Try dragging windows outside the main area")

print("\n✅ Multi-viewport system is ready!")
print("Overlay will appear when DOS screen updates")