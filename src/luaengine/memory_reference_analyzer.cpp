#include "memory_reference_analyzer.h"
#include "core_debug_interface.h"
#include <cctype>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace LuaEngineDebug {

MemoryReferenceAnalyzer::MemoryReferenceAnalyzer() {
}

MemoryReferenceAnalyzer::~MemoryReferenceAnalyzer() {
}

void MemoryReferenceAnalyzer::toUpperCase(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return std::toupper(c); });
}

bool MemoryReferenceAnalyzer::containsSubstring(const std::string& str, const std::string& substr) {
    std::string str_upper = str;
    std::string substr_upper = substr;
    toUpperCase(str_upper);
    toUpperCase(substr_upper);
    return str_upper.find(substr_upper) != std::string::npos;
}

uint32_t MemoryReferenceAnalyzer::getRegisterValue(
    const std::string& reg_name,
    const CPUState& cpu_state
) {
    std::string reg_upper = reg_name;
    toUpperCase(reg_upper);

    // 32-bit registers
    if (reg_upper == "EAX") return cpu_state.eax;
    if (reg_upper == "EBX") return cpu_state.ebx;
    if (reg_upper == "ECX") return cpu_state.ecx;
    if (reg_upper == "EDX") return cpu_state.edx;
    if (reg_upper == "ESI") return cpu_state.esi;
    if (reg_upper == "EDI") return cpu_state.edi;
    if (reg_upper == "ESP") return cpu_state.esp;
    if (reg_upper == "EBP") return cpu_state.ebp;
    if (reg_upper == "EIP") return cpu_state.ip;

    // 16-bit registers
    if (reg_upper == "AX") return cpu_state.eax & 0xFFFF;
    if (reg_upper == "BX") return cpu_state.ebx & 0xFFFF;
    if (reg_upper == "CX") return cpu_state.ecx & 0xFFFF;
    if (reg_upper == "DX") return cpu_state.edx & 0xFFFF;
    if (reg_upper == "SI") return cpu_state.esi & 0xFFFF;
    if (reg_upper == "DI") return cpu_state.edi & 0xFFFF;
    if (reg_upper == "SP") return cpu_state.esp & 0xFFFF;
    if (reg_upper == "BP") return cpu_state.ebp & 0xFFFF;
    if (reg_upper == "IP") return cpu_state.ip & 0xFFFF;

    // 8-bit registers
    if (reg_upper == "AL") return cpu_state.eax & 0xFF;
    if (reg_upper == "BL") return cpu_state.ebx & 0xFF;
    if (reg_upper == "CL") return cpu_state.ecx & 0xFF;
    if (reg_upper == "DL") return cpu_state.edx & 0xFF;
    if (reg_upper == "AH") return (cpu_state.eax >> 8) & 0xFF;
    if (reg_upper == "BH") return (cpu_state.ebx >> 8) & 0xFF;
    if (reg_upper == "CH") return (cpu_state.ecx >> 8) & 0xFF;
    if (reg_upper == "DH") return (cpu_state.edx >> 8) & 0xFF;

    // Segment registers
    if (reg_upper == "CS") return cpu_state.cs;
    if (reg_upper == "DS") return cpu_state.ds;
    if (reg_upper == "ES") return cpu_state.es;
    if (reg_upper == "FS") return cpu_state.fs;
    if (reg_upper == "GS") return cpu_state.gs;
    if (reg_upper == "SS") return cpu_state.ss;

    return 0;
}

uint32_t MemoryReferenceAnalyzer::getSegmentValue(
    const std::string& segment_name,
    const CPUState& cpu_state
) {
    std::string seg_upper = segment_name;
    toUpperCase(seg_upper);

    if (seg_upper == "CS") return cpu_state.cs;
    if (seg_upper == "DS") return cpu_state.ds;
    if (seg_upper == "ES") return cpu_state.es;
    if (seg_upper == "FS") return cpu_state.fs;
    if (seg_upper == "GS") return cpu_state.gs;
    if (seg_upper == "SS") return cpu_state.ss;

    return cpu_state.ds; // Default
}

std::string MemoryReferenceAnalyzer::determineSegment(
    const std::string& operands,
    const CPUState& cpu_state
) {
    // Check for explicit segment prefix
    if (operands.find("cs:") != std::string::npos ||
        operands.find("CS:") != std::string::npos) {
        return "cs";
    }
    if (operands.find("ds:") != std::string::npos ||
        operands.find("DS:") != std::string::npos) {
        return "ds";
    }
    if (operands.find("es:") != std::string::npos ||
        operands.find("ES:") != std::string::npos) {
        return "es";
    }
    if (operands.find("fs:") != std::string::npos ||
        operands.find("FS:") != std::string::npos) {
        return "fs";
    }
    if (operands.find("gs:") != std::string::npos ||
        operands.find("GS:") != std::string::npos) {
        return "gs";
    }
    if (operands.find("ss:") != std::string::npos ||
        operands.find("SS:") != std::string::npos) {
        return "ss";
    }

    // Check for BP or SP - these default to SS segment
    if (containsSubstring(operands, "BP") || containsSubstring(operands, "SP")) {
        return "ss";
    }

    // Default to DS
    return "ds";
}

uint32_t MemoryReferenceAnalyzer::parseMemoryExpression(
    const char* expr,
    const CPUState& cpu_state,
    const char** end_ptr
) {
    uint32_t result = 0;
    const char* ptr = expr;
    bool first_term = true;
    char operation = '+';

    while (*ptr && *ptr != ']') {
        // Skip whitespace
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        if (*ptr == ']') break;

        // Handle operators
        if (*ptr == '+' || *ptr == '-') {
            operation = *ptr;
            ptr++;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            first_term = false;
            continue;
        }

        // Try to parse a register name or hex value
        const char* term_start = ptr;
        std::string term;

        // Collect alphanumeric characters
        while (isalnum(*ptr) || *ptr == '_') {
            term += *ptr;
            ptr++;
        }

        if (term.empty()) {
            ptr++;
            continue;
        }

        toUpperCase(term);

        uint32_t value = 0;

        // Check if it's a register
        if (term == "EAX" || term == "EBX" || term == "ECX" || term == "EDX" ||
            term == "ESI" || term == "EDI" || term == "ESP" || term == "EBP" ||
            term == "AX" || term == "BX" || term == "CX" || term == "DX" ||
            term == "SI" || term == "DI" || term == "SP" || term == "BP" ||
            term == "AL" || term == "BL" || term == "CL" || term == "DL" ||
            term == "AH" || term == "BH" || term == "CH" || term == "DH") {
            value = getRegisterValue(term, cpu_state);
        } else {
            // Try to parse as hex number
            value = (uint32_t)strtoul(term.c_str(), nullptr, 16);
        }

        // Apply operation
        if (first_term || operation == '+') {
            result += value;
        } else if (operation == '-') {
            result -= value;
        }

        first_term = false;
        operation = '+'; // Reset to default
    }

    if (end_ptr) {
        *end_ptr = ptr;
    }

    return result;
}

int MemoryReferenceAnalyzer::determineOperandSize(
    const std::string& mnemonic,
    const std::string& operands
) {
    std::string mn_upper = mnemonic;
    toUpperCase(mn_upper);

    // Check for explicit size specifiers in operands
    if (containsSubstring(operands, "BYTE PTR") ||
        containsSubstring(operands, "byte ptr")) {
        return 8;
    }
    if (containsSubstring(operands, "WORD PTR") ||
        containsSubstring(operands, "word ptr")) {
        return 16;
    }
    if (containsSubstring(operands, "DWORD PTR") ||
        containsSubstring(operands, "dword ptr")) {
        return 32;
    }

    // Infer from instruction mnemonic
    // Byte operations
    if (mn_upper.find("MOVB") != std::string::npos ||
        mn_upper.find("SETB") != std::string::npos ||
        mn_upper.find("CMPB") != std::string::npos) {
        return 8;
    }

    // Word operations (16-bit)
    if (mn_upper.find("MOVW") != std::string::npos ||
        mn_upper.find("CMPW") != std::string::npos) {
        return 16;
    }

    // Dword operations (32-bit)
    if (mn_upper.find("MOVD") != std::string::npos ||
        mn_upper.find("CMPD") != std::string::npos) {
        return 32;
    }

    // Check for E-register usage in operands (implies 32-bit)
    if (containsSubstring(operands, "EAX") ||
        containsSubstring(operands, "EBX") ||
        containsSubstring(operands, "ECX") ||
        containsSubstring(operands, "EDX") ||
        containsSubstring(operands, "ESI") ||
        containsSubstring(operands, "EDI") ||
        containsSubstring(operands, "ESP") ||
        containsSubstring(operands, "EBP")) {
        return 32;
    }

    // Check for H/L registers (implies 8-bit)
    if (containsSubstring(operands, "AL") ||
        containsSubstring(operands, "BL") ||
        containsSubstring(operands, "CL") ||
        containsSubstring(operands, "DL") ||
        containsSubstring(operands, "AH") ||
        containsSubstring(operands, "BH") ||
        containsSubstring(operands, "CH") ||
        containsSubstring(operands, "DH")) {
        return 8;
    }

    // Default to 16-bit for most x86 real mode operations
    return 16;
}

std::string MemoryReferenceAnalyzer::formatMemoryReference(
    const std::string& segment,
    uint32_t offset,
    uint64_t value,
    int operand_size
) {
    char buffer[128];

    switch (operand_size) {
    case 8:
        snprintf(buffer, sizeof(buffer), "%s:[%04X]=%02X",
            segment.c_str(), offset, (uint8_t)value);
        break;
    case 16:
        snprintf(buffer, sizeof(buffer), "%s:[%04X]=%04X",
            segment.c_str(), offset, (uint16_t)value);
        break;
    case 32:
        snprintf(buffer, sizeof(buffer), "%s:[%08X]=%08X",
            segment.c_str(), offset, (uint32_t)value);
        break;
    default:
        snprintf(buffer, sizeof(buffer), "%s:[%04X]=??",
            segment.c_str(), offset);
        break;
    }

    return std::string(buffer);
}

bool MemoryReferenceAnalyzer::isValidMemoryAddress(
    uint32_t address,
    CoreDebugInterface* debug_interface
) {
    // Try to read a byte - if it fails, address is invalid
    // Note: In DOSBox-X, the console debugger checks TLB flags
    // We'll use a simpler approach: just try to read
    try {
        debug_interface->readByte(address);
        return true;
    } catch (...) {
        return false;
    }
}

MemoryReference MemoryReferenceAnalyzer::analyzeOperands(
    const std::string& operands,
    const std::string& mnemonic,
    const CPUState& cpu_state,
    CoreDebugInterface* debug_interface
) {
    MemoryReference result;

    // Look for memory reference brackets
    size_t bracket_open = operands.find('[');
    if (bracket_open == std::string::npos) {
        // No memory reference
        result.valid = false;
        return result;
    }

    size_t bracket_close = operands.find(']', bracket_open);
    if (bracket_close == std::string::npos) {
        // Malformed
        result.valid = false;
        return result;
    }

    // Determine segment
    result.segment = determineSegment(operands, cpu_state);
    uint32_t seg_value = getSegmentValue(result.segment, cpu_state);

    // Extract expression inside brackets
    std::string expr = operands.substr(bracket_open + 1, bracket_close - bracket_open - 1);

    // Parse the expression to get offset
    const char* end_ptr = nullptr;
    result.offset = parseMemoryExpression(expr.c_str(), cpu_state, &end_ptr);

    // Calculate effective linear address
    result.effective_address = (seg_value << 4) + result.offset;

    // Determine operand size
    result.operand_size = determineOperandSize(mnemonic, operands);

    // Check if address is valid
    if (!isValidMemoryAddress(result.effective_address, debug_interface)) {
        result.valid = true; // Still a valid reference, just illegal memory
        result.display_text = "[illegal]";
        return result;
    }

    // Read memory value based on size
    try {
        switch (result.operand_size) {
        case 8:
            result.value = debug_interface->readByte(result.effective_address);
            break;
        case 16:
            result.value = debug_interface->readWord(result.effective_address);
            break;
        case 32:
            result.value = debug_interface->readDWord(result.effective_address);
            break;
        default:
            result.value = 0;
            break;
        }
    } catch (...) {
        result.valid = true;
        result.display_text = "[illegal]";
        return result;
    }

    // Format display text
    result.display_text = formatMemoryReference(
        result.segment,
        result.offset,
        result.value,
        result.operand_size
    );

    result.valid = true;
    return result;
}

} // namespace LuaEngineDebug
