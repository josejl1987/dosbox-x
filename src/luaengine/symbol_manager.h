#ifndef SYMBOL_MANAGER_H
#define SYMBOL_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace LuaEngineSymbols {

// Symbol information structure
struct Symbol {
    uint32_t address;           // Final calculated address (after segment mapping)
    std::string name;
    std::string module;
    std::string type;           // "function", "data", "label", etc.
    uint32_t size;
    uint16_t segment;           // Original segment from file
    uint16_t offset;            // Original offset from file
    uint32_t file_address;      // Original address from file (before mapping)
    std::string segment_name;   // Segment name (e.g., "_TEXT", "_DATA")
    std::string sourceline;     // Optional source line for listing view
    
    Symbol() : address(0), size(0), segment(0), offset(0), file_address(0) {}
    Symbol(uint32_t addr, const std::string& n, const std::string& t = "unknown") 
        : address(addr), name(n), type(t), size(0), segment(0), offset(0), file_address(addr) {}
};

// Segment mapping structure
struct SegmentMapping {
    std::string segment_name;   // Segment name from file (e.g., "_TEXT", "_DATA")
    uint16_t file_segment;      // Original segment value from file
    uint32_t memory_base;       // Actual base address in memory
    uint32_t size;              // Segment size
    bool enabled;               // Whether this mapping is active
    
    SegmentMapping() : file_segment(0), memory_base(0), size(0), enabled(true) {}
    SegmentMapping(const std::string& name, uint16_t seg, uint32_t base, uint32_t sz = 0x10000) 
        : segment_name(name), file_segment(seg), memory_base(base), size(sz), enabled(true) {}
};

// ponytail: PR6 — user annotation at an address
struct Annotation {
    uint32_t address;
    std::string comment;
    std::string module;  // optional
};

// ponytail: PR6 — typed data range
struct TypedDataRange {
    uint32_t start_address;
    uint32_t length;
    std::string data_type;    // "word_array", "string", "struct", etc.
    uint32_t element_size;
    std::string module;       // optional
};

// Symbol file format types
enum class SymbolFormat {
    UNKNOWN,
    MASM_MAP,      // Microsoft MASM .MAP files
    MASM_LST,      // Microsoft MASM .LST files (listing files)
    COFF_SYMBOLS,  // COFF object file symbols
    WATCOM_MAP,    // Watcom linker .MAP files
    BORLAND_MAP,   // Borland linker .MAP files
    GNU_MAP        // GNU ld .MAP files
};

class SymbolManager {
public:
    SymbolManager();
    ~SymbolManager();

    static constexpr uint32_t INVALID_ADDRESS = 0xFFFFFFFF;
    
    // Symbol file loading (lightweight text-based parser)
    typedef std::function<void(int percentage, const std::string& message)> ProgressCallback;
    void setProgressCallback(ProgressCallback callback) { progress_callback_ = callback; }
    bool loadFromFile(const std::string& filename);

    void clearSymbols();
    
    // Symbol lookup
    std::string getSymbolName(uint32_t address, bool include_offset = true);
    uint32_t getSymbolAddress(const std::string& name);
    Symbol* findSymbol(uint32_t address);
    Symbol* findSymbolByName(const std::string& name);
    
    // Symbol enumeration
    std::vector<Symbol> getAllSymbols() const;
    std::vector<Symbol> getSymbolsInRange(uint32_t start_addr, uint32_t end_addr) const;
    std::vector<std::string> getSymbolNames() const;
    
    // Symbol file info
    bool hasSymbols() const { return !symbols_by_address_.empty(); }
    size_t getSymbolCount() const { return symbols_by_address_.size(); }
    std::string getLoadedFileName() const { return loaded_filename_; }
    SymbolFormat getLoadedFormat() const { return loaded_format_; }
    
    // Address calculation helpers
    uint32_t segmentOffsetToLinear(uint16_t segment, uint16_t offset);
    void linearToSegmentOffset(uint32_t linear, uint16_t& segment, uint16_t& offset);
    
    // Symbol management
    void addSymbol(const Symbol& symbol);
    void removeSymbol(uint32_t address);
    void removeSymbol(const std::string& name);
    
    // Segment mapping
    void addSegmentMapping(const SegmentMapping& mapping);
    void removeSegmentMapping(const std::string& segment_name);
    void updateSegmentMapping(const std::string& segment_name, uint32_t memory_base);
    void enableSegmentMapping(const std::string& segment_name, bool enabled);
    std::vector<SegmentMapping> getSegmentMappings() const;
    SegmentMapping* findSegmentMapping(const std::string& segment_name);
    SegmentMapping* findSegmentMapping(uint16_t file_segment);
    void clearSegmentMappings();
    void autoDetectSegments();
    void remapAllSymbols();
    void recalculateAllSymbols(); // Alias for remapAllSymbols
    
    // ponytail: PR6 — Annotation management
    void addAnnotation(const Annotation& ann);
    std::vector<Annotation> getAnnotationsByAddress(uint32_t address) const;
    std::vector<Annotation> getAnnotationsByText(const std::string& substring) const;
    bool removeAnnotation(uint32_t address, const std::string& comment);
    void clearAnnotations();
    
    // ponytail: PR6 — Typed data range management
    bool addTypedDataRange(const TypedDataRange& range);
    std::vector<TypedDataRange> getTypedDataRangesAt(uint32_t address) const;
    std::vector<TypedDataRange> getAllTypedDataRanges(const std::string& module = "") const;
    bool removeTypedDataRange(uint32_t start_address);
    void clearTypedDataRanges();
    
private:
    // Symbol storage
    std::map<uint32_t, Symbol> symbols_by_address_;
    std::map<std::string, uint32_t> symbols_by_name_;
    
    // Segment mapping storage
    std::map<std::string, SegmentMapping> segment_mappings_;
    std::map<uint16_t, std::string> segment_by_number_;
    
    // ponytail: PR6 — annotation/typed-data storage
    std::multimap<uint32_t, Annotation> annotations_by_addr_;
    std::unordered_map<std::string, std::vector<uint32_t>> annotations_by_text_;
    std::vector<TypedDataRange> typed_ranges_;
    
    // File info
    std::string loaded_filename_;
    SymbolFormat loaded_format_;
    
    // Progress tracking
    ProgressCallback progress_callback_;
    
    // Helper functions
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    bool isHexString(const std::string& str);
    uint32_t parseHexValue(const std::string& str);
    uint16_t parseHexValue16(const std::string& str);
    
    // Symbol name formatting
    std::string formatSymbolWithOffset(const Symbol& symbol, uint32_t address);
    
    // Segment mapping helpers
    uint32_t mapAddressToMemory(uint16_t segment, uint16_t offset, const std::string& segment_name = "");
    void updateSymbolAddress(Symbol& symbol);
};

// Global symbol manager instance - managed by LuaEngine
// This pointer is set by LuaEngine to point to its member
extern SymbolManager* g_symbol_manager;

// Convenience functions
inline std::string GetSymbolName(uint32_t address, bool include_offset = true) {
    return g_symbol_manager ? g_symbol_manager->getSymbolName(address, include_offset) : "";
}

inline uint32_t GetSymbolAddress(const std::string& name) {
    return g_symbol_manager ? g_symbol_manager->getSymbolAddress(name) : SymbolManager::INVALID_ADDRESS;
}

inline bool HasSymbols() {
    return g_symbol_manager && g_symbol_manager->hasSymbols();
}

} // namespace LuaEngineSymbols

#endif // SYMBOL_MANAGER_H
