-- DOSBox-X Lua Symbol Management Examples
-- This script demonstrates how to use the symbol management API from Lua

print("=== DOSBox-X Symbol Management Examples ===")

-- Example 1: Load a symbol file
function loadSymbols(filename)
    print("Loading symbol file: " .. filename)
    local success = symbols.loadSymbolFile(filename)
    if success then
        print("✓ Symbol file loaded successfully!")
        print("  Format: " .. symbols.getLoadedFormat())
        print("  Symbols: " .. symbols.getSymbolCount())
        print("  File: " .. symbols.getLoadedFileName())
    else
        print("✗ Failed to load symbol file")
    end
    return success
end

-- Example 2: Auto-detect and setup segment mappings
function setupSegmentMappings()
    print("\n=== Setting up segment mappings ===")
    
    -- Auto-detect segments from loaded symbols
    symbols.autoDetectSegments()
    
    -- Get all detected mappings
    local mappings = symbols.getSegmentMappings()
    print("Detected " .. #mappings .. " segments:")
    
    for i, mapping in ipairs(mappings) do
        print(string.format("  %s: File %04X -> Memory %08X (Size: %08X)", 
            mapping.segment_name, 
            mapping.file_segment, 
            mapping.memory_base, 
            mapping.size))
    end
end

-- Example 3: Manual segment mapping for specific memory layout
function setupCustomSegmentMapping()
    print("\n=== Custom segment mapping ===")
    
    -- Clear existing mappings
    symbols.clearSegmentMappings()
    
    -- Add custom mappings for a DOS program loaded at specific addresses
    local text_mapping = {
        segment_name = "_TEXT",
        file_segment = 0x1000,
        memory_base = 0xA0000,  -- Program loaded at A000:0000
        size = 0x10000,
        enabled = true
    }
    
    local data_mapping = {
        segment_name = "_DATA", 
        file_segment = 0x2000,
        memory_base = 0xB0000,  -- Data at B000:0000
        size = 0x10000,
        enabled = true
    }
    
    symbols.addSegmentMapping(text_mapping)
    symbols.addSegmentMapping(data_mapping)
    
    print("Added custom segment mappings:")
    print("  _TEXT: 1000 -> A0000")
    print("  _DATA: 2000 -> B0000")
    
    -- Remap all symbols with new mappings
    symbols.remapAllSymbols()
    print("✓ All symbols remapped to new addresses")
end

-- Example 4: Symbol lookup and analysis
function analyzeSymbols()
    print("\n=== Symbol Analysis ===")
    
    if not symbols.hasSymbols() then
        print("No symbols loaded")
        return
    end
    
    -- Get all symbols
    local all_symbols = symbols.getAllSymbols()
    print("Total symbols: " .. #all_symbols)
    
    -- Categorize symbols by type
    local types = {}
    for _, symbol in ipairs(all_symbols) do
        local type_name = symbol.type
        types[type_name] = (types[type_name] or 0) + 1
    end
    
    print("Symbol types:")
    for type_name, count in pairs(types) do
        print("  " .. type_name .. ": " .. count)
    end
    
    -- Find functions (procedures)
    print("\nFunctions found:")
    for _, symbol in ipairs(all_symbols) do
        if symbol.type == "function" then
            print(string.format("  %s at %08X (%04X:%04X)", 
                symbol.name, symbol.address, symbol.segment, symbol.offset))
        end
    end
end

-- Example 5: Address conversion utilities
function demonstrateAddressConversion()
    print("\n=== Address Conversion ===")
    
    -- Convert segment:offset to linear
    local linear = symbols.segmentOffsetToLinear(0x1000, 0x0100)
    print(string.format("1000:0100 -> %08X", linear))
    
    -- Convert linear back to segment:offset
    local seg, off = symbols.linearToSegmentOffset(linear)
    print(string.format("%08X -> %04X:%04X", linear, seg, off))
    
    -- Test with mapped addresses
    local mapped = symbols.mapAddressToMemory(0x1000, 0x0100, "_TEXT")
    print(string.format("1000:0100 mapped -> %08X", mapped))
end

-- Example 6: Symbol search and filtering
function searchSymbols(pattern)
    print("\n=== Symbol Search: '" .. pattern .. "' ===")
    
    local all_symbols = symbols.getAllSymbols()
    local matches = {}
    
    for _, symbol in ipairs(all_symbols) do
        if string.find(string.lower(symbol.name), string.lower(pattern)) then
            table.insert(matches, symbol)
        end
    end
    
    print("Found " .. #matches .. " matching symbols:")
    for _, symbol in ipairs(matches) do
        print(string.format("  %s (%s) at %08X", 
            symbol.name, symbol.type, symbol.address))
    end
    
    return matches
end

-- Example 7: Dynamic symbol management
function addCustomSymbol(name, address, symbol_type)
    print("\n=== Adding custom symbol ===")
    
    local custom_symbol = {
        name = name,
        address = address,
        type = symbol_type or "label",
        segment = math.floor(address / 16),
        offset = address % 65536,
        file_address = address,
        segment_name = "CUSTOM",
        module = "lua_script",
        size = 0
    }
    
    symbols.addSymbol(custom_symbol)
    print(string.format("✓ Added symbol '%s' at %08X", name, address))
end

-- Example 8: Memory range analysis
function analyzeMemoryRange(start_addr, end_addr)
    print(string.format("\n=== Memory Range Analysis: %08X - %08X ===", start_addr, end_addr))
    
    local range_symbols = symbols.getSymbolsInRange(start_addr, end_addr)
    print("Symbols in range: " .. #range_symbols)
    
    for _, symbol in ipairs(range_symbols) do
        print(string.format("  %08X: %s (%s)", 
            symbol.address, symbol.name, symbol.type))
    end
end

-- Example 9: Segment mapping management
function manageSegmentMappings()
    print("\n=== Segment Mapping Management ===")
    
    -- List current mappings
    local mappings = symbols.getSegmentMappings()
    print("Current mappings:")
    for _, mapping in ipairs(mappings) do
        local status = mapping.enabled and "enabled" or "disabled"
        print(string.format("  %s: %04X -> %08X (%s)", 
            mapping.segment_name, mapping.file_segment, mapping.memory_base, status))
    end
    
    -- Update a mapping
    if #mappings > 0 then
        local first_mapping = mappings[1]
        local new_base = first_mapping.memory_base + 0x1000
        symbols.updateSegmentMapping(first_mapping.segment_name, new_base)
        print(string.format("Updated %s mapping to %08X", first_mapping.segment_name, new_base))
    end
end

-- Example 10: Complete workflow
function completeWorkflow(symbol_file)
    print("\n=== Complete Symbol Management Workflow ===")
    
    -- Step 1: Load symbols
    if not loadSymbols(symbol_file) then
        print("Cannot continue without symbols")
        return
    end
    
    -- Step 2: Setup segment mappings
    setupSegmentMappings()
    
    -- Step 3: Analyze loaded symbols
    analyzeSymbols()
    
    -- Step 4: Demonstrate address conversion
    demonstrateAddressConversion()
    
    -- Step 5: Search for main function
    searchSymbols("main")
    
    -- Step 6: Add a custom symbol
    addCustomSymbol("debug_marker", 0x12345, "marker")
    
    -- Step 7: Analyze a memory range
    analyzeMemoryRange(0x10000, 0x11000)
    
    print("\n✓ Workflow completed successfully!")
end

-- Helper function to get symbol at current execution address
function getCurrentSymbol()
    -- This would need to be integrated with the debugger's current address
    local current_addr = 0x10000  -- Example address
    local symbol_name = symbols.getSymbolName(current_addr, true)
    
    if symbol_name ~= "" then
        print("Current location: " .. symbol_name)
        local symbol = symbols.findSymbol(current_addr)
        if symbol then
            return symbol
        end
    else
        print(string.format("No symbol at current address %08X", current_addr))
    end
    return nil
end

-- Main execution
print("Symbol management API loaded!")
print("Available functions:")
print("  loadSymbols(filename)")
print("  setupSegmentMappings()")
print("  setupCustomSegmentMapping()")
print("  analyzeSymbols()")
print("  searchSymbols(pattern)")
print("  addCustomSymbol(name, address, type)")
print("  analyzeMemoryRange(start, end)")
print("  completeWorkflow(symbol_file)")
print("")
print("Example usage:")
print("  completeWorkflow('program.lst')")
print("  searchSymbols('main')")
print("  setupCustomSegmentMapping()")