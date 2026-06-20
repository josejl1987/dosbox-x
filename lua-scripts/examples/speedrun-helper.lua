-- Speedrun Helper Example
-- ========================
-- This script provides tools useful for speedrunning DOS games.
-- It includes timing, checkpoint tracking, and save state management.

debug.log("=== Speedrun Helper Example ===")

-- Speedrun state variables
local speedrun_active = false
local start_time = 0
local checkpoint_times = {}
local current_checkpoint = 0
local best_times = {}

-- Configuration
local CHECKPOINT_ADDRESSES = {
    {seg = 0x1000, offset = 0x0500, name = "Level 1 Start"},
    {seg = 0x1000, offset = 0x0501, name = "Level 1 Boss"},
    {seg = 0x1000, offset = 0x0502, name = "Level 2 Start"},
    {seg = 0x1000, offset = 0x0503, name = "Level 2 Boss"},
    {seg = 0x1000, offset = 0x0504, name = "Final Boss"}
}

-- Function to start speedrun timing
function start_speedrun()
    speedrun_active = true
    start_time = os.clock()
    checkpoint_times = {}
    current_checkpoint = 0
    
    debug.log("=== SPEEDRUN STARTED ===")
    debug.log("Timer started at: " .. os.date("%H:%M:%S"))
    
    -- Auto-save at start
    savestate.quick_save()
    debug.log("Starting save state created")
end

-- Function to stop speedrun timing
function stop_speedrun()
    if not speedrun_active then
        debug.log("No speedrun currently active")
        return
    end
    
    local final_time = os.clock() - start_time
    speedrun_active = false
    
    debug.log("=== SPEEDRUN COMPLETED ===")
    debug.log("Final time: " .. format_time(final_time))
    
    -- Show checkpoint breakdown
    show_checkpoint_times()
    
    -- Check if this is a personal best
    local pb_key = "total_time"
    if not best_times[pb_key] or final_time < best_times[pb_key] then
        best_times[pb_key] = final_time
        debug.log("🎉 NEW PERSONAL BEST! 🎉")
        debug.log("Previous best: " .. (best_times[pb_key] and format_time(best_times[pb_key]) or "None"))
    end
end

-- Function to mark a checkpoint
function hit_checkpoint(checkpoint_name)
    if not speedrun_active then
        return
    end
    
    local checkpoint_time = os.clock() - start_time
    current_checkpoint = current_checkpoint + 1
    
    checkpoint_times[current_checkpoint] = {
        name = checkpoint_name or ("Checkpoint " .. current_checkpoint),
        time = checkpoint_time
    }
    
    debug.log("✓ " .. checkpoint_times[current_checkpoint].name .. 
             " reached at " .. format_time(checkpoint_time))
    
    -- Check for checkpoint personal best
    local pb_key = "checkpoint_" .. current_checkpoint
    if not best_times[pb_key] or checkpoint_time < best_times[pb_key] then
        best_times[pb_key] = checkpoint_time
        debug.log("  → New checkpoint PB!")
    end
    
    -- Auto-save at checkpoints
    savestate.set_current_slot(current_checkpoint)
    savestate.quick_save()
    debug.log("  → Checkpoint save created (slot " .. current_checkpoint .. ")")
end

-- Function to show current speedrun time
function show_current_time()
    if not speedrun_active then
        debug.log("No speedrun currently active")
        return
    end
    
    local current_time = os.clock() - start_time
    debug.log("Current time: " .. format_time(current_time))
end

-- Function to format time in MM:SS.mmm format
function format_time(seconds)
    local minutes = math.floor(seconds / 60)
    local secs = seconds % 60
    return string.format("%02d:%06.3f", minutes, secs)
end

-- Function to show checkpoint times
function show_checkpoint_times()
    if #checkpoint_times == 0 then
        debug.log("No checkpoints recorded")
        return
    end
    
    debug.log("=== Checkpoint Times ===")
    for i = 1, #checkpoint_times do
        local cp = checkpoint_times[i]
        local split_time = i == 1 and cp.time or (cp.time - checkpoint_times[i-1].time)
        debug.log(string.format("%d. %s: %s (split: %s)", 
                               i, cp.name, format_time(cp.time), format_time(split_time)))
    end
end

-- Function to show personal bests
function show_personal_bests()
    debug.log("=== Personal Best Times ===")
    
    if best_times.total_time then
        debug.log("Total time PB: " .. format_time(best_times.total_time))
    end
    
    for i = 1, #CHECKPOINT_ADDRESSES do
        local pb_key = "checkpoint_" .. i
        if best_times[pb_key] then
            debug.log(CHECKPOINT_ADDRESSES[i].name .. " PB: " .. format_time(best_times[pb_key]))
        end
    end
end

-- Function to reset personal bests
function reset_personal_bests()
    best_times = {}
    debug.log("Personal best times reset")
end

-- Function to load speedrun state from specific checkpoint
function load_checkpoint(checkpoint_number)
    if checkpoint_number < 1 or checkpoint_number > #CHECKPOINT_ADDRESSES then
        debug.log("Invalid checkpoint number: " .. checkpoint_number)
        return
    end
    
    savestate.set_current_slot(checkpoint_number)
    savestate.quick_load()
    debug.log("Loaded checkpoint " .. checkpoint_number .. ": " .. 
             CHECKPOINT_ADDRESSES[checkpoint_number].name)
end

-- Set up automatic checkpoint detection
-- These breakpoints should be placed at actual checkpoint locations in your game
function setup_checkpoint_breakpoints()
    for i, cp in ipairs(CHECKPOINT_ADDRESSES) do
        local checkpoint_name = cp.name
        local checkpoint_index = i
        
        breakpoint.add(cp.seg, cp.offset, "Checkpoint" .. i,
                      "", -- No condition, always trigger
                      "hit_checkpoint('" .. checkpoint_name .. "')")
    end
    
    debug.log("Set up " .. #CHECKPOINT_ADDRESSES .. " checkpoint breakpoints")
end

-- Practice mode functions
function practice_mode_start(checkpoint_number)
    checkpoint_number = checkpoint_number or 1
    load_checkpoint(checkpoint_number)
    start_speedrun()
    debug.log("Practice mode started from checkpoint " .. checkpoint_number)
end

-- Input replay system (basic)
local input_log = {}
local recording_inputs = false

function start_input_recording()
    input_log = {}
    recording_inputs = true
    debug.log("Input recording started")
end

function stop_input_recording()
    recording_inputs = false
    debug.log("Input recording stopped (" .. #input_log .. " inputs recorded)")
end

-- Frame-perfect timing helper
function frame_count()
    -- This would need integration with DOSBox-X frame counting
    -- For now, just estimate based on time (assuming 60 FPS)
    if speedrun_active then
        local elapsed = os.clock() - start_time
        return math.floor(elapsed * 60) -- Approximate frame count
    end
    return 0
end

-- Initialize checkpoint breakpoints
setup_checkpoint_breakpoints()

debug.log("Speedrun Helper loaded successfully!")
debug.log("")
debug.log("Available commands:")
debug.log("  start_speedrun() - Begin timing")
debug.log("  stop_speedrun() - End timing")
debug.log("  show_current_time() - Display current time")
debug.log("  hit_checkpoint(name) - Manual checkpoint")
debug.log("  show_checkpoint_times() - Checkpoint breakdown")
debug.log("  show_personal_bests() - Display PB times")
debug.log("  reset_personal_bests() - Clear PB times")
debug.log("  load_checkpoint(n) - Load specific checkpoint")
debug.log("  practice_mode_start(n) - Practice from checkpoint")
debug.log("")
debug.log("Remember to configure checkpoint addresses for your specific game!")