-- DOSBox-X Segment Mapping Utility
-- Advanced segment mapping and memory layout management

-- Predefined memory layouts for common scenarios
local memory_layouts = {
    -- Standard DOS program layout
    dos_standard = {
        base = 0x10000,
        segments = {
            {name = "_TEXT", offset = 0x00000, size = 0x10000},
            {name = "_DATA", offset = 0x10000, size = 0x08000},
            {name = "_BSS",  offset = 0x18000, size = 0x04000},
            {name = "_STACK", offset = 0x1C000, size = 0x04000}
        }
    },
    
    -- TSR (Terminate and Stay Resident) layout
    tsr_layout = {
        base = 0xA0000,
        segments = {
            {name = "_TEXT", offset = 0x00000, size = 0x08000},
            {name = "_DATA", offset = 0x08000, size = 0x04000},
            {name = "_BSS",  offset = 0x0C000, size = 0x02000}
        }
    },
    
    -- Game/Graphics program layout
    game_layout = {
        base = 0x10000,
        segments = {
            {name = "_TEXT", offset = 0x00000, size = 0x20000},  -- Larger code segment
            {name = "_DATA", offset = 0x20000, size = 0x10000},  -- Game data
            {name = "_GRAPHICS", offset = 0x30000, size = 0x20000}, -- Graphics data
            {name = "_SOUND", offset = 0x50000, size = 0x08000},  -- Sound data
            {name = "_STACK", offset = 0x58000, size = 0x08000}
        }
    },
    
    -- Overlay program layout
    overlay_layout = {
        base = 0x10000,
        segments = {
            {name = "_TEXT", offset = 0x00000, size = 0x08000},   -- Main code
            {name = "_OVERLAY1", offset = 0x08000, size = 0x08000}, -- Overlay 1
            {name = "_OVERLAY2", offset = 0x08000, size = 0x08000}, -- Overlay 2 (same address)
            {name = "_DATA", offset = 0x10000, size = 0x08000},
            {name = "_STACK", offset = 0x18000, size = 0x08000}
        }
    }
}

-- Apply a predefined memory layout
function applyMemoryLayout(layout_name, base_address)
    local layout = memory_layouts[layout_name]
    if not layout then
        print("ERROR: Unknown layout '" .. layout_name .. "'")
        print("Available layouts: " .. table.concat(getLayoutNames(), ", "))
        return false
    end
    
    base_address = base_address or layout.base
    
    print("Applying memory layout: " .. layout_name)
    print("Base address: " .. string.format("%08X", base_address))
    
    symbols.clearSegmentMappings()
    
    for _, seg in ipairs(layout.segments) do
        local mapping = {
            segment_name = seg.name,
            file_segment = 0x1000, -- Will be auto-detected
            memory_base = base_address + seg.offset,
            size = seg.size,
            enabled = true
        }
        
        symbols.addSegmentMapping(mapping)
        print(string.format("  %s: %08X - %08X (Size: %08X)", 
            seg.name, 
            mapping.memory_base, 
            mapping.memory_base + seg.size - 1,
            seg.size))
    end
    
    symbols.remapAllSymbols()
    print("✓ Layout applied and symbols remapped")
    return true
end

-- Get list of available layout names
function getLayoutNames()
    local names = {}
    for name, _ in pairs(memory_layouts) do
        table.insert(names, name)
    end
    table.sort(names)
    return names
end

-- Create custom memory layout interactively
function createCustomLayout()
    print("\n=== Custom Memory Layout Creator ===")
    
    -- This would be interactive in a real implementation
    print("Enter base address (hex, e.g., 10000):")
    local base_address = 0x10000  -- Would get from user input
    
    print("Creating custom layout at base " .. string.format("%08X", base_address))
    
    symbols.clearSegmentMappings()
    
    -- Example custom segments
    local custom_segments = {
        {name = "CODE", base = base_address, size = 0x8000},
        {name = "DATA", base = base_address + 0x8000, size = 0x4000},
        {name = "HEAP", base = base_address + 0xC000, size = 0x4000}
    }
    
    for _, seg in ipairs(custom_segments) do
        local mapping = {
            segment_name = seg.name,
            file_segment = 0x1000,
            memory_base = seg.base,
            size = seg.size,
            enabled = true
        }
        
        symbols.addSegmentMapping(mapping)
        print(string.format("Added %s: %08X - %08X", 
            seg.name, seg.base, seg.base + seg.size - 1))
    end
    
    symbols.remapAllSymbols()
    print("✓ Custom layout created")
end

-- Analyze current memory layout
function analyzeMemoryLayout()
    print("\n=== Memory Layout Analysis ===")
    
    local mappings = symbols.getSegmentMappings()
    if #mappings == 0 then
        print("No segment mappings defined")
        return
    end
    
    -- Sort by memory base address
    table.sort(mappings, function(a, b) return a.memory_base < b.memory_base end)
    
    print("Current memory layout:")
    print("Segment        | Start    | End      | Size     | Status")
    print("---------------|----------|----------|----------|--------")
    
    local total_size = 0
    local last_end = 0
    
    for _, mapping in ipairs(mappings) do
        local start_addr = mapping.memory_base
        local end_addr = start_addr + mapping.size - 1
        local status = mapping.enabled and "enabled" or "disabled"
        
        print(string.format("%-14s | %08X | %08X | %08X | %s",
            mapping.segment_name, start_addr, end_addr, mapping.size, status))
        
        if mapping.enabled then
            total_size = total_size + mapping.size
            
            -- Check for gaps
            if last_end > 0 and start_addr > last_end + 1 then
                local gap_size = start_addr - last_end - 1
                print(string.format("               | %08X | %08X | %08X | GAP",
                    last_end + 1, start_addr - 1, gap_size))
            end
            
            last_end = end_addr
        end
    end
    
    print("---------------|----------|----------|----------|--------")
    print(string.format("Total mapped:  |          |          | %08X |", total_size))
    
    -- Check for overlaps
    checkMemoryOverlaps(mappings)
end

-- Check for memory overlaps in segment mappings
function checkMemoryOverlaps(mappings)
    print("\n=== Overlap Analysis ===")
    
    local enabled_mappings = {}
    for _, mapping in ipairs(mappings) do
        if mapping.enabled then
            table.insert(enabled_mappings, mapping)
        end
    end
    
    table.sort(enabled_mappings, function(a, b) return a.memory_base < b.memory_base end)
    
    local overlaps_found = false
    
    for i = 1, #enabled_mappings - 1 do
        local current = enabled_mappings[i]
        local next_mapping = enabled_mappings[i + 1]
        
        local current_end = current.memory_base + current.size - 1
        local next_start = next_mapping.memory_base
        
        if current_end >= next_start then
            overlaps_found = true
            local overlap_size = current_end - next_start + 1
            print(string.format("OVERLAP: %s and %s overlap by %08X bytes",
                current.segment_name, next_mapping.segment_name, overlap_size))
            print(string.format("  %s: %08X - %08X", 
                current.segment_name, current.memory_base, current_end))
            print(string.format("  %s: %08X - %08X", 
                next_mapping.segment_name, next_start, next_start + next_mapping.size - 1))
        end
    end
    
    if not overlaps_found then
        print("✓ No overlaps detected")
    end
end

-- Optimize memory layout to eliminate gaps and overlaps
function optimizeMemoryLayout(base_address)
    base_address = base_address or 0x10000
    
    print("Optimizing memory layout starting at " .. string.format("%08X", base_address))
    
    local mappings = symbols.getSegmentMappings()
    if #mappings == 0 then
        print("No mappings to optimize")
        return
    end
    
    -- Sort by original order (could be by name or size)
    table.sort(mappings, function(a, b) return a.segment_name < b.segment_name end)
    
    local current_base = base_address
    
    for _, mapping in ipairs(mappings) do
        if mapping.enabled then
            print(string.format("Moving %s from %08X to %08X", 
                mapping.segment_name, mapping.memory_base, current_base))
            
            symbols.updateSegmentMapping(mapping.segment_name, current_base)
            current_base = current_base + mapping.size
        end
    end
    
    print("✓ Memory layout optimized")
    analyzeMemoryLayout()
end

-- Save current memory layout to a file (conceptual)
function saveMemoryLayout(filename)
    local mappings = symbols.getSegmentMappings()
    
    print("Saving memory layout to: " .. filename)
    
    -- In a real implementation, this would write to a file
    print("Layout configuration:")
    for _, mapping in ipairs(mappings) do
        print(string.format("%s=%08X,%08X,%s", 
            mapping.segment_name, 
            mapping.memory_base, 
            mapping.size,
            mapping.enabled and "enabled" or "disabled"))
    end
    
    print("✓ Layout saved (conceptual)")
end

-- Load memory layout from a file (conceptual)
function loadMemoryLayout(filename)
    print("Loading memory layout from: " .. filename)
    
    -- In a real implementation, this would read from a file
    -- For now, demonstrate with example data
    local example_layout = {
        {segment_name = "_TEXT", memory_base = 0x10000, size = 0x8000, enabled = true},
        {segment_name = "_DATA", memory_base = 0x18000, size = 0x4000, enabled = true}
    }
    
    symbols.clearSegmentMappings()
    
    for _, mapping in ipairs(example_layout) do
        mapping.file_segment = 0x1000  -- Default
        symbols.addSegmentMapping(mapping)
        print(string.format("Loaded %s at %08X", mapping.segment_name, mapping.memory_base))
    end
    
    symbols.remapAllSymbols()
    print("✓ Layout loaded and applied")
end

-- Interactive segment mapper
function interactiveMapper()
    print("\n=== Interactive Segment Mapper ===")
    print("1. Apply predefined layout")
    print("2. Create custom layout")
    print("3. Analyze current layout")
    print("4. Optimize layout")
    print("5. Save layout")
    print("6. Load layout")
    print("")
    
    -- In a real implementation, this would be interactive
    print("Available predefined layouts:")
    for _, name in ipairs(getLayoutNames()) do
        print("  " .. name)
    end
    
    print("\nExample usage:")
    print("  applyMemoryLayout('dos_standard')")
    print("  applyMemoryLayout('game_layout', 0x20000)")
end

-- Segment mapping utilities
function segmentMapperHelp()
    print("\n=== Segment Mapper Commands ===")
    print("Predefined Layouts:")
    print("  applyMemoryLayout(layout_name, [base_address])")
    print("  getLayoutNames()")
    print("")
    print("Custom Layouts:")
    print("  createCustomLayout()")
    print("  interactiveMapper()")
    print("")
    print("Analysis:")
    print("  analyzeMemoryLayout()")
    print("  checkMemoryOverlaps(mappings)")
    print("  optimizeMemoryLayout([base_address])")
    print("")
    print("File Operations:")
    print("  saveMemoryLayout(filename)")
    print("  loadMemoryLayout(filename)")
    print("")
    print("Available layouts: " .. table.concat(getLayoutNames(), ", "))
    print("")
    print("Quick start:")
    print("  applyMemoryLayout('dos_standard')")
    print("  analyzeMemoryLayout()")
end

print("Segment Mapper utility loaded!")
print("Type segmentMapperHelp() for available commands")
print("Quick start: applyMemoryLayout('dos_standard')")