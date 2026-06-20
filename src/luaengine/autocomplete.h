#ifndef LUAENGINE_AUTOCOMPLETE_H
#define LUAENGINE_AUTOCOMPLETE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#define SOL_ALL_SAFETIES_ON  1
#include "../../vs/lua/sol/sol.hpp"

namespace LuaAutocomplete {

    struct LuaAPIFunction {
        std::string name;                    // Function name (e.g., "readbyte")
        std::string namespace_path;          // Full path (e.g., "memory.readbyte")
        std::string signature;               // Function signature with parameters
        std::string description;             // Help text/description
        std::vector<std::string> parameters; // Parameter names and types
        std::string return_type;             // Return value type
        bool is_namespace;                   // True if this is a namespace (e.g., "memory")
        
        LuaAPIFunction() : is_namespace(false) {}
        LuaAPIFunction(const std::string& n, const std::string& ns, bool is_ns = false)
            : name(n), namespace_path(ns), is_namespace(is_ns) {}
    };

    struct AutocompleteSuggestion {
        std::string text;           // The completion text
        std::string display_text;   // What to show in the dropdown
        std::string description;    // Help text
        int priority;               // Higher = better match
        bool is_exact_match;        // True for exact matches
        
        AutocompleteSuggestion() : priority(0), is_exact_match(false) {}
        AutocompleteSuggestion(const std::string& t, const std::string& d, int p = 0)
            : text(t), display_text(d), priority(p), is_exact_match(false) {}
    };

    class AutocompleteEngine {
    private:
        std::vector<LuaAPIFunction> api_registry;
        std::vector<AutocompleteSuggestion> current_suggestions;
        std::map<std::string, std::vector<LuaAPIFunction*>> namespace_cache;
        
        // Input analysis
        std::string current_input;
        std::string current_namespace;
        std::string current_partial;
        
        // Enhanced context analysis
        struct ContextFlags {
            bool in_function_call = false;
            bool in_string = false;
            bool in_comment = false;
            bool in_assignment = false;
            int parenthesis_depth = 0;
            int bracket_depth = 0;
        } context_flags;
        
        // Function call context
        std::string current_function_call;
        int current_parameter_index = 0;
        size_t cursor_position;
        
        // Internal methods
        void buildNamespaceCache();
        void analyzeInput(const std::string& input, size_t cursor_pos);
        void findNamespaceSuggestions(const std::string& partial);
        void findFunctionSuggestions(const std::string& namespace_name, const std::string& partial);
        void findGlobalSuggestions(const std::string& partial);
        int calculateMatchPriority(const std::string& candidate, const std::string& partial);
        bool fuzzyMatch(const std::string& candidate, const std::string& query);
        
        // Enhanced text analysis methods
        void analyzeContext(const std::string& input_to_cursor);
        size_t findWordStart(const std::string& input_to_cursor);
        void parseNamespaceAndPartial(const std::string& current_word);
        void analyzeFunctionCallContext(const std::string& input_to_cursor, size_t word_start);
        
        // Parameter hints and documentation
        std::string getParameterHint(const std::string& function_name, int parameter_index);
        std::string getFunctionDocumentation(const std::string& function_name);
        
        // Function call context getters
        std::string getCurrentFunctionCall() const;
        int getCurrentParameterIndex() const;
        
    public:
        AutocompleteEngine();
        ~AutocompleteEngine() = default;
        
        // API registration methods
        void registerFunction(const std::string& name, const std::string& namespace_path,
                             const std::string& signature = "", const std::string& description = "");
        void registerNamespace(const std::string& name, const std::string& description = "");
        void buildFromLuaState(sol::state& lua);
        
        // Autocomplete functionality
        std::vector<AutocompleteSuggestion> getSuggestions(const std::string& input, 
                                                          size_t cursor_position = std::string::npos);
        std::string getCompletionText(const AutocompleteSuggestion& suggestion, 
                                     const std::string& original_input);
        
        // Utility methods
        void clearRegistry();
        size_t getRegistrySize() const { return api_registry.size(); }
        std::vector<std::string> getNamespaces() const;
        std::vector<LuaAPIFunction> getFunctionsInNamespace(const std::string& namespace_name) const;
        
        // Configuration
        void setMaxSuggestions(size_t max) { max_suggestions = max; }
        void setFuzzyMatchEnabled(bool enabled) { fuzzy_match_enabled = enabled; }
        
    private:
        size_t max_suggestions = 20;
        bool fuzzy_match_enabled = true;
    };

    // Helper functions for common Lua API patterns
    namespace APIHelpers {
        std::vector<std::string> extractLuaGlobals(sol::state& lua);
        std::string formatFunctionSignature(const std::string& name, 
                                           const std::vector<std::string>& params,
                                           const std::string& return_type = "");
        std::string extractFunctionDoc(sol::state& lua, const std::string& function_path);
    }

} // namespace LuaAutocomplete

#endif // LUAENGINE_AUTOCOMPLETE_H