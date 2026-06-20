#include "watch_list.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <bitset>
#include <cmath>
#include <cstring>

namespace LuaEngineWatchList {

//=============================================================================
// Watch Implementation
//=============================================================================

Watch::Watch(uint32_t address, const std::string& domain, WatchSize size)
    : address_(address), domain_(domain), size_(size), 
      display_type_(WatchDisplayType::HEXADECIMAL), 
      current_value_(0), previous_value_(0), initial_value_(0),
      frozen_(false), frozen_value_(0), 
      change_count_(0), value_changed_(false),
      memory_manager_(nullptr), codepage_(932), string_length_(16) {
}

Watch::~Watch() {
}

void Watch::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    memory_manager_ = memory_mgr;
    updateValue();
    initial_value_ = current_value_;
}

void Watch::updateValue() {
    if (!memory_manager_) return;
    
    auto domain_type = memory_manager_->getDomainTypeFromName(domain_);
    auto* domain = memory_manager_->GetDomain(domain_type);
    if (!domain) return;
    
    previous_value_ = current_value_;
    
    switch (size_) {
        case WatchSize::BYTE_1:
            current_value_ = domain->readByte(address_);
            break;
        case WatchSize::BYTE_2:
            current_value_ = domain->readWord(address_);
            break;
        case WatchSize::BYTE_4:
            current_value_ = domain->readDWord(address_);
            break;
        case WatchSize::BYTE_8:
            // Read as two 32-bit values
            current_value_ = domain->readDWord(address_);
            current_value_ |= (static_cast<uint64_t>(domain->readDWord(address_ + 4)) << 32);
            break;
    }
    
    value_changed_ = (current_value_ != previous_value_);
    if (value_changed_) {
        change_count_++;
    }
}

void Watch::updateValueFromCache(uint64_t cached_value) {
    previous_value_ = current_value_;
    current_value_ = cached_value;
    value_changed_ = (current_value_ != previous_value_);
    if (value_changed_) {
        change_count_++;
    }
}

void Watch::poke(uint64_t value) {
    if (!memory_manager_) return;
    
    auto domain_type = memory_manager_->getDomainTypeFromName(domain_);
    auto* domain = memory_manager_->GetDomain(domain_type);
    if (!domain) return;
    
    switch (size_) {
        case WatchSize::BYTE_1:
            domain->writeByte(address_, static_cast<uint8_t>(value));
            break;
        case WatchSize::BYTE_2:
            domain->writeWord(address_, static_cast<uint16_t>(value));
            break;
        case WatchSize::BYTE_4:
            domain->writeDWord(address_, static_cast<uint32_t>(value));
            break;
        case WatchSize::BYTE_8: {
            // Write as two 32-bit values
            uint32_t low = static_cast<uint32_t>(value);
            uint32_t high = static_cast<uint32_t>(value >> 32);
            domain->writeDWord(address_, low);
            domain->writeDWord(address_ + 4, high);
            break;
        }
            domain->writeByte(address_, static_cast<uint8_t>(value));
            break;
    }
    
    updateValue();
}

void Watch::setInitialValue(uint64_t value) {
    initial_value_ = value;
}

void Watch::freeze() {
    frozen_ = true;
    frozen_value_ = current_value_;
}

void Watch::freeze(uint64_t value) {
    frozen_ = true;
    frozen_value_ = value;
}

void Watch::unfreeze() {
    frozen_ = false;
}

void Watch::applyFrozenValue() {
    if (frozen_) {
        poke(frozen_value_);
    }
}

std::string Watch::getAddressString() const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << address_;
    return ss.str();
}

std::string Watch::getCurrentValueString() const {
    return formatValue(current_value_);
}

std::string Watch::getPreviousValueString() const {
    return formatValue(previous_value_);
}

std::string Watch::getInitialValueString() const {
    return formatValue(initial_value_);
}

std::string Watch::getFrozenValueString() const {
    return formatValue(frozen_value_);
}

std::string Watch::formatValue(uint64_t value) const {
    std::stringstream ss;
    
    switch (display_type_) {
        case WatchDisplayType::HEXADECIMAL:
            switch (size_) {
                case WatchSize::BYTE_1:
                    ss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') 
                       << static_cast<uint8_t>(value);
                    break;
                case WatchSize::BYTE_2:
                    ss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') 
                       << static_cast<uint16_t>(value);
                    break;
                case WatchSize::BYTE_4:
                            ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') 
                       << static_cast<uint32_t>(value);
                    break;
                case WatchSize::BYTE_8:
                    ss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') 
                       << value;
                    break;
                            ss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') 
                       << static_cast<uint8_t>(value);
                    break;
            }
            break;
            
        case WatchDisplayType::DECIMAL:
            switch (size_) {
                case WatchSize::BYTE_1:
                    ss << static_cast<uint8_t>(value);
                    break;
                case WatchSize::BYTE_2:
                    ss << static_cast<uint16_t>(value);
                    break;
                case WatchSize::BYTE_4:
                    ss << static_cast<uint32_t>(value);
                    break;
                case WatchSize::BYTE_8:
                    ss << value;
                    break;
                default:
                    ss << value;
                    break;
            }
            break;
            
        case WatchDisplayType::SIGNED_DECIMAL:
            switch (size_) {
                case WatchSize::BYTE_1:
                    ss << static_cast<int8_t>(value);
                    break;
                case WatchSize::BYTE_2:
                    ss << static_cast<int16_t>(value);
                    break;
                case WatchSize::BYTE_4:
                    ss << static_cast<int32_t>(value);
                    break;
                case WatchSize::BYTE_8:
                    ss << static_cast<int64_t>(value);
                    break;
                default:
                    ss << static_cast<int64_t>(value);
                    break;
            }
            break;
            
        case WatchDisplayType::BINARY:
            switch (size_) {
                case WatchSize::BYTE_1:
                    ss << std::bitset<8>(static_cast<uint8_t>(value));
                    break;
                case WatchSize::BYTE_2:
                    ss << std::bitset<16>(static_cast<uint16_t>(value));
                    break;
                case WatchSize::BYTE_4:
                    ss << std::bitset<32>(static_cast<uint32_t>(value));
                    break;
                case WatchSize::BYTE_8:
                    ss << std::bitset<64>(value);
                    break;
                default:
                    ss << std::bitset<32>(static_cast<uint32_t>(value));
                    break;
            }
            break;
            
        case WatchDisplayType::FLOAT:
            if (size_ == WatchSize::BYTE_4) {
                ss << *reinterpret_cast<const float*>(&value);
            } else {
                ss << static_cast<float>(value);
            }
            break;
            
        case WatchDisplayType::DOUBLE:
            if (size_ == WatchSize::BYTE_8) {
                ss << *reinterpret_cast<const double*>(&value);
            } else {
                ss << static_cast<double>(value);
            }
            break;
            
        case WatchDisplayType::STRING_SJIS:
        case WatchDisplayType::STRING_UTF8:
        case WatchDisplayType::STRING_ASCII:
            return readStringValue();
    }
    
    return ss.str();
}

std::string Watch::readStringValue() const {
    if (!memory_manager_) return "";
    
    auto domain_type = memory_manager_->getDomainTypeFromName(domain_);
    auto* domain = memory_manager_->GetDomain(domain_type);
    if (!domain) return "";
    
    switch (display_type_) {
        case WatchDisplayType::STRING_SJIS:
            return domain->readText(address_, string_length_, 932);
        case WatchDisplayType::STRING_UTF8:
            return domain->readText(address_, string_length_, codepage_);
        case WatchDisplayType::STRING_ASCII:
            return domain->readText(address_, string_length_, 437);
        default:
            return domain->readText(address_, string_length_, codepage_);
    }
}

std::string Watch::serialize() const {
    std::stringstream ss;
    ss << address_ << "|" << domain_ << "|" << static_cast<int>(size_) << "|" 
       << static_cast<int>(display_type_) << "|" << notes_ << "|" << codepage_ << "|" 
       << string_length_ << "|" << frozen_ << "|" << frozen_value_;
    return ss.str();
}

bool Watch::deserialize(const std::string& data) {
    std::stringstream ss(data);
    std::string item;
    
    try {
        // Address
        if (!std::getline(ss, item, '|')) return false;
        address_ = std::stoul(item);
        
        // Domain
        if (!std::getline(ss, item, '|')) return false;
        domain_ = item;
        
        // Size
        if (!std::getline(ss, item, '|')) return false;
        size_ = static_cast<WatchSize>(std::stoi(item));
        
        // Display type
        if (!std::getline(ss, item, '|')) return false;
        display_type_ = static_cast<WatchDisplayType>(std::stoi(item));
        
        // Notes
        if (!std::getline(ss, item, '|')) return false;
        notes_ = item;
        
        // Codepage
        if (!std::getline(ss, item, '|')) return false;
        codepage_ = std::stoul(item);
        
        // String length
        if (!std::getline(ss, item, '|')) return false;
        string_length_ = std::stoul(item);
        
        // Frozen
        if (!std::getline(ss, item, '|')) return false;
        frozen_ = (item == "1");
        
        // Frozen value
        if (!std::getline(ss, item, '|')) return false;
        frozen_value_ = std::stoull(item);
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

//=============================================================================
// WatchList Implementation
//=============================================================================

WatchList::WatchList() 
    : memory_manager_(nullptr), cache_dirty_(true), update_interval_ms_(100) {
}

WatchList::~WatchList() {
    clearWatches();
}

void WatchList::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    memory_manager_ = memory_mgr;
    last_update_ = std::chrono::steady_clock::now();
}

void WatchList::addWatch(std::unique_ptr<Watch> watch) {
    if (watch && memory_manager_) {
        watch->initialize(memory_manager_);
        watches_.push_back(std::move(watch));
        cache_dirty_ = true;
    }
}

size_t WatchList::addWatch(uint32_t address, const std::string& domain, WatchSize size) {
    auto watch = std::make_unique<Watch>(address, domain, size);
    size_t index = watches_.size();
    addWatch(std::move(watch));
    return index;
}

void WatchList::removeWatch(size_t index) {
    if (index < watches_.size()) {
        watches_.erase(watches_.begin() + index);
        cache_dirty_ = true;
    }
}

void WatchList::removeWatch(uint32_t address, const std::string& domain) {
    auto it = std::find_if(watches_.begin(), watches_.end(),
                          [address, &domain](const std::unique_ptr<Watch>& watch) {
                              return watch->getAddress() == address && 
                                     watch->getDomain() == domain;
                          });
    
    if (it != watches_.end()) {
        watches_.erase(it);
        cache_dirty_ = true;
    }
}

void WatchList::clearWatches() {
    watches_.clear();
    cache_dirty_ = true;
}

void WatchList::updateAllValues() {
    std::lock_guard<std::mutex> lock(update_mutex_);
    
    for (auto& watch : watches_) {
        watch->updateValue();
        watch->applyFrozenValue(); // Apply freeze if needed
    }
    
    last_update_ = std::chrono::steady_clock::now();
}

void WatchList::updateAllValuesBatched() {
    std::lock_guard<std::mutex> lock(update_mutex_);
    
    if (cache_dirty_) {
        rebuildBatchRequests();
        cache_dirty_ = false;
    }
    
    // Clear existing cache
    address_cache_.clear();
    
    // Process batch requests for each domain
    for (const auto& [domain, requests] : batch_requests_) {
        if (!memory_manager_) continue;
        
        auto domain_type = memory_manager_->getDomainTypeFromName(domain);
        auto* domain_ptr = memory_manager_->GetDomain(domain_type);
        if (!domain_ptr) continue;
        
        for (const auto& request : requests) {
            // Read memory block
            size_t block_size = request.end_address - request.start_address + 1;
            std::vector<uint8_t> buffer(block_size);
            auto data = domain_ptr->readBlock(request.start_address, block_size);
            std::copy(data.begin(), data.end(), buffer.data());
            
            // Update cache for this block
            for (size_t i = 0; i < block_size; ++i) {
                address_cache_[request.start_address + i] = buffer[i];
            }
        }
    }
    
    // Update watches from cache
    for (auto& watch : watches_) {
        uint32_t address = watch->getAddress();
        if (address_cache_.find(address) != address_cache_.end()) {
            uint64_t cached_value = getCachedValue(address, watch->getSize());
            watch->updateValueFromCache(cached_value);
            watch->applyFrozenValue(); // Apply freeze if needed
        }
    }
    
    last_update_ = std::chrono::steady_clock::now();
}

void WatchList::applyAllFrozenValues() {
    for (auto& watch : watches_) {
        watch->applyFrozenValue();
    }
}

void WatchList::unfreezeAll() {
    for (auto& watch : watches_) {
        watch->unfreeze();
    }
}

void WatchList::rebuildBatchRequests() {
    batch_requests_.clear();
    
    if (watches_.empty()) return;
    
    // Group watches by domain
    std::map<std::string, std::vector<Watch*>> domain_watches;
    for (auto& watch : watches_) {
        domain_watches[watch->getDomain()].push_back(watch.get());
    }
    
    // Create batch requests for each domain
    for (const auto& [domain, watches] : domain_watches) {
        std::vector<BatchReadRequest> requests;
        
        // Sort watches by address
        std::vector<Watch*> sorted_watches = watches;
        std::sort(sorted_watches.begin(), sorted_watches.end(),
                 [](const Watch* a, const Watch* b) {
                     return a->getAddress() < b->getAddress();
                 });
        
        // Group consecutive addresses
        if (!sorted_watches.empty()) {
            BatchReadRequest current_request;
            current_request.domain = domain;
            current_request.start_address = sorted_watches[0]->getAddress();
            current_request.end_address = sorted_watches[0]->getAddress() + 
                                        static_cast<uint32_t>(sorted_watches[0]->getSize()) - 1;
            current_request.watches.push_back(sorted_watches[0]);
            
            for (size_t i = 1; i < sorted_watches.size(); ++i) {
                uint32_t addr = sorted_watches[i]->getAddress();
                uint32_t size = static_cast<uint32_t>(sorted_watches[i]->getSize());
                
                // If addresses are close, extend the current request
                if (addr <= current_request.end_address + 64) {
                    current_request.end_address = std::max(current_request.end_address, addr + size - 1);
                    current_request.watches.push_back(sorted_watches[i]);
                } else {
                    // Start a new request
                    requests.push_back(current_request);
                    current_request.start_address = addr;
                    current_request.end_address = addr + size - 1;
                    current_request.watches.clear();
                    current_request.watches.push_back(sorted_watches[i]);
                }
            }
            
            requests.push_back(current_request);
        }
        
        batch_requests_[domain] = requests;
    }
}

void WatchList::clearCache() {
    address_cache_.clear();
}

bool WatchList::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    // Write header
    file << "WatchList_v" << FILE_FORMAT_VERSION << "\n";
    file << watches_.size() << "\n";
    
    // Write watches
    for (const auto& watch : watches_) {
        file << watch->serialize() << "\n";
    }
    
    file.close();
    return true;
}

bool WatchList::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    std::string line;
    
    // Read header
    if (!std::getline(file, line) || line != "WatchList_v1") {
        file.close();
        return false;
    }
    
    // Read count
    if (!std::getline(file, line)) {
        file.close();
        return false;
    }
    
    size_t count = std::stoul(line);
    
    // Clear existing watches
    clearWatches();
    
    // Read watches
    for (size_t i = 0; i < count; ++i) {
        if (!std::getline(file, line)) {
            file.close();
            return false;
        }
        
        auto watch = std::make_unique<Watch>(0, "", WatchSize::BYTE_1);
        if (watch->deserialize(line)) {
            addWatch(std::move(watch));
        }
    }
    
    file.close();
    return true;
}

bool WatchList::exportToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    // Write CSV header
    file << "Address,Domain,Size,Display,Notes,Current,Previous,Frozen,Changes\n";
    
    // Write watches
    for (const auto& watch : watches_) {
        file << watch->getAddressString() << ",";
        file << watch->getDomain() << ",";
        file << getSizeString(watch->getSize()) << ",";
        file << getDisplayTypeString(watch->getDisplayType()) << ",";
        file << "\"" << watch->getNotes() << "\",";
        file << watch->getCurrentValueString() << ",";
        file << watch->getPreviousValueString() << ",";
        file << (watch->isFrozen() ? "Yes" : "No") << ",";
        file << watch->getChangeCount() << "\n";
    }
    
    file.close();
    return true;
}

Watch* WatchList::findWatch(uint32_t address, const std::string& domain) {
    auto it = std::find_if(watches_.begin(), watches_.end(),
                          [address, &domain](const std::unique_ptr<Watch>& watch) {
                              return watch->getAddress() == address && 
                                     watch->getDomain() == domain;
                          });
    
    return (it != watches_.end()) ? it->get() : nullptr;
}

const Watch* WatchList::findWatch(uint32_t address, const std::string& domain) const {
    auto it = std::find_if(watches_.begin(), watches_.end(),
                          [address, &domain](const std::unique_ptr<Watch>& watch) {
                              return watch->getAddress() == address && 
                                     watch->getDomain() == domain;
                          });
    
    return (it != watches_.end()) ? it->get() : nullptr;
}

size_t WatchList::getFrozenCount() const {
    return std::count_if(watches_.begin(), watches_.end(),
                        [](const std::unique_ptr<Watch>& watch) {
                            return watch->isFrozen();
                        });
}

size_t WatchList::getChangedCount() const {
    return std::count_if(watches_.begin(), watches_.end(),
                        [](const std::unique_ptr<Watch>& watch) {
                            return watch->hasValueChanged();
                        });
}

void WatchList::importFromSearchResults(const std::vector<LuaEngineRamSearch::SearchResult>& results, 
                                       const std::string& domain) {
    for (const auto& result : results) {
        WatchSize size = WatchSize::BYTE_1;
        
        // Use the size directly from the search result
        size = result.size;
        
        addWatch(result.address, domain, size);
    }
}

std::string WatchList::getSizeString(WatchSize size) const {
    switch (size) {
        case WatchSize::BYTE_1: return "Byte";
        case WatchSize::BYTE_2: return "Word";
        case WatchSize::BYTE_4: return "DWord";
        case WatchSize::BYTE_8: return "Double";
        default: return "Unknown";
    }
}

std::string WatchList::getDisplayTypeString(WatchDisplayType type) const {
    switch (type) {
        case WatchDisplayType::HEXADECIMAL: return "Hex";
        case WatchDisplayType::DECIMAL: return "Dec";
        case WatchDisplayType::BINARY: return "Bin";
        case WatchDisplayType::SIGNED_DECIMAL: return "Signed";
        case WatchDisplayType::FLOAT: return "Float";
        case WatchDisplayType::DOUBLE: return "Double";
        case WatchDisplayType::STRING_SJIS: return "SJIS";
        case WatchDisplayType::STRING_UTF8: return "UTF8";
        case WatchDisplayType::STRING_ASCII: return "ASCII";
        default: return "Unknown";
    }
}

WatchSize WatchList::stringToSize(const std::string& str) const {
    if (str == "Byte") return WatchSize::BYTE_1;
    if (str == "Word") return WatchSize::BYTE_2;
    if (str == "DWord") return WatchSize::BYTE_4;
    if (str == "Float") return WatchSize::BYTE_4;
    if (str == "Double") return WatchSize::BYTE_8;
    if (str == "String") return WatchSize::BYTE_1;
    return WatchSize::BYTE_1;
}

WatchDisplayType WatchList::stringToDisplayType(const std::string& str) const {
    if (str == "Hex") return WatchDisplayType::HEXADECIMAL;
    if (str == "Dec") return WatchDisplayType::DECIMAL;
    if (str == "Bin") return WatchDisplayType::BINARY;
    if (str == "Signed") return WatchDisplayType::SIGNED_DECIMAL;
    if (str == "Float") return WatchDisplayType::FLOAT;
    if (str == "Double") return WatchDisplayType::DOUBLE;
    if (str == "SJIS") return WatchDisplayType::STRING_SJIS;
    if (str == "UTF8") return WatchDisplayType::STRING_UTF8;
    if (str == "ASCII") return WatchDisplayType::STRING_ASCII;
    return WatchDisplayType::HEXADECIMAL;
}

void WatchList::freezeWatch(size_t watch_id, uint64_t value) {
    if (watch_id < watches_.size()) {
        watches_[watch_id]->freeze(value);
    }
}

void WatchList::setMaxWatches(int max_watches) {
    // Limit the number of watches for performance
    if (max_watches > 0 && watches_.size() > static_cast<size_t>(max_watches)) {
        watches_.resize(max_watches);
        cache_dirty_ = true;
    }
}

void WatchList::setUpdateInterval(int interval) {
    update_interval_ms_ = interval;
}

void WatchList::onMemoryChanged(uint32_t address, uint64_t old_value, uint64_t new_value) {
    // Update any watches that match this address
    for (auto& watch : watches_) {
        if (watch->getAddress() == address) {
            watch->updateValue();
        }
    }
}

//=============================================================================
// Private Helper Methods
//=============================================================================

uint64_t WatchList::getCachedValue(uint32_t address, WatchSize size) const {
    uint64_t value = 0;
    
    switch (size) {
        case WatchSize::BYTE_1:
            if (address_cache_.find(address) != address_cache_.end()) {
                value = address_cache_.at(address);
            }
            break;
            
        case WatchSize::BYTE_2:
            if (address_cache_.find(address) != address_cache_.end() &&
                address_cache_.find(address + 1) != address_cache_.end()) {
                value = address_cache_.at(address) | 
                       (static_cast<uint64_t>(address_cache_.at(address + 1)) << 8);
            }
            break;
            
        case WatchSize::BYTE_4:
            for (int i = 0; i < 4; ++i) {
                if (address_cache_.find(address + i) != address_cache_.end()) {
                    value |= (static_cast<uint64_t>(address_cache_.at(address + i)) << (i * 8));
                }
            }
            break;
            
        case WatchSize::BYTE_8:
            for (int i = 0; i < 8; ++i) {
                if (address_cache_.find(address + i) != address_cache_.end()) {
                    value |= (static_cast<uint64_t>(address_cache_.at(address + i)) << (i * 8));
                }
            }
            break;
            
            if (address_cache_.find(address) != address_cache_.end()) {
                value = address_cache_.at(address);
            }
            break;
    }
    
    return value;
}

} // namespace LuaEngineWatchList