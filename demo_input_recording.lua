-- DOSBox-X Enhanced Lua Input Recording Demo
-- BizHawk-inspired TAS tools demonstration

print("DOSBox-X Input Recording Demo Starting...")

-- Initialize input recording
local recording_active = false
local current_movie = "demo_tas.dbxtas"

-- Basic joypad input demonstration
function demo_joypad_input()
    print("=== Joypad Input Demo ===")
    
    -- Get current joypad state for player 1
    local joy1 = joypad.get(1)
    print("Player 1 current input:")
    for button, pressed in pairs(joy1) do
        if pressed then
            print("  " .. button .. ": PRESSED")
        end
    end
    
    -- Set specific input for player 1
    local new_input = {
        Up = false,
        Down = false,
        Left = true,   -- Move left
        Right = false,
        A = true,      -- Press A button
        B = false,
        Start = false,
        Select = false
    }
    
    joypad.set(new_input, 1)
    print("Set player 1 to: Left + A")
end

-- Movie recording demonstration
function demo_movie_recording()
    print("=== Movie Recording Demo ===")
    
    if not recording_active then
        local success = movie.startrecording(current_movie, "Demo Game", "Claude AI")
        if success then
            recording_active = true
            print("Started recording to: " .. current_movie)
        else
            print("Failed to start recording")
        end
    else
        local success = movie.stoprecording()
        if success then
            recording_active = false
            print("Stopped recording")
            
            -- Show movie statistics
            local stats = tas.getmoviestats()
            print("Movie Statistics:")
            print("  Total frames: " .. stats.total_frames)
            print("  Input frames: " .. stats.input_frames)
            print("  Lag frames: " .. stats.lag_frames)
            print("  Rerecords: " .. stats.rerecords)
            print("  Average FPS: " .. string.format("%.2f", stats.average_fps))
            print("  Total time: " .. stats.total_time_ms .. "ms")
        end
    end
end

-- Movie playback demonstration
function demo_movie_playback()
    print("=== Movie Playback Demo ===")
    
    if not movie.isplaying() then
        local success = movie.startplayback(current_movie)
        if success then
            print("Started playback of: " .. current_movie)
            print("Movie length: " .. movie.getlength() .. " frames")
        else
            print("Failed to start playback (file may not exist)")
        end
    else
        movie.stopplayback()
        print("Stopped playback")
    end
end

-- Advanced TAS features demonstration
function demo_tas_features()
    print("=== TAS Features Demo ===")
    
    -- Frame counter
    local frame = tas.getframecounter()
    print("Current frame: " .. frame)
    
    -- Lag frame detection
    local lag_count = tas.getlagcounter()
    print("Total lag frames: " .. lag_count)
    
    if frame > 0 then
        local is_lag = tas.islagframe(frame)
        print("Current frame is lag frame: " .. tostring(is_lag))
    end
    
    -- Input change detection
    local changes = tas.findinputchanges()
    print("Input changes found at frames:")
    for i = 1, math.min(10, #changes) do  -- Show first 10 changes
        print("  Frame " .. changes[i])
    end
    
    -- Speed control
    print("Current speed: " .. tas.getspeed())
    
    -- Movie metadata
    if movie.isrecording() or movie.isplaying() then
        local metadata = tas.getmoviedata()
        print("Movie metadata:")
        print("  Game: " .. metadata.game_name)
        print("  Author: " .. metadata.author)
        print("  Version: " .. metadata.version)
    end
end

-- Input pattern analysis
function analyze_input_patterns()
    print("=== Input Pattern Analysis ===")
    
    if movie.isplaying() then
        local current_frame = movie.getframecounter()
        print("Analyzing input patterns around frame " .. current_frame)
        
        -- Find recent input changes
        local changes = tas.findinputchanges(1)  -- Player 1 only
        local recent_changes = {}
        
        for _, frame_num in ipairs(changes) do
            if frame_num >= current_frame - 60 and frame_num <= current_frame + 60 then
                table.insert(recent_changes, frame_num)
            end
        end
        
        print("Recent input changes for Player 1 (±60 frames):")
        for _, frame_num in ipairs(recent_changes) do
            print("  Frame " .. frame_num)
        end
    end
end

-- Frame advancement demo
function demo_frame_advance()
    print("=== Frame Advance Demo ===")
    
    if movie.isplaying() then
        local current = movie.getframecounter()
        print("Current playback frame: " .. current)
        
        -- Seek forward 60 frames
        local target = current + 60
        if tas.seektoframe(target) then
            print("Seeked to frame: " .. target)
        else
            print("Seek failed")
        end
    else
        print("Not currently playing a movie")
    end
end

-- Save state integration demo
function demo_savestate_integration()
    print("=== Save State Integration Demo ===")
    
    if savestate then
        print("Save states available: " .. savestate.getslotcount())
        
        -- Quick save/load demo
        local slot = 1
        print("Quick saving to slot " .. slot)
        savestate.save(slot)
        
        -- In a real scenario, you'd do something here and then load
        print("Quick loading from slot " .. slot)
        savestate.load(slot)
        
        -- Show persistence of Lua data
        if recording_active then
            print("Recording state preserved across save/load")
        end
    else
        print("Save state system not available")
    end
end

-- Main demo loop
function run_demo()
    print("DOSBox-X Enhanced Lua Input Recording System")
    print("BizHawk-inspired TAS Tools Demonstration")
    print("=" .. string.rep("=", 50))
    
    -- Demo sequence
    demo_joypad_input()
    print()
    
    demo_movie_recording()
    print()
    
    -- Record some sample input for a few frames
    if recording_active then
        print("Recording 10 frames of sample input...")
        for i = 1, 10 do
            local input = {
                Up = (i % 4) == 1,
                Down = (i % 4) == 3,
                Left = (i % 3) == 1,
                Right = (i % 3) == 2,
                A = (i % 2) == 0,
                B = (i % 5) == 0
            }
            joypad.set(input, 1)
            emu.frameadvance()  -- This would advance one frame
        end
        print("Sample input recorded")
        print()
        
        demo_movie_recording()  -- Stop recording
        print()
    end
    
    demo_movie_playback()
    print()
    
    demo_tas_features()
    print()
    
    analyze_input_patterns()
    print()
    
    demo_frame_advance()
    print()
    
    demo_savestate_integration()
    print()
    
    print("Demo completed!")
    print("Available APIs:")
    print("  joypad.get(player) - Get current joypad state")
    print("  joypad.set(input, player) - Set joypad input")
    print("  movie.startrecording(file, game, author) - Start recording")
    print("  movie.stoprecording() - Stop recording")
    print("  movie.startplayback(file) - Start playback")
    print("  movie.stopplayback() - Stop playback")
    print("  movie.save(file) - Save movie")
    print("  movie.load(file) - Load movie")
    print("  movie.getlength() - Get movie length")
    print("  movie.getframecounter() - Get current frame")
    print("  movie.seektoframe(frame) - Seek to frame")
    print("  movie.isrecording() - Check if recording")
    print("  movie.isplaying() - Check if playing")
    print("  movie.getrerecordcount() - Get rerecord count")
    print("  tas.getframecounter() - Get frame counter")
    print("  tas.getlagcounter() - Get lag frame count")
    print("  tas.islagframe(frame) - Check if frame is lag")
    print("  tas.findinputchanges(player) - Find input changes")
    print("  tas.getmoviestats() - Get movie statistics")
    print("  tas.setspeed(mode) - Set emulation speed")
    print("  tas.getspeed() - Get current speed")
    print("  tas.setmoviedata(name, author, desc) - Set movie metadata")
    print("  tas.getmoviedata() - Get movie metadata")
    print("  tas.seektoframe(frame) - Seek to specific frame")
    print("  tas.seektoinputchange(start, forward, player) - Seek to input change")
end

-- Event handlers for integration with the emulator
if event then
    -- Frame start event handler
    event.onframestart(function()
        -- This would be called at the start of each frame
        -- Could be used for automatic input recording, lag detection, etc.
    end)
    
    -- Memory read/write event handlers for TAS verification
    event.onmemoryread(function(segment, offset, value, size)
        -- Could log memory reads for TAS verification
        -- Example: check for RNG reads, input polling, etc.
    end)
    
    event.onmemorywrite(function(segment, offset, value, size)
        -- Could log memory writes for state changes
        -- Example: detect game state changes, score updates, etc.
    end)
end

-- Auto-run demo if this script is loaded directly
if not pcall(function() return recording_active end) then
    run_demo()
end

print("Input recording system initialized!")
print("Call run_demo() to see all features in action.")