#ifndef MEMORY_PROTECTION_H
#define MEMORY_PROTECTION_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>

namespace LuaEngineMemoryProtection {

// Ultra-robust memory validation functions
bool isStringObjectSafe(const std::string& str);
std::string copyStringDefensively(const std::string& str);
std::string convertFromUTF8(const std::string& utf8_text, bool* conversion_succeeded = nullptr);

// Memory protection initialization
bool InitializeMemoryDomains();
void ShutdownMemoryDomains();

} // namespace LuaEngineMemoryProtection

#endif // MEMORY_PROTECTION_H