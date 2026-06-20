#include "cheat_engine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace LuaEngineCheatEngine {

void CheatEngine::setApplyInterval(int interval) {
    if (interval > 0) {
        apply_interval_ = interval;
    }
}

//=============================================================================
// Cheat Implementation
//=============================================================================

Cheat::Cheat(uint32_t id, const std::string& name, uint32_t address, const std::string& domain, 
             LuaEngineRamSearch::WatchSize size)
    : id_(id), name_(name), address_(address), domain_(domain), size_(size),
      type_(CheatType::STATIC_VALUE), trigger_(CheatTrigger::ALWAYS),
      enabled_(true), value_(0), delta_value_(0), range_size_(1),
      hit_count_(0), last_applied_value_(0), dirty_(false),
      memory_manager_(nullptr) {
}

Cheat::~Cheat() {
}

void Cheat::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    memory_manager_ = memory_mgr;
}

void Cheat::apply() {
    if (!enabled_ || !memory_manager_) return;
    
    switch (type_) {
        case CheatType::STATIC_VALUE:
            applyStaticValue();
            break;
        case CheatType::POINTER_VALUE:
            applyPointerValue();
            break;
        case CheatType::CODE_MODIFICATION:
            applyCodeModification();
            break;
        case CheatType::CONDITIONAL:
            applyConditional();
            break;
        case CheatType::INCREASE_DECREASE:
            applyIncreaseDecrease();
            break;
        case CheatType::FREEZE_RANGE:
            applyFreezeRange();
            break;
    }
}

void Cheat::reset() {
    hit_count_ = 0;
    last_applied_value_ = 0;
    dirty_ = false;
}

void Cheat::toggle() {
    enabled_ = !enabled_;
}

void Cheat::setStaticValue(uint64_t value) {
    value_ = value;
    type_ = CheatType::STATIC_VALUE;
}

void Cheat::setPointerPath(const std::vector<uint32_t>& path) {
    pointer_path_ = path;
    type_ = CheatType::POINTER_VALUE;
}

void Cheat::setCodeModification(const std::vector<uint8_t>& original, const std::vector<uint8_t>& modified) {
    original_code_ = original;
    modified_code_ = modified;
    type_ = CheatType::CODE_MODIFICATION;
}

void Cheat::setCondition(const std::string& lua_condition) {
    condition_ = lua_condition;
    type_ = CheatType::CONDITIONAL;
}

void Cheat::setDeltaValue(int64_t delta) {
    delta_value_ = delta;
    type_ = CheatType::INCREASE_DECREASE;
}

void Cheat::setRangeSize(uint32_t size) {
    range_size_ = size;
    type_ = CheatType::FREEZE_RANGE;
}

std::string Cheat::getValueString() const {
    return LuaEngineRamSearch::RamSearchEngine::formatValue(value_, 
                                                          LuaEngineRamSearch::WatchDisplayType::UNSIGNED, 
                                                          size_);
}

std::string Cheat::getTypeString() const {
    switch (type_) {
        case CheatType::STATIC_VALUE: return "Static Value";
        case CheatType::POINTER_VALUE: return "Pointer Value";
        case CheatType::CODE_MODIFICATION: return "Code Modification";
        case CheatType::CONDITIONAL: return "Conditional";
        case CheatType::INCREASE_DECREASE: return "Increase/Decrease";
        case CheatType::FREEZE_RANGE: return "Freeze Range";
        default: return "Unknown";
    }
}

std::string Cheat::getTriggerString() const {
    switch (trigger_) {
        case CheatTrigger::ALWAYS: return "Always";
        case CheatTrigger::ON_LOAD: return "On Load";
        case CheatTrigger::ON_FRAME: return "On Frame";
        case CheatTrigger::ON_CONDITION: return "On Condition";
        case CheatTrigger::ON_HOTKEY: return "On Hotkey";
        default: return "Unknown";
    }
}

std::string Cheat::getStatusString() const {
    if (!enabled_) return "Disabled";
    if (hit_count_ == 0) return "Ready";
    return "Applied " + std::to_string(hit_count_) + " times";
}

std::string Cheat::serialize() const {
    std::stringstream ss;
    ss << id_ << "," << name_ << "," << description_ << "," << domain_ << ","
       << address_ << "," << value_ << "," << static_cast<int>(size_) << ","
       << static_cast<int>(type_) << "," << static_cast<int>(trigger_) << ","
       << (enabled_ ? 1 : 0) << "," << condition_ << "," << delta_value_ << "," << range_size_;
    return ss.str();
}

bool Cheat::deserialize(const std::string& data) {
    std::stringstream ss(data);
    std::string token;
    std::vector<std::string> tokens;
    
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    
    if (tokens.size() < 13) return false;
    
    try {
        id_ = std::stoul(tokens[0]);
        name_ = tokens[1];
        description_ = tokens[2];
        domain_ = tokens[3];
        address_ = std::stoul(tokens[4]);
        value_ = std::stoull(tokens[5]);
        size_ = static_cast<LuaEngineRamSearch::WatchSize>(std::stoi(tokens[6]));
        type_ = static_cast<CheatType>(std::stoi(tokens[7]));
        trigger_ = static_cast<CheatTrigger>(std::stoi(tokens[8]));
        enabled_ = (std::stoi(tokens[9]) != 0);
        condition_ = tokens[10];
        delta_value_ = std::stoll(tokens[11]);
        range_size_ = std::stoul(tokens[12]);
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

//=============================================================================
// Cheat Internal Methods
//=============================================================================

uint64_t Cheat::readValue(uint32_t address) const {
    if (!memory_manager_) return 0;
    
    switch (size_) {
        case LuaEngineRamSearch::WatchSize::BYTE_1:
            return memory_manager_->readByte(domain_, address);
        case LuaEngineRamSearch::WatchSize::BYTE_2:
            return memory_manager_->readWord(domain_, address);
        case LuaEngineRamSearch::WatchSize::BYTE_4:
            return memory_manager_->readDWord(domain_, address);
        case LuaEngineRamSearch::WatchSize::BYTE_8:
            {
                uint64_t low = memory_manager_->readDWord(domain_, address);
                uint64_t high = memory_manager_->readDWord(domain_, address + 4);
                return low | (high << 32);
            }
        default:
            return 0;
    }
}

void Cheat::writeValue(uint32_t address, uint64_t value) {
    if (!memory_manager_) return;
    
    switch (size_) {
        case LuaEngineRamSearch::WatchSize::BYTE_1:
            memory_manager_->writeByte(domain_, address, static_cast<uint8_t>(value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_2:
            memory_manager_->writeWord(domain_, address, static_cast<uint16_t>(value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_4:
            memory_manager_->writeDWord(domain_, address, static_cast<uint32_t>(value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_8:
            memory_manager_->writeDWord(domain_, address, static_cast<uint32_t>(value & 0xFFFFFFFF));
            memory_manager_->writeDWord(domain_, address + 4, static_cast<uint32_t>(value >> 32));
            break;
    }
}

uint32_t Cheat::resolvePointer() const {
    if (pointer_path_.empty()) return address_;
    
    uint32_t current_address = address_;
    
    for (size_t i = 0; i < pointer_path_.size(); ++i) {
        uint32_t pointer_value = static_cast<uint32_t>(readValue(current_address));
        current_address = pointer_value + pointer_path_[i];
    }
    
    return current_address;
}

bool Cheat::evaluateCondition() const {
    if (condition_.empty()) return true;
    
    // TODO: Implement Lua condition evaluation
    // For now, always return true
    return true;
}

void Cheat::applyStaticValue() {
    writeValue(address_, value_);
    last_applied_value_ = value_;
    hit_count_++;
}

void Cheat::applyPointerValue() {
    uint32_t resolved_address = resolvePointer();
    writeValue(resolved_address, value_);
    last_applied_value_ = value_;
    hit_count_++;
}

void Cheat::applyCodeModification() {
    if (original_code_.empty() || modified_code_.empty()) return;
    
    // Write modified code bytes
    for (size_t i = 0; i < modified_code_.size(); ++i) {
        if (memory_manager_) {
            memory_manager_->writeByte(domain_, address_ + i, modified_code_[i]);
        }
    }
    
    hit_count_++;
}

void Cheat::applyConditional() {
    if (evaluateCondition()) {
        applyStaticValue();
    }
}

void Cheat::applyIncreaseDecrease() {
    uint64_t current_value = readValue(address_);
    int64_t new_value = static_cast<int64_t>(current_value) + delta_value_;
    
    // Clamp to valid range based on size
    switch (size_) {
        case LuaEngineRamSearch::WatchSize::BYTE_1:
            new_value = std::max(0LL, std::min(255LL, new_value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_2:
            new_value = std::max(0LL, std::min(65535LL, new_value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_4:
            new_value = std::max(0LL, std::min(4294967295LL, new_value));
            break;
        case LuaEngineRamSearch::WatchSize::BYTE_8:
            // No clamping for 64-bit
            break;
    }
    
    writeValue(address_, static_cast<uint64_t>(new_value));
    last_applied_value_ = static_cast<uint64_t>(new_value);
    hit_count_++;
}

void Cheat::applyFreezeRange() {
    for (uint32_t i = 0; i < range_size_; ++i) {
        writeValue(address_ + i, value_);
    }
    last_applied_value_ = value_;
    hit_count_++;
}

//=============================================================================
// CheatEngine Implementation
//=============================================================================

CheatEngine::CheatEngine() 
    : memory_manager_(nullptr), enabled_(true), auto_save_(false),
      next_cheat_id_(1), frame_count_(0), cheats_applied_this_frame_(0) {
}

CheatEngine::~CheatEngine() {
}

void CheatEngine::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    memory_manager_ = memory_mgr;
    
    // Initialize all existing cheats
    for (auto& cheat : cheats_) {
        cheat->initialize(memory_mgr);
    }
}

uint32_t CheatEngine::addCheat(const std::string& name, uint32_t address, const std::string& domain, 
                              LuaEngineRamSearch::WatchSize size) {
    uint32_t id = generateCheatId();
    auto cheat = std::make_unique<Cheat>(id, name, address, domain, size);
    
    if (memory_manager_) {
        cheat->initialize(memory_manager_);
    }
    
    cheats_.push_back(std::move(cheat));
    markDirty();
    
    return id;
}

void CheatEngine::removeCheat(uint32_t id) {
    cheats_.erase(
        std::remove_if(cheats_.begin(), cheats_.end(),
                      [id](const std::unique_ptr<Cheat>& cheat) {
                          return cheat->getId() == id;
                      }),
        cheats_.end()
    );
    markDirty();
}

void CheatEngine::removeAllCheats() {
    cheats_.clear();
    markDirty();
}

void CheatEngine::toggleCheat(uint32_t id) {
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->toggle();
        markDirty();
    }
}

void CheatEngine::enableCheat(uint32_t id, bool enabled) {
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->setEnabled(enabled);
        markDirty();
    }
}

void CheatEngine::enableAllCheats(bool enabled) {
    for (auto& cheat : cheats_) {
        cheat->setEnabled(enabled);
    }
    markDirty();
}

void CheatEngine::resetAllCheats() {
    for (auto& cheat : cheats_) {
        cheat->reset();
    }
}

void CheatEngine::applyCheats() {
    if (!enabled_) return;
    
    frame_count_++;
    cheats_applied_this_frame_ = 0;
    
    for (auto& cheat : cheats_) {
        if (cheat->isEnabled()) {
            CheatTrigger trigger = cheat->getTrigger();
            if (trigger == CheatTrigger::ALWAYS || trigger == CheatTrigger::ON_FRAME) {
                cheat->apply();
                cheats_applied_this_frame_++;
            }
        }
    }
    
    autoSaveIfNeeded();
}

void CheatEngine::applyCheatsByTrigger(CheatTrigger trigger) {
    if (!enabled_) return;
    
    for (auto& cheat : cheats_) {
        if (cheat->isEnabled() && cheat->getTrigger() == trigger) {
            cheat->apply();
        }
    }
}

bool CheatEngine::saveCheatFile(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    // Write header
    file << "# DOSBox-X Cheat File\n";
    file << "# Version: " << FILE_FORMAT_VERSION << "\n";
    file << "# Format: ID,Name,Description,Domain,Address,Value,Size,Type,Trigger,Enabled,Condition,Delta,RangeSize\n";
    file << "#\n";
    
    // Write cheats
    for (const auto& cheat : cheats_) {
        file << cheat->serialize() << "\n";
    }
    
    return true;
}

bool CheatEngine::loadCheatFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    removeAllCheats();
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        auto cheat = std::make_unique<Cheat>(0, "", 0, "", LuaEngineRamSearch::WatchSize::BYTE_1);
        if (cheat->deserialize(line)) {
            if (memory_manager_) {
                cheat->initialize(memory_manager_);
            }
            cheats_.push_back(std::move(cheat));
        }
    }
    
    return true;
}

Cheat* CheatEngine::findCheat(uint32_t id) {
    for (auto& cheat : cheats_) {
        if (cheat->getId() == id) {
            return cheat.get();
        }
    }
    return nullptr;
}

const Cheat* CheatEngine::findCheat(uint32_t id) const {
    for (const auto& cheat : cheats_) {
        if (cheat->getId() == id) {
            return cheat.get();
        }
    }
    return nullptr;
}

Cheat* CheatEngine::findCheatByName(const std::string& name) {
    for (auto& cheat : cheats_) {
        if (cheat->getName() == name) {
            return cheat.get();
        }
    }
    return nullptr;
}

size_t CheatEngine::getEnabledCheatCount() const {
    return std::count_if(cheats_.begin(), cheats_.end(),
                        [](const std::unique_ptr<Cheat>& cheat) {
                            return cheat->isEnabled();
                        });
}

void CheatEngine::freezeAddress(uint32_t address, uint64_t value, const std::string& domain, 
                               LuaEngineRamSearch::WatchSize size) {
    // Create a static cheat for freezing
    std::string name = "Freeze " + std::to_string(address);
    uint32_t id = addCheat(name, address, domain, size);
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->setStaticValue(value);
        cheat->setTrigger(CheatTrigger::ALWAYS);
    }
}

void CheatEngine::unfreezeAddress(uint32_t address, const std::string& domain) {
    // Find and remove cheat for this address
    cheats_.erase(
        std::remove_if(cheats_.begin(), cheats_.end(),
                      [address, &domain](const std::unique_ptr<Cheat>& cheat) {
                          return cheat->getAddress() == address && cheat->getDomain() == domain;
                      }),
        cheats_.end()
    );
}

uint32_t CheatEngine::createStaticCheat(const std::string& name, uint32_t address, const std::string& domain,
                                       LuaEngineRamSearch::WatchSize size, uint64_t value) {
    uint32_t id = addCheat(name, address, domain, size);
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->setStaticValue(value);
    }
    return id;
}

uint32_t CheatEngine::createPointerCheat(const std::string& name, const std::vector<uint32_t>& pointer_path,
                                        const std::string& domain, LuaEngineRamSearch::WatchSize size, uint64_t value) {
    uint32_t id = addCheat(name, pointer_path.empty() ? 0 : pointer_path[0], domain, size);
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->setPointerPath(pointer_path);
        cheat->setStaticValue(value);
    }
    return id;
}

uint32_t CheatEngine::createCodeCheat(const std::string& name, uint32_t address, const std::string& domain,
                                     const std::vector<uint8_t>& original, const std::vector<uint8_t>& modified) {
    uint32_t id = addCheat(name, address, domain, LuaEngineRamSearch::WatchSize::BYTE_1);
    auto* cheat = findCheat(id);
    if (cheat) {
        cheat->setCodeModification(original, modified);
    }
    return id;
}

void CheatEngine::registerHotkey(uint32_t cheat_id, const std::string& hotkey) {
    hotkey_map_[hotkey] = cheat_id;
}

void CheatEngine::unregisterHotkey(uint32_t cheat_id) {
    for (auto it = hotkey_map_.begin(); it != hotkey_map_.end(); ++it) {
        if (it->second == cheat_id) {
            hotkey_map_.erase(it);
            break;
        }
    }
}

void CheatEngine::processHotkey(const std::string& hotkey) {
    auto it = hotkey_map_.find(hotkey);
    if (it != hotkey_map_.end()) {
        auto* cheat = findCheat(it->second);
        if (cheat) {
            cheat->apply();
        }
    }
}

//=============================================================================
// CheatEngine Internal Methods
//=============================================================================

uint32_t CheatEngine::generateCheatId() {
    return next_cheat_id_++;
}

void CheatEngine::markDirty() {
    // Mark that cheats have been modified
}

void CheatEngine::autoSaveIfNeeded() {
    if (auto_save_ && !cheat_file_.empty()) {
        // Only save occasionally to avoid performance issues
        if (frame_count_ % 3600 == 0) {  // Save every 60 seconds at 60fps
            saveCheatFile(cheat_file_);
        }
    }
}

} // namespace LuaEngineCheatEngine