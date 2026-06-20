#include "autocomplete.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace LuaAutocomplete {

AutocompleteEngine::AutocompleteEngine() {
    // Pre-register common Lua built-ins and DOSBox-X APIs
    registerNamespace("memory", "Memory access functions for reading/writing emulated memory");
    registerNamespace("cpu", "CPU register and state access functions");
    registerNamespace("debug", "Debugging utilities and breakpoint management");
    registerNamespace("emu", "Emulator control functions (pause, speed, frame advance)");
    registerNamespace("input", "Input recording and playback functions");
    registerNamespace("gui", "GUI overlay and drawing functions");
    registerNamespace("savestate", "Save state management functions");
    registerNamespace("event", "Event system for hooks and callbacks");
    
    // Common memory API functions (based on examples in console help text)
    registerFunction("readbyte", "memory.readbyte", "readbyte(segment, offset)", 
                    "Read a single byte from memory at segment:offset");
    registerFunction("writebyte", "memory.writebyte", "writebyte(segment, offset, value)",
                    "Write a single byte to memory at segment:offset");
    registerFunction("readword", "memory.readword", "readword(segment, offset)",
                    "Read a 16-bit word from memory at segment:offset");
    registerFunction("writeword", "memory.writeword", "writeword(segment, offset, value)",
                    "Write a 16-bit word to memory at segment:offset");
    registerFunction("readdword", "memory.readdword", "readdword(segment, offset)",
                    "Read a 32-bit double word from memory at segment:offset");
    registerFunction("writedword", "memory.writedword", "writedword(segment, offset, value)",
                    "Write a 32-bit double word to memory at segment:offset");
    
    // Common CPU API functions (based on examples in console help text)
    registerFunction("get_ax", "cpu.get_ax", "get_ax()", "Get the value of the AX register");
    registerFunction("get_bx", "cpu.get_bx", "get_bx()", "Get the value of the BX register");
    registerFunction("get_cx", "cpu.get_cx", "get_cx()", "Get the value of the CX register");
    registerFunction("get_dx", "cpu.get_dx", "get_dx()", "Get the value of the DX register");
    registerFunction("set_ax", "cpu.set_ax", "set_ax(value)", "Set the value of the AX register");
    registerFunction("set_bx", "cpu.set_bx", "set_bx(value)", "Set the value of the BX register");
    registerFunction("set_cx", "cpu.set_cx", "set_cx(value)", "Set the value of the CX register");
    registerFunction("set_dx", "cpu.set_dx", "set_dx(value)", "Set the value of the DX register");
    
    // Emulator control functions
    registerFunction("pause", "emu.pause", "pause()", "Pause emulation");
    registerFunction("unpause", "emu.unpause", "unpause()", "Resume emulation");
    registerFunction("frameadvance", "emu.frameadvance", "frameadvance()", "Advance one frame while paused");
    registerFunction("framecount", "emu.framecount", "framecount()", "Get current frame number");
    registerFunction("setspeed", "emu.setspeed", "setspeed(multiplier)", "Set emulation speed multiplier");
    
    // Debug functions
    registerFunction("setbreakpoint", "debug.setbreakpoint", "setbreakpoint(address)", 
                    "Set breakpoint at memory address");
    registerFunction("removebreakpoint", "debug.removebreakpoint", "removebreakpoint(address)",
                    "Remove breakpoint at memory address");
    registerFunction("print", "debug.print", "print(message)", "Print debug message to console");
    
    buildNamespaceCache();
}

void AutocompleteEngine::registerFunction(const std::string& name, const std::string& namespace_path,
                                        const std::string& signature, const std::string& description) {
    LuaAPIFunction func;
    func.name = name;
    func.namespace_path = namespace_path;
    func.signature = signature.empty() ? name + "()" : signature;
    func.description = description;
    func.is_namespace = false;
    
    // Extract parameters from signature if provided
    if (!signature.empty()) {
        size_t start = signature.find('(');
        size_t end = signature.find(')', start);
        if (start != std::string::npos && end != std::string::npos && end > start + 1) {
            std::string params_str = signature.substr(start + 1, end - start - 1);
            std::stringstream ss(params_str);
            std::string param;
            while (std::getline(ss, param, ',')) {
                // Trim whitespace
                param.erase(0, param.find_first_not_of(" \t"));
                param.erase(param.find_last_not_of(" \t") + 1);
                if (!param.empty()) {
                    func.parameters.push_back(param);
                }
            }
        }
    }
    
    api_registry.push_back(func);
}

void AutocompleteEngine::registerNamespace(const std::string& name, const std::string& description) {
    LuaAPIFunction ns;
    ns.name = name;
    ns.namespace_path = name;
    ns.description = description;
    ns.is_namespace = true;
    
    api_registry.push_back(ns);
}

void AutocompleteEngine::buildNamespaceCache() {
    namespace_cache.clear();
    
    for (auto& func : api_registry) {
        if (func.is_namespace) continue;
        
        // Extract namespace from full path (e.g., "memory.readbyte" -> "memory")
        size_t dot_pos = func.namespace_path.find('.');
        if (dot_pos != std::string::npos) {
            std::string ns = func.namespace_path.substr(0, dot_pos);
            namespace_cache[ns].push_back(&func);
        }
    }
}

void AutocompleteEngine::analyzeInput(const std::string& input, size_t cursor_pos) {
    current_input = input;
    cursor_position = (cursor_pos == std::string::npos) ? input.length() : cursor_pos;
    
    // Enhanced context analysis
    std::string input_to_cursor = input.substr(0, cursor_position);
    
    // Analyze the context around the cursor
    analyzeContext(input_to_cursor);
    
    // Find the current word being typed (up to cursor position)
    size_t word_start = findWordStart(input_to_cursor);
    std::string current_word = input_to_cursor.substr(word_start);
    
    // Parse namespace and partial text
    parseNamespaceAndPartial(current_word);
    
    // Analyze function call context
    analyzeFunctionCallContext(input_to_cursor, word_start);
}

void AutocompleteEngine::analyzeContext(const std::string& input_to_cursor) {
    // Reset context flags
    context_flags.in_function_call = false;
    context_flags.in_string = false;
    context_flags.in_comment = false;
    context_flags.in_assignment = false;
    context_flags.parenthesis_depth = 0;
    context_flags.bracket_depth = 0;
    
    // Track context by scanning through the input
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_line_comment = false;
    
    for (size_t i = 0; i < input_to_cursor.length(); ++i) {
        char c = input_to_cursor[i];
        char next_c = (i + 1 < input_to_cursor.length()) ? input_to_cursor[i + 1] : '\0';
        
        // Handle comments
        if (!in_single_quote && !in_double_quote) {
            if (c == '-' && next_c == '-') {
                in_line_comment = true;
                continue;
            }
            if (c == '\n') {
                in_line_comment = false;
                continue;
            }
        }
        
        if (in_line_comment) continue;
        
        // Handle strings
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        
        context_flags.in_string = in_single_quote || in_double_quote;
        context_flags.in_comment = in_line_comment;
        
        if (!context_flags.in_string && !context_flags.in_comment) {
            // Track parentheses and brackets
            if (c == '(') context_flags.parenthesis_depth++;
            else if (c == ')') context_flags.parenthesis_depth--;
            else if (c == '[') context_flags.bracket_depth++;
            else if (c == ']') context_flags.bracket_depth--;
            
            // Check for assignment
            if (c == '=' && next_c != '=') {
                context_flags.in_assignment = true;
            }
            
            // Check for function call
            if (context_flags.parenthesis_depth > 0) {
                context_flags.in_function_call = true;
            }
        }
    }
}

size_t AutocompleteEngine::findWordStart(const std::string& input_to_cursor) {
    size_t word_start = 0;
    bool found_word_boundary = false;
    
    for (int i = static_cast<int>(input_to_cursor.length()) - 1; i >= 0; --i) {
        char c = input_to_cursor[i];
        
        // Enhanced word boundary detection
        if (std::isspace(c) || c == '(' || c == ')' || c == ',' || c == ';' || 
            c == '=' || c == '+' || c == '-' || c == '*' || c == '/' || 
            c == '[' || c == ']' || c == '{' || c == '}' || c == '<' || c == '>') {
            word_start = i + 1;
            found_word_boundary = true;
            break;
        }
    }
    
    return word_start;
}

void AutocompleteEngine::parseNamespaceAndPartial(const std::string& current_word) {
    // Check if we have a namespace (contains dot)
    size_t dot_pos = current_word.find('.');
    if (dot_pos != std::string::npos) {
        current_namespace = current_word.substr(0, dot_pos);
        current_partial = current_word.substr(dot_pos + 1);
    } else {
        current_namespace = "";
        current_partial = current_word;
    }
}

void AutocompleteEngine::analyzeFunctionCallContext(const std::string& input_to_cursor, size_t word_start) {
    // Reset function call info
    current_function_call.clear();
    current_parameter_index = 0;
    
    if (!context_flags.in_function_call) return;
    
    // Find the function name that we're inside
    size_t paren_pos = input_to_cursor.find_last_of('(');
    if (paren_pos != std::string::npos) {
        // Find the function name before the parenthesis
        size_t func_start = paren_pos;
        while (func_start > 0 && (std::isalnum(input_to_cursor[func_start - 1]) || 
                                  input_to_cursor[func_start - 1] == '_' || 
                                  input_to_cursor[func_start - 1] == '.')) {
            func_start--;
        }
        
        if (func_start < paren_pos) {
            current_function_call = input_to_cursor.substr(func_start, paren_pos - func_start);
            
            // Count commas to determine parameter index
            std::string params_section = input_to_cursor.substr(paren_pos + 1);
            current_parameter_index = std::count(params_section.begin(), params_section.end(), ',');
        }
    }
}

std::vector<AutocompleteSuggestion> AutocompleteEngine::getSuggestions(const std::string& input, 
                                                                      size_t cursor_position) {
    current_suggestions.clear();
    analyzeInput(input, cursor_position);
    
    if (current_namespace.empty()) {
        // Suggest namespaces and global functions
        findGlobalSuggestions(current_partial);
    } else {
        // Suggest functions within the namespace
        findFunctionSuggestions(current_namespace, current_partial);
    }
    
    // Sort suggestions by priority (highest first)
    std::sort(current_suggestions.begin(), current_suggestions.end(),
              [](const AutocompleteSuggestion& a, const AutocompleteSuggestion& b) {
                  if (a.is_exact_match && !b.is_exact_match) return true;
                  if (!a.is_exact_match && b.is_exact_match) return false;
                  return a.priority > b.priority;
              });
    
    // Limit results
    if (current_suggestions.size() > max_suggestions) {
        current_suggestions.resize(max_suggestions);
    }
    
    return current_suggestions;
}

void AutocompleteEngine::findGlobalSuggestions(const std::string& partial) {
    // Suggest namespaces
    for (const auto& func : api_registry) {
        if (func.is_namespace) {
            int priority = calculateMatchPriority(func.name, partial);
            if (priority > 0 || partial.empty()) {
                std::string display_text = func.name + " - " + func.description;
                AutocompleteSuggestion suggestion(func.name, display_text, priority);
                suggestion.description = func.description;
                suggestion.is_exact_match = (func.name == partial);
                current_suggestions.push_back(suggestion);
            }
        }
    }
    
    // Also suggest functions without namespace (global functions)
    for (const auto& func : api_registry) {
        if (!func.is_namespace && func.namespace_path.find('.') == std::string::npos) {
            int priority = calculateMatchPriority(func.name, partial);
            if (priority > 0 || partial.empty()) {
                std::string display_text = func.signature + " - " + func.description;
                AutocompleteSuggestion suggestion(func.name, display_text, priority);
                suggestion.description = func.description;
                suggestion.is_exact_match = (func.name == partial);
                current_suggestions.push_back(suggestion);
            }
        }
    }
}

void AutocompleteEngine::findFunctionSuggestions(const std::string& namespace_name, const std::string& partial) {
    auto ns_it = namespace_cache.find(namespace_name);
    if (ns_it == namespace_cache.end()) return;
    
    for (LuaAPIFunction* func : ns_it->second) {
        int priority = calculateMatchPriority(func->name, partial);
        if (priority > 0 || partial.empty()) {
            std::string display_text = func->signature + " - " + func->description;
            AutocompleteSuggestion suggestion(func->name, display_text, priority);
            suggestion.description = func->description;
            suggestion.is_exact_match = (func->name == partial);
            current_suggestions.push_back(suggestion);
        }
    }
}

int AutocompleteEngine::calculateMatchPriority(const std::string& candidate, const std::string& partial) {
    if (partial.empty()) return 50; // Default priority for empty query
    if (candidate.empty()) return 0;
    
    // Exact match gets highest priority
    if (candidate == partial) return 1000;
    
    // Case-insensitive exact match
    std::string candidate_lower = candidate;
    std::string partial_lower = partial;
    std::transform(candidate_lower.begin(), candidate_lower.end(), candidate_lower.begin(), ::tolower);
    std::transform(partial_lower.begin(), partial_lower.end(), partial_lower.begin(), ::tolower);
    
    if (candidate_lower == partial_lower) return 950;
    
    // Starts with (case sensitive)
    if (candidate.find(partial) == 0) return 900;
    
    // Starts with (case insensitive)
    if (candidate_lower.find(partial_lower) == 0) return 850;
    
    // Contains (case sensitive)
    if (candidate.find(partial) != std::string::npos) return 700;
    
    // Contains (case insensitive)
    if (candidate_lower.find(partial_lower) != std::string::npos) return 650;
    
    // Fuzzy match if enabled
    if (fuzzy_match_enabled && fuzzyMatch(candidate_lower, partial_lower)) {
        return 500;
    }
    
    return 0; // No match
}

bool AutocompleteEngine::fuzzyMatch(const std::string& candidate, const std::string& query) {
    if (query.empty()) return true;
    if (candidate.empty()) return false;
    
    size_t candidate_idx = 0;
    size_t query_idx = 0;
    
    while (candidate_idx < candidate.size() && query_idx < query.size()) {
        if (candidate[candidate_idx] == query[query_idx]) {
            query_idx++;
        }
        candidate_idx++;
    }
    
    return query_idx == query.size();
}

std::string AutocompleteEngine::getCompletionText(const AutocompleteSuggestion& suggestion, 
                                                 const std::string& original_input) {
    // Find the word being completed in the original input
    size_t word_start = 0;
    for (int i = static_cast<int>(cursor_position) - 1; i >= 0; --i) {
        char c = original_input[i];
        if (std::isspace(c) || c == '(' || c == ')' || c == ',' || c == ';' || c == '=' || c == '+' || c == '-') {
            word_start = i + 1;
            break;
        }
    }
    
    // Replace the partial word with the completion
    std::string result = original_input;
    std::string completion_text = suggestion.text;
    
    // If we're completing a function and it's not already followed by '(', add it
    if (completion_text.find('.') == std::string::npos && // Not a namespace
        cursor_position < original_input.size() && 
        original_input[cursor_position] != '(') {
        completion_text += "(";
    }
    
    result.replace(word_start, cursor_position - word_start, completion_text);
    
    return result;
}

void AutocompleteEngine::clearRegistry() {
    api_registry.clear();
    namespace_cache.clear();
    current_suggestions.clear();
}

std::vector<std::string> AutocompleteEngine::getNamespaces() const {
    std::vector<std::string> namespaces;
    for (const auto& func : api_registry) {
        if (func.is_namespace) {
            namespaces.push_back(func.name);
        }
    }
    return namespaces;
}

std::vector<LuaAPIFunction> AutocompleteEngine::getFunctionsInNamespace(const std::string& namespace_name) const {
    std::vector<LuaAPIFunction> functions;
    auto ns_it = namespace_cache.find(namespace_name);
    if (ns_it != namespace_cache.end()) {
        for (LuaAPIFunction* func : ns_it->second) {
            functions.push_back(*func);
        }
    }
    return functions;
}

void AutocompleteEngine::buildFromLuaState(sol::state& lua) {
    // Extract global functions and namespaces from the Lua state
    std::vector<std::string> globals = APIHelpers::extractLuaGlobals(lua);
    
    // Register discovered functions
    for (const auto& global : globals) {
        // Check if it's a function
        sol::object obj = lua[global];
        if (obj.is<sol::function>()) {
            // Register as a global function
            registerFunction(global, "", global + "()", 
                           "Dynamically discovered function: " + global);
        } else if (obj.is<sol::table>()) {
            // It's a namespace/table - register it and explore its contents
            registerNamespace(global, "Dynamically discovered namespace: " + global);
            
            // Explore the table contents
            sol::table table = obj.as<sol::table>();
            table.for_each([&](sol::object key, sol::object value) {
                if (key.is<std::string>() && value.is<sol::function>()) {
                    std::string func_name = key.as<std::string>();
                    std::string full_name = global + "." + func_name;
                    
                    registerFunction(func_name, global, full_name + "()", 
                                   "Dynamically discovered function: " + full_name);
                }
            });
        }
    }
    
    // Rebuild the namespace cache after dynamic registration
    buildNamespaceCache();
}

// API Helper functions
namespace APIHelpers {

std::vector<std::string> extractLuaGlobals(sol::state& lua) {
    std::vector<std::string> globals;
    
    try {
        // Get the global table
        sol::table global_table = lua.globals();
        
        // Iterate through all global variables
        global_table.for_each([&](sol::object key, sol::object value) {
            if (key.is<std::string>()) {
                std::string name = key.as<std::string>();
                
                // Skip standard Lua globals that we don't want in autocomplete
                if (name == "_G" || name == "_VERSION" || name == "arg" || 
                    name == "package" || name == "require" || name == "module" ||
                    name == "getfenv" || name == "setfenv" || name == "unpack" ||
                    name == "xpcall" || name == "pcall" || name == "assert" ||
                    name == "error" || name == "type" || name == "next" ||
                    name == "pairs" || name == "ipairs" || name == "getmetatable" ||
                    name == "setmetatable" || name == "rawget" || name == "rawset" ||
                    name == "rawequal" || name == "rawlen" || name == "tostring" ||
                    name == "tonumber" || name == "select" || name == "print" ||
                    name == "string" || name == "table" || name == "math" ||
                    name == "io" || name == "os" || name == "debug" ||
                    name == "coroutine" || name == "bit32" || name == "utf8") {
                    return;
                }
                
                // Add functions and custom tables/namespaces
                if (value.is<sol::function>() || value.is<sol::table>()) {
                    globals.push_back(name);
                }
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "[Autocomplete] Failed to iterate Lua globals: " << e.what() << std::endl;
        // Return empty vector to avoid crashing if the Lua state is not properly initialized
    }
    
    return globals;
}

std::string formatFunctionSignature(const std::string& name, 
                                   const std::vector<std::string>& params,
                                   const std::string& return_type) {
    std::string signature = name + "(";
    
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) signature += ", ";
        signature += params[i];
    }
    
    signature += ")";
    
    if (!return_type.empty()) {
        signature += " -> " + return_type;
    }
    
    return signature;
}

std::string extractFunctionDoc(sol::state& lua, const std::string& function_path) {
    try {
        // Try to get documentation from a special __doc table if it exists
        sol::optional<sol::table> doc_table = lua["__doc"];
        if (doc_table) {
            sol::optional<std::string> doc = doc_table.value()[function_path];
            if (doc) {
                return doc.value();
            }
        }
        
        // Try to get documentation from function metadata
        sol::object func_obj = lua[function_path];
        if (func_obj.is<sol::table>()) {
            sol::table func_table = func_obj.as<sol::table>();
            sol::optional<std::string> doc = func_table["__doc"];
            if (doc) {
                return doc.value();
            }
        }
        
        // Return empty string if no documentation found
        return "";
    } catch (const std::exception& e) {
        return "";
    }
}

} // namespace APIHelpers

std::string AutocompleteEngine::getParameterHint(const std::string& function_name, int parameter_index) {
    // Find the function in our registry
    for (const auto& func : api_registry) {
        if (func.name == function_name || func.namespace_path + "." + func.name == function_name) {
            // Parse the signature to extract parameter information
            std::string signature = func.signature;
            size_t paren_start = signature.find('(');
            size_t paren_end = signature.find(')', paren_start);
            
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                std::string params_str = signature.substr(paren_start + 1, paren_end - paren_start - 1);
                
                // Split parameters by comma
                std::vector<std::string> parameters;
                std::stringstream ss(params_str);
                std::string param;
                
                while (std::getline(ss, param, ',')) {
                    // Trim whitespace
                    param.erase(0, param.find_first_not_of(" 	"));
                    param.erase(param.find_last_not_of(" 	") + 1);
                    if (!param.empty()) {
                        parameters.push_back(param);
                    }
                }
                
                // Return hint for the requested parameter
                if (parameter_index >= 0 && parameter_index < (int)parameters.size()) {
                    std::string hint = signature + "\nParameter " + std::to_string(parameter_index + 1) + ": " + parameters[parameter_index];
                    if (!func.description.empty()) {
                        hint += "\n\n" + func.description;
                    }
                    return hint;
                } else if (!parameters.empty()) {
                    // Show full signature if parameter index is out of range
                    std::string hint = signature + "\n\n" + func.description;
                    return hint;
                }
            }
            
            // Fallback to basic info
            return func.signature + "\n\n" + func.description;
        }
    }
    
    return "";
}

std::string AutocompleteEngine::getFunctionDocumentation(const std::string& function_name) {
    // Find the function in our registry
    for (const auto& func : api_registry) {
        if (func.name == function_name || func.namespace_path + "." + func.name == function_name) {
            std::string doc = func.signature;
            if (!func.description.empty()) {
                doc += "\n\n" + func.description;
            }
            return doc;
        }
    }
    
    return "";
}


std::string AutocompleteEngine::getCurrentFunctionCall() const {
    return current_function_call;
}

int AutocompleteEngine::getCurrentParameterIndex() const {
    return current_parameter_index;
}

} // namespace LuaAutocomplete
