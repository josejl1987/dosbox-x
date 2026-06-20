#ifndef MEMORY_REFERENCE_ANALYZER_H
#define MEMORY_REFERENCE_ANALYZER_H

#include <string>
#include <cstdint>

namespace LuaEngineDebug {

// Forward declarations
struct CPUState;
class CoreDebugInterface;

// Structure to hold parsed memory reference information
struct MemoryReference {
    bool valid;                     // True if this is a valid memory reference
    std::string segment;            // Segment prefix ("ds", "ss", "es", "cs", "fs", "gs")
    uint32_t offset;                // Offset within segment
    uint32_t effective_address;     // Computed linear address (segment << 4) + offset
    int operand_size;               // 8, 16, or 32 bits
    uint64_t value;                 // Value read from memory
    std::string display_text;       // Formatted string like "ds:[1234]=42"

    MemoryReference()
        : valid(false)
        , segment("")
        , offset(0)
        , effective_address(0)
        , operand_size(0)
        , value(0)
        , display_text("")
    {}
};

/**
 * Memory Reference Analyzer
 *
 * Parses x86 instruction operands to extract memory references and their values.
 * Ported from the console debugger's AnalyzeInstruction() function.
 */
class MemoryReferenceAnalyzer {
public:
    MemoryReferenceAnalyzer();
    ~MemoryReferenceAnalyzer();

    /**
     * Analyze an instruction's operands to find memory references
     *
     * @param operands The operand string from disassembly (e.g., "[BP+6]", "ds:[SI]")
     * @param mnemonic The instruction mnemonic (e.g., "MOV", "ADD") - used to determine operand size
     * @param cpu_state Current CPU register state for evaluating expressions
     * @param debug_interface Interface for reading memory
     * @return MemoryReference structure with analysis results
     */
    MemoryReference analyzeOperands(
        const std::string& operands,
        const std::string& mnemonic,
        const CPUState& cpu_state,
        CoreDebugInterface* debug_interface
    );

private:
    /**
     * Parse a memory expression like "[BP+6]" or "[SI+DI]"
     * Returns the calculated offset value
     */
    uint32_t parseMemoryExpression(
        const char* expr,
        const CPUState& cpu_state,
        const char** end_ptr
    );

    /**
     * Determine which segment to use based on operands
     * BP/SP use SS, others default to DS unless explicitly specified
     */
    std::string determineSegment(
        const std::string& operands,
        const CPUState& cpu_state
    );

    /**
     * Get segment register value by name
     */
    uint32_t getSegmentValue(
        const std::string& segment_name,
        const CPUState& cpu_state
    );

    /**
     * Determine operand size from instruction mnemonic
     * Returns 8, 16, or 32
     */
    int determineOperandSize(
        const std::string& mnemonic,
        const std::string& operands
    );

    /**
     * Get register value by name (case insensitive)
     */
    uint32_t getRegisterValue(
        const std::string& reg_name,
        const CPUState& cpu_state
    );

    /**
     * Format memory reference for display
     */
    std::string formatMemoryReference(
        const std::string& segment,
        uint32_t offset,
        uint64_t value,
        int operand_size
    );

    /**
     * Check if memory address is valid (not paged out or illegal)
     */
    bool isValidMemoryAddress(
        uint32_t address,
        CoreDebugInterface* debug_interface
    );

    /**
     * Convert string to uppercase in-place
     */
    static void toUpperCase(std::string& str);

    /**
     * Check if string contains substring (case insensitive)
     */
    static bool containsSubstring(const std::string& str, const std::string& substr);
};

} // namespace LuaEngineDebug

#endif // MEMORY_REFERENCE_ANALYZER_H
