#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <string>
#include <vector>
#include <cstdint>
#include <cctype>
#include <algorithm>

namespace LuaEngineDebugUtils {

/**
 * Shared utility functions for debugging tools
 * Consolidates duplicated parsing and formatting code from:
 * - cheat_window
 * - hex_editor_window
 * - watch_window
 * - ram_search_window
 */

// Fast lookup table for hex conversion
static const char hex_chars[] = "0123456789ABCDEF";
static const char hex_chars_lower[] = "0123456789abcdef";

// ============================================================================
// Address Parsing
// ============================================================================

/**
 * Parse address from string (supports hex with 0x prefix or plain hex)
 * @param input Address string (e.g., "0x1000", "1000", "A000")
 * @return Parsed address or 0 on error
 */
inline uint32_t parseAddress(const std::string& input) {
    if (input.empty()) return 0;

    try {
        // Always parse as hex (with or without 0x prefix)
        if (input.length() >= 2 && input.substr(0, 2) == "0x") {
            return std::stoul(input, nullptr, 16);
        } else {
            return std::stoul(input, nullptr, 16);  // Default to hex
        }
    } catch (const std::exception&) {
        return 0;
    }
}

// ============================================================================
// Value Parsing
// ============================================================================

/**
 * Parse value from string (supports hex with 0x prefix, or decimal)
 * @param input Value string
 * @return Parsed 64-bit value or 0 on error
 */
inline uint64_t parseValue(const std::string& input) {
    if (input.empty()) return 0;

    try {
        if (input.length() >= 2 && input.substr(0, 2) == "0x") {
            return std::stoull(input, nullptr, 16);
        } else {
            return std::stoull(input);  // Decimal
        }
    } catch (const std::exception&) {
        return 0;
    }
}

/**
 * Parse signed value from string
 * @param input Value string
 * @return Parsed signed 64-bit value or 0 on error
 */
inline int64_t parseSignedValue(const std::string& input) {
    if (input.empty()) return 0;

    try {
        if (input.length() >= 2 && input.substr(0, 2) == "0x") {
            return static_cast<int64_t>(std::stoull(input, nullptr, 16));
        } else {
            return std::stoll(input);
        }
    } catch (const std::exception&) {
        return 0;
    }
}

// ============================================================================
// Hex String Parsing
// ============================================================================

/**
 * Parse hex string into byte array (e.g., "48 65 6C 6C 6F" -> [0x48, 0x65, 0x6C, 0x6C, 0x6F])
 * @param input Hex string with optional spaces
 * @return Vector of parsed bytes
 */
inline std::vector<uint8_t> parseHexString(const std::string& input) {
    std::vector<uint8_t> result;
    std::string clean_input;

    // Remove spaces and non-hex characters, keep only hex digits
    for (char c : input) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            clean_input += std::toupper(static_cast<unsigned char>(c));
        }
    }

    // Parse hex pairs
    for (size_t i = 0; i < clean_input.length(); i += 2) {
        if (i + 1 < clean_input.length()) {
            std::string hex_pair = clean_input.substr(i, 2);
            try {
                uint8_t byte = static_cast<uint8_t>(std::stoul(hex_pair, nullptr, 16));
                result.push_back(byte);
            } catch (const std::exception&) {
                // Skip invalid bytes
            }
        }
    }

    return result;
}

/**
 * Parse pointer path from string (e.g., "[[0x1000]+4]+8" or "0x1000,4,8")
 * @param input Pointer path string
 * @return Vector of address offsets
 */
inline std::vector<uint32_t> parsePointerPath(const std::string& input) {
    std::vector<uint32_t> result;
    std::string clean_input = input;

    // Remove brackets and spaces
    clean_input.erase(std::remove_if(clean_input.begin(), clean_input.end(),
        [](char c) { return c == '[' || c == ']' || c == ' '; }), clean_input.end());

    // Split by '+' or ','
    size_t start = 0;
    for (size_t i = 0; i <= clean_input.length(); ++i) {
        if (i == clean_input.length() || clean_input[i] == '+' || clean_input[i] == ',') {
            if (i > start) {
                std::string offset_str = clean_input.substr(start, i - start);
                result.push_back(parseAddress(offset_str));
            }
            start = i + 1;
        }
    }

    return result;
}

// ============================================================================
// Formatting Functions
// ============================================================================

/**
 * Format byte as hex string
 * @param value Byte value
 * @return Formatted string (e.g., "4A")
 */
inline std::string formatHexByte(uint8_t value) {
    std::string result(2, '0');
    result[0] = hex_chars[(value >> 4) & 0xF];
    result[1] = hex_chars[value & 0xF];
    return result;
}

/**
 * Format word as hex string
 * @param value Word value
 * @return Formatted string (e.g., "1A4B")
 */
inline std::string formatHexWord(uint16_t value) {
    std::string result(4, '0');
    result[0] = hex_chars[(value >> 12) & 0xF];
    result[1] = hex_chars[(value >> 8) & 0xF];
    result[2] = hex_chars[(value >> 4) & 0xF];
    result[3] = hex_chars[value & 0xF];
    return result;
}

/**
 * Format dword as hex string
 * @param value Dword value
 * @return Formatted string (e.g., "001A4B3C")
 */
inline std::string formatHexDWord(uint32_t value) {
    std::string result(8, '0');
    result[0] = hex_chars[(value >> 28) & 0xF];
    result[1] = hex_chars[(value >> 24) & 0xF];
    result[2] = hex_chars[(value >> 20) & 0xF];
    result[3] = hex_chars[(value >> 16) & 0xF];
    result[4] = hex_chars[(value >> 12) & 0xF];
    result[5] = hex_chars[(value >> 8) & 0xF];
    result[6] = hex_chars[(value >> 4) & 0xF];
    result[7] = hex_chars[value & 0xF];
    return result;
}

/**
 * Format address as hex string with 0x prefix
 * @param address Address value
 * @return Formatted string (e.g., "0x001A4B3C")
 */
inline std::string formatAddress(uint32_t address) {
    std::string result(10, '0');
    result[0] = '0'; result[1] = 'x';
    result[2] = hex_chars[(address >> 28) & 0xF];
    result[3] = hex_chars[(address >> 24) & 0xF];
    result[4] = hex_chars[(address >> 20) & 0xF];
    result[5] = hex_chars[(address >> 16) & 0xF];
    result[6] = hex_chars[(address >> 12) & 0xF];
    result[7] = hex_chars[(address >> 8) & 0xF];
    result[8] = hex_chars[(address >> 4) & 0xF];
    result[9] = hex_chars[address & 0xF];
    return result;
}

/**
 * Format address as segment:offset
 * @param address Linear address
 * @return Formatted string (e.g., "1000:4B3C")
 */
inline std::string formatSegmentOffset(uint32_t address) {
    uint16_t segment = (address >> 4) & 0xFFFF;
    uint16_t offset = address & 0xFFFF;
    return formatHexWord(segment) + ":" + formatHexWord(offset);
}

/**
 * Format byte array as hex string
 * @param data Byte array
 * @param length Array length
 * @param spacing Include spaces between bytes
 * @return Formatted hex string
 */
inline std::string formatHexBytes(const uint8_t* data, size_t length, bool spacing = true) {
    if (length == 0) return "";
    std::string result((length * (spacing ? 3 : 2)) - (spacing ? 1 : 0), ' ');
    size_t idx = 0;
    for (size_t i = 0; i < length; ++i) {
        result[idx++] = hex_chars[(data[i] >> 4) & 0xF];
        result[idx++] = hex_chars[data[i] & 0xF];
        if (spacing && i < length - 1) {
            idx++; // Skip space
        }
    }
    return result;
}

/**
 * Format vector as hex string
 * @param data Byte vector
 * @param spacing Include spaces between bytes
 * @return Formatted hex string
 */
inline std::string formatHexBytes(const std::vector<uint8_t>& data, bool spacing = true) {
    return formatHexBytes(data.data(), data.size(), spacing);
}

// ============================================================================
// Validation Functions
// ============================================================================

/**
 * Check if string is valid hex
 * @param input String to check
 * @return True if valid hex string
 */
inline bool isValidHex(const std::string& input) {
    if (input.empty()) return false;

    std::string check_str = input;
    if (check_str.length() >= 2 && check_str.substr(0, 2) == "0x") {
        check_str = check_str.substr(2);
    }

    return !check_str.empty() &&
           std::all_of(check_str.begin(), check_str.end(),
               [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

/**
 * Check if string is valid decimal number
 * @param input String to check
 * @return True if valid decimal
 */
inline bool isValidDecimal(const std::string& input) {
    if (input.empty()) return false;

    size_t start = 0;
    if (input[0] == '-' || input[0] == '+') {
        start = 1;
        if (input.length() == 1) return false;
    }

    return std::all_of(input.begin() + start, input.end(),
        [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

/**
 * Check if address is aligned
 * @param address Address to check
 * @param alignment Required alignment (e.g., 2, 4, 8)
 * @return True if properly aligned
 */
inline bool isAligned(uint32_t address, uint32_t alignment) {
    return (address % alignment) == 0;
}

} // namespace LuaEngineDebugUtils

#endif // DEBUG_UTILS_H
