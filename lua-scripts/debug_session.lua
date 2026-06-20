-- DOSBox-X Advanced Debugging Session Script
-- This script provides high-level debugging utilities using the symbol system

-- Global state for debugging session
local debug_session = {
    breakpoints = {},
    watch_symbols = {},
    call_trace = {},
    current_function = nil
}

-- Load symbols and setup debugging environment
function startDebugSession(symbol_file, program_base_address)
    print("=== Starting Debug Session ===")
    
    -- Load symbol file
    if not symbols.loadSymbolFile(symbol_file) then
        print("ERROR: Could not load symbol file: " .. symbol_file)
        return false
    end
    
    print("✓ Loaded symbols from: " .. symbol_file)
    print("  Format: " .. symbols.getLoadedFormat())
    print("  Symbols: " .. symbols.getSymbolCount())
    
    -- Setup segment mappings if program base address is provided
    if program_base_address then
        setupProgramMapping(program_base_address)
    else
        symbols.autoDetectSegments()
    end
    
    -- List all functions for reference
    listFunctions()
    
    print("✓ Debug session ready!")
    return true
end

-- Setup segment mapping for a program loaded at specific address
function setupProgramMapping(base_address)
    print("Setting up program mapping at base address: " .. string.format("%08X", base_address))
    
    symbols.clearSegmentMappings()
    
    -- Common DOS program layout
    local mappings = {
        {segment_name = "_TEXT", file_segment = 0x1000, memory_base = base_address, size = 0x10000},
        {segment_name = "_DATA", file_segment = 0x2000, memory_base = base_address + 0x10000, size = 0x10000},
        {segment_name = "_BSS", file_segment = 0x3000, memory_base = base_address + 0x20000, size = 0x10000},
        {segment_name = "_STACK", file_segment = 0x4000, memory_base = base_address + 0x30000, size = 0x10000}
    }
    
    for _, mapping in ipairs(mappings) do
        mapping.enabled = true
        symbols.addSegmentMapping(mapping)
        print(string.format("  %s: %04X -> %08X", mapping.segment_name, mapping.file_segment, mapping.memory_base))
    end
    
    symbols.remapAllSymbols()
    print("✓ Program segments mapped and symbols updated")
end

-- List all functions in the program
function listFunctions()
    print("\n=== Available Functions ===")
    
    local all_symbols = symbols.getAllSymbols()
    local functions = {}
    
    for _, symbol in ipairs(all_symbols) do
        if symbol.type == "function" or symbol.type == "label" then
            table.insert(functions, symbol)
        end
    end
    
    -- Sort by address
    table.sort(functions, function(a, b) return a.address < b.address end)
    
    for _, func in ipairs(functions) do
        print(string.format("  %08X: %s (%s)", func.address, func.name, func.type))
    end
    
    print("Total functions/labels: " .. #functions)
end

-- Set breakpoint at symbol
function breakAtSymbol(symbol_name)
    local address = symbols.getSymbolAddress(symbol_name)
    if address == 0 then
        print("ERROR: Symbol '" .. symbol_name .. "' not found")
        return false
    end
    
    -- This would integrate with the actual debugger breakpoint system
    debug_session.breakpoints[symbol_name] = address
    print(string.format("✓ Breakpoint set at %s (%08X)", symbol_name, address))
    
    -- In a real implementation, this would call the debugger's addBreakpoint function
    -- debugger.addBreakpoint(address)
    
    return true
end

-- Watch a symbol for changes
function watchSymbol(symbol_name)
    local symbol = symbols.findSymbol(symbols.getSymbolAddress(symbol_name))
    if not symbol then
        print("ERROR: Symbol '" .. symbol_name .. "' not found")
        return false
    end
    
    debug_session.watch_symbols[symbol_name] = {
        address = symbol.address,
        type = symbol.type,
        last_value = nil
    }
    
    print(string.format("✓ Watching symbol %s at %08X", symbol_name, symbol.address))
    return true
end

-- Analyze call stack with symbols
function analyzeCallStack(call_stack_addresses)
    print("\n=== Call Stack Analysis ===")
    
    if not call_stack_addresses or #call_stack_addresses == 0 then
        print("No call stack provided")
        return
    end
    
    for i, address in ipairs(call_stack_addresses) do
        local symbol_name = symbols.getSymbolName(address, true)
        if symbol_name ~= "" then
            print(string.format("Frame %d: %08X - %s", i-1, address, symbol_name))
        else
            print(string.format("Frame %d: %08X - (no symbol)", i-1, address))
        end
    end
end

-- Find symbols near an address (useful for crash analysis)
function findNearbySymbols(address, range)
    range = range or 0x1000  -- Default 4KB range
    
    print(string.format("\n=== Symbols near %08X (±%X) ===", address, range))
    
    local start_addr = address - range
    local end_addr = address + range
    
    local nearby = symbols.getSymbolsInRange(start_addr, end_addr)
    
    for _, symbol in ipairs(nearby) do
        local distance = math.abs(symbol.address - address)
        local direction = symbol.address > address and "+" or "-"
        print(string.format("  %08X (%s%X): %s (%s)", 
            symbol.address, direction, distance, symbol.name, symbol.type))
    end
    
    if #nearby == 0 then
        print("  No symbols found in range")
    end
end

-- Disassemble function by name
function disassembleFunction(function_name, instruction_count)
    instruction_count = instruction_count or 20
    
    local address = symbols.getSymbolAddress(function_name)
    if address == 0 then
        print("ERROR: Function '" .. function_name .. "' not found")
        return
    end
    
    print(string.format("\n=== Disassembly of %s at %08X ===", function_name, address))
    
    -- This would integrate with the actual disassembler
    print(string.format("Starting at %08X for %d instructions", address, instruction_count))
    print("(Integration with disassembler needed)")
    
    -- In a real implementation:
    -- local disasm = debugger.disassemble(address, instruction_count)
    -- for _, line in ipairs(disasm) do
    --     print(line)
    -- end
end

-- Memory dump with symbol annotations
function memoryDumpWithSymbols(start_address, size)
    size = size or 0x100  -- Default 256 bytes
    
    print(string.format("\n=== Memory Dump %08X - %08X ===", start_address, start_address + size))
    
    -- Find symbols in this range
    local range_symbols = symbols.getSymbolsInRange(start_address, start_address + size)
    
    -- Create symbol map for quick lookup
    local symbol_map = {}
    for _, symbol in ipairs(range_symbols) do
        symbol_map[symbol.address] = symbol
    end
    
    -- This would integrate with actual memory reading
    print("Memory dump with symbol annotations:")
    for offset = 0, size-1, 16 do
        local addr = start_address + offset
        local symbol = symbol_map[addr]
        
        if symbol then
            print(string.format("%08X: <%s> (%s)", addr, symbol.name, symbol.type))
        end
        
        -- In real implementation, show actual memory bytes
        print(string.format("%08X: (memory bytes would be shown here)", addr))
    end
end

-- Search for string patterns in symbol names
function searchSymbolPatterns(patterns)
    print("\n=== Symbol Pattern Search ===")
    
    if type(patterns) == "string" then
        patterns = {patterns}
    end
    
    local all_symbols = symbols.getAllSymbols()
    local matches = {}
    
    for _, pattern in ipairs(patterns) do
        print("Searching for pattern: " .. pattern)
        local pattern_matches = {}
        
        for _, symbol in ipairs(all_symbols) do
            if string.find(string.lower(symbol.name), string.lower(pattern)) then
                table.insert(pattern_matches, symbol)
            end
        end
        
        print(string.format("  Found %d matches:", #pattern_matches))
        for _, symbol in ipairs(pattern_matches) do
            print(string.format("    %08X: %s (%s)", symbol.address, symbol.name, symbol.type))
        end
        
        matches[pattern] = pattern_matches
    end
    
    return matches
end

-- Generate debugging report
function generateDebugReport()
    print("\n=== Debug Session Report ===")
    
    if not symbols.hasSymbols() then
        print("No symbols loaded")
        return
    end
    
    print("Symbol File: " .. symbols.getLoadedFileName())
    print("Format: " .. symbols.getLoadedFormat())
    print("Total Symbols: " .. symbols.getSymbolCount())
    
    -- Segment mappings
    local mappings = symbols.getSegmentMappings()
    print("\nSegment Mappings:")
    for _, mapping in ipairs(mappings) do
        local status = mapping.enabled and "enabled" or "disabled"
        print(string.format("  %s: %04X -> %08X (%s)", 
            mapping.segment_name, mapping.file_segment, mapping.memory_base, status))
    end
    
    -- Breakpoints
    print("\nBreakpoints:")
    for name, address in pairs(debug_session.breakpoints) do
        print(string.format("  %s at %08X", name, address))
    end
    
    -- Watched symbols
    print("\nWatched Symbols:")
    for name, info in pairs(debug_session.watch_symbols) do
        print(string.format("  %s at %08X (%s)", name, info.address, info.type))
    end
    
    -- Symbol statistics
    local all_symbols = symbols.getAllSymbols()
    local stats = {}
    for _, symbol in ipairs(all_symbols) do
        stats[symbol.type] = (stats[symbol.type] or 0) + 1
    end
    
    print("\nSymbol Statistics:")
    for type_name, count in pairs(stats) do
        print(string.format("  %s: %d", type_name, count))
    end
end

-- Quick setup for common debugging scenarios
function quickSetup(scenario, symbol_file, base_address)
    if scenario == "dos_program" then
        startDebugSession(symbol_file, base_address or 0x10000)
        breakAtSymbol("main")
        breakAtSymbol("start")
        
    elseif scenario == "tsr_debug" then
        startDebugSession(symbol_file, base_address or 0xA0000)
        searchSymbolPatterns({"interrupt", "handler", "isr"})
        
    elseif scenario == "game_debug" then
        startDebugSession(symbol_file, base_address or 0x10000)
        searchSymbolPatterns({"update", "render", "input", "main"})
        
    else
        print("Unknown scenario. Available: dos_program, tsr_debug, game_debug")
    end
end

-- Main help function
function debugHelp()
    print("\n=== Debug Session Commands ===")
    print("Setup:")
    print("  startDebugSession(symbol_file, [base_address])")
    print("  quickSetup(scenario, symbol_file, [base_address])")
    print("")
    print("Breakpoints & Watching:")
    print("  breakAtSymbol(symbol_name)")
    print("  watchSymbol(symbol_name)")
    print("")
    print("Analysis:")
    print("  listFunctions()")
    print("  analyzeCallStack(addresses)")
    print("  findNearbySymbols(address, [range])")
    print("  disassembleFunction(name, [count])")
    print("  memoryDumpWithSymbols(address, [size])")
    print("")
    print("Search:")
    print("  searchSymbolPatterns(patterns)")
    print("")
    print("Utilities:")
    print("  generateDebugReport()")
    print("  setupProgramMapping(base_address)")
    print("")
    print("Quick scenarios:")
    print("  quickSetup('dos_program', 'program.lst')")
    print("  quickSetup('tsr_debug', 'tsr.map', 0xA0000)")
    print("  quickSetup('game_debug', 'game.lst')")
end

print("Advanced debugging session loaded!")
print("Type debugHelp() for available commands")
print("Quick start: quickSetup('dos_program', 'your_program.lst')")