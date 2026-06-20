#include "watch_window.h"
#include "debug_utils.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace LuaEngineWatchList {

//=============================================================================
// WatchWindow Implementation
//=============================================================================

WatchWindow::WatchWindow() 
    : show_window_(true), show_add_dialog_(false), show_edit_dialog_(false), 
      show_poke_dialog_(false), selected_watch_index_(-1), 
      size_combo_index_(0), display_type_combo_index_(0),
      poke_address_(0), poke_size_(LuaEngineRamSearch::WatchSize::BYTE_1) {
    
    // Initialize input buffers
    address_input_[0] = '\0';
    domain_input_[0] = '\0';
    notes_input_[0] = '\0';
    poke_value_input_[0] = '\0';
}

WatchWindow::~WatchWindow() {
}

void WatchWindow::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    watch_list_.initialize(memory_mgr);
}

void WatchWindow::render() {
    if (!show_window_) return;
    
    if (ImGui::Begin("Watch List", &show_window_, ImGuiWindowFlags_MenuBar)) {
        renderMenuBar();
        renderWatchTable();
    }
    ImGui::End();
    
    // Render dialogs
    if (show_add_dialog_) {
        renderAddDialog();
    }
    
    if (show_edit_dialog_) {
        renderEditDialog();
    }
    
    if (show_poke_dialog_) {
        renderPokeDialog();
    }
}

void WatchWindow::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Watch List", "Ctrl+N")) {
                clearWatches();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Watch List...", "Ctrl+O")) {
                // TODO: Open file dialog
                loadWatchList("watchlist.wch");
            }
            if (ImGui::MenuItem("Save Watch List", "Ctrl+S")) {
                // TODO: Save file dialog
                saveWatchList("watchlist.wch");
            }
            if (ImGui::MenuItem("Save Watch List As...", "Ctrl+Shift+S")) {
                // TODO: Save file dialog
                saveWatchList("watchlist.wch");
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Add Watch...", "Ctrl+A")) {
                openAddDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove All Watches", "Ctrl+D")) {
                clearWatches();
            }
            if (ImGui::MenuItem("Unfreeze All", "Ctrl+U")) {
                unfreezeAllWatches();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Update All", "F5")) {
                updateAllWatches();
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
}

void WatchWindow::renderWatchTable() {
    // Status bar
    ImGui::Text("Watches: %zu | Frozen: %zu | Changed: %zu", 
                getWatchCount(), getFrozenCount(), getChangedCount());
    
    ImGui::SameLine();
    if (ImGui::Button("Add Watch")) {
        openAddDialog();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Update All")) {
        updateAllWatches();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Apply Frozen")) {
        applyAllFrozenValues();
    }
    
    ImGui::Separator();
    
    // Watch table
    if (ImGui::BeginTable("WatchTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Previous", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Changes", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Frozen", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        
        const auto& watches = watch_list_.getWatches();
        for (size_t i = 0; i < watches.size(); ++i) {
            const auto& watch = watches[i];
            
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            
            // Address
            ImGui::TableNextColumn();
            ImGui::Text("%s", watch->getAddressString().c_str());
            
            // Domain
            ImGui::TableNextColumn();
            ImGui::Text("%s", watch->getDomain().c_str());
            
            // Current value
            ImGui::TableNextColumn();
            if (watch->hasValueChanged()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            }
            ImGui::Text("%s", watch->getCurrentValueString().c_str());
            if (watch->hasValueChanged()) {
                ImGui::PopStyleColor();
            }
            
            // Previous value
            ImGui::TableNextColumn();
            ImGui::Text("%s", watch->getPreviousValueString().c_str());
            
            // Change count
            ImGui::TableNextColumn();
            ImGui::Text("%u", watch->getChangeCount());
            
            // Frozen status
            ImGui::TableNextColumn();
            if (watch->isFrozen()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
                ImGui::Text("YES");
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("NO");
            }
            
            // Notes
            ImGui::TableNextColumn();
            ImGui::Text("%s", watch->getNotes().c_str());
            
            // Context menu
            if (ImGui::BeginPopupContextItem("WatchWindowContextMenu")) {
                if (ImGui::MenuItem("Edit...")) {
                    openEditDialog(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Remove")) {
                    onRemoveWatch(static_cast<int>(i));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Poke...")) {
                    onPokeWatch(static_cast<int>(i));
                }
                if (watch->isFrozen()) {
                    if (ImGui::MenuItem("Unfreeze")) {
                        onUnfreezeWatch(static_cast<int>(i));
                    }
                } else {
                    if (ImGui::MenuItem("Freeze")) {
                        onFreezeWatch(static_cast<int>(i));
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate")) {
                    onDuplicateWatch(static_cast<int>(i));
                }
                ImGui::EndPopup();
            }
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
    }
}

void WatchWindow::renderAddDialog() {
    if (ImGui::BeginPopupModal("Add Watch", &show_add_dialog_)) {
        ImGui::Text("Address:");
        ImGui::InputText("##Address", address_input_, sizeof(address_input_));
        
        ImGui::Text("Domain:");
        ImGui::InputText("##Domain", domain_input_, sizeof(domain_input_));
        
        ImGui::Text("Size:");
        const char* size_names[] = {"1 Byte", "2 Bytes", "4 Bytes", "8 Bytes"};
        ImGui::Combo("##Size", &size_combo_index_, size_names, IM_ARRAYSIZE(size_names));
        
        ImGui::Text("Display Type:");
        const char* display_names[] = {"Unsigned", "Signed", "Hex", "Binary", "Float"};
        ImGui::Combo("##DisplayType", &display_type_combo_index_, display_names, IM_ARRAYSIZE(display_names));
        
        ImGui::Text("Notes:");
        ImGui::InputTextMultiline("##Notes", notes_input_, sizeof(notes_input_), ImVec2(0, 60));
        
        ImGui::Separator();
        
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            uint32_t address = parseAddress(address_input_);
            std::string domain = domain_input_;
            auto size = static_cast<LuaEngineRamSearch::WatchSize>(size_combo_index_ + 1);
            auto display_type = static_cast<LuaEngineRamSearch::WatchDisplayType>(display_type_combo_index_);
            
            auto watch = std::make_unique<Watch>(address, domain, size);
            watch->setDisplayType(static_cast<LuaEngineWatchList::WatchDisplayType>(display_type));
            watch->setNotes(notes_input_);
            watch_list_.addWatch(std::move(watch));
            
            show_add_dialog_ = false;
            resetAddDialog();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_add_dialog_ = false;
            resetAddDialog();
        }
        
        ImGui::EndPopup();
    }
}

void WatchWindow::renderEditDialog() {
    if (ImGui::BeginPopupModal("Edit Watch", &show_edit_dialog_)) {
        if (selected_watch_index_ >= 0 && selected_watch_index_ < static_cast<int>(watch_list_.getWatchCount())) {
            auto* watch = watch_list_.getWatch(selected_watch_index_);
            
            ImGui::Text("Address:");
            ImGui::InputText("##Address", address_input_, sizeof(address_input_));
            
            ImGui::Text("Domain:");
            ImGui::InputText("##Domain", domain_input_, sizeof(domain_input_));
            
            ImGui::Text("Size:");
            const char* size_names[] = {"1 Byte", "2 Bytes", "4 Bytes", "8 Bytes"};
            ImGui::Combo("##Size", &size_combo_index_, size_names, IM_ARRAYSIZE(size_names));
            
            ImGui::Text("Display Type:");
            const char* display_names[] = {"Unsigned", "Signed", "Hex", "Binary", "Float"};
            ImGui::Combo("##DisplayType", &display_type_combo_index_, display_names, IM_ARRAYSIZE(display_names));
            
            ImGui::Text("Notes:");
            ImGui::InputTextMultiline("##Notes", notes_input_, sizeof(notes_input_), ImVec2(0, 60));
            
            ImGui::Separator();
            
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                uint32_t address = parseAddress(address_input_);
                std::string domain = domain_input_;
                auto size = static_cast<LuaEngineRamSearch::WatchSize>(size_combo_index_ + 1);
                auto display_type = static_cast<LuaEngineRamSearch::WatchDisplayType>(display_type_combo_index_);
                
                // Update the watch
                watch->setDomain(domain);
                watch->setSize(size);
                watch->setDisplayType(static_cast<LuaEngineWatchList::WatchDisplayType>(display_type));
                watch->setNotes(notes_input_);
                
                show_edit_dialog_ = false;
                resetEditDialog();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_edit_dialog_ = false;
                resetEditDialog();
            }
        }
        
        ImGui::EndPopup();
    }
}

void WatchWindow::renderPokeDialog() {
    if (ImGui::BeginPopupModal("Poke Value", &show_poke_dialog_)) {
        ImGui::Text("Address: %s", Watch(poke_address_, poke_domain_, poke_size_).getAddressString().c_str());
        ImGui::Text("Domain: %s", poke_domain_.c_str());
        
        ImGui::Text("New Value:");
        ImGui::InputText("##PokeValue", poke_value_input_, sizeof(poke_value_input_));
        
        ImGui::Separator();
        
        if (ImGui::Button("Poke", ImVec2(120, 0))) {
            uint64_t value = parseValue(poke_value_input_);
            
            // Find the watch and poke it
            auto* watch = watch_list_.findWatch(poke_address_, poke_domain_);
            if (watch) {
                watch->poke(value);
            }
            
            // Call callback if set
            if (onPokeValueCallback) {
                onPokeValueCallback(poke_address_, value, poke_size_, poke_domain_);
            }
            
            show_poke_dialog_ = false;
            resetPokeDialog();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_poke_dialog_ = false;
            resetPokeDialog();
        }
        
        ImGui::EndPopup();
    }
}

//=============================================================================
// Helper Methods
//=============================================================================

void WatchWindow::resetAddDialog() {
    address_input_[0] = '\0';
    strncpy(domain_input_, "DOS Conventional", sizeof(domain_input_) - 1);
    domain_input_[sizeof(domain_input_) - 1] = '\0';
    notes_input_[0] = '\0';
    size_combo_index_ = 0;
    display_type_combo_index_ = 0;
}

void WatchWindow::resetEditDialog() {
    address_input_[0] = '\0';
    domain_input_[0] = '\0';
    notes_input_[0] = '\0';
    size_combo_index_ = 0;
    display_type_combo_index_ = 0;
    selected_watch_index_ = -1;
}

void WatchWindow::resetPokeDialog() {
    poke_value_input_[0] = '\0';
    poke_address_ = 0;
    poke_domain_.clear();
    poke_size_ = LuaEngineRamSearch::WatchSize::BYTE_1;
}

uint32_t WatchWindow::parseAddress(const std::string& input) {
    return LuaEngineDebugUtils::parseAddress(input);
}

uint64_t WatchWindow::parseValue(const std::string& input) {
    return LuaEngineDebugUtils::parseValue(input);
}

void WatchWindow::openAddDialog() {
    resetAddDialog();
    show_add_dialog_ = true;
    ImGui::OpenPopup("Add Watch");
}

void WatchWindow::openEditDialog(int watch_index) {
    if (watch_index >= 0 && watch_index < static_cast<int>(watch_list_.getWatchCount())) {
        auto* watch = watch_list_.getWatch(watch_index);
        
        // Fill dialog with current values
        snprintf(address_input_, sizeof(address_input_), "0x%08X", watch->getAddress());
        strncpy(domain_input_, watch->getDomain().c_str(), sizeof(domain_input_) - 1);
        strncpy(notes_input_, watch->getNotes().c_str(), sizeof(notes_input_) - 1);
        size_combo_index_ = static_cast<int>(watch->getSize()) - 1;
        display_type_combo_index_ = static_cast<int>(watch->getDisplayType());
        
        selected_watch_index_ = watch_index;
        show_edit_dialog_ = true;
        ImGui::OpenPopup("Edit Watch");
    }
}

void WatchWindow::openPokeDialog(uint32_t address, const std::string& domain, LuaEngineRamSearch::WatchSize size) {
    poke_address_ = address;
    poke_domain_ = domain;
    poke_size_ = size;
    
    resetPokeDialog();
    show_poke_dialog_ = true;
    ImGui::OpenPopup("Poke Value");
}

//=============================================================================
// Context Menu Actions
//=============================================================================

void WatchWindow::onEditWatch(int index) {
    openEditDialog(index);
}

void WatchWindow::onRemoveWatch(int index) {
    watch_list_.removeWatch(index);
}

void WatchWindow::onPokeWatch(int index) {
    if (index >= 0 && index < static_cast<int>(watch_list_.getWatchCount())) {
        auto* watch = watch_list_.getWatch(index);
        openPokeDialog(watch->getAddress(), watch->getDomain(), watch->getSize());
    }
}

void WatchWindow::onFreezeWatch(int index) {
    if (index >= 0 && index < static_cast<int>(watch_list_.getWatchCount())) {
        auto* watch = watch_list_.getWatch(index);
        watch->freeze();
        
        if (onFreezeValueCallback) {
            onFreezeValueCallback(watch->getAddress(), watch->getCurrentValue(), 
                                 watch->getSize(), watch->getDomain());
        }
    }
}

void WatchWindow::onUnfreezeWatch(int index) {
    if (index >= 0 && index < static_cast<int>(watch_list_.getWatchCount())) {
        auto* watch = watch_list_.getWatch(index);
        watch->unfreeze();
        
        if (onUnfreezeValueCallback) {
            onUnfreezeValueCallback(watch->getAddress(), watch->getDomain());
        }
    }
}

void WatchWindow::onDuplicateWatch(int index) {
    if (index >= 0 && index < static_cast<int>(watch_list_.getWatchCount())) {
        auto* original = watch_list_.getWatch(index);
        auto duplicate = std::make_unique<Watch>(original->getAddress(), original->getDomain(), original->getSize());
        duplicate->setDisplayType(original->getDisplayType());
        duplicate->setNotes(original->getNotes() + " (Copy)");
        watch_list_.addWatch(std::move(duplicate));
    }
}

//=============================================================================
// Public Interface
//=============================================================================

void WatchWindow::show() {
    show_window_ = true;
}

void WatchWindow::hide() {
    show_window_ = false;
}

bool WatchWindow::isVisible() const {
    return show_window_;
}

void WatchWindow::addWatch(uint32_t address, const std::string& domain, LuaEngineRamSearch::WatchSize size) {
    watch_list_.addWatch(address, domain, size);
}

void WatchWindow::addWatch(const LuaEngineRamSearch::SearchResult& result, const std::string& domain) {
    auto watch = std::make_unique<Watch>(result.address, domain, result.size);
    watch->setInitialValue(result.initial_value);
    watch_list_.addWatch(std::move(watch));
}

void WatchWindow::removeWatch(uint32_t address, const std::string& domain) {
    watch_list_.removeWatch(address, domain);
}

void WatchWindow::clearWatches() {
    watch_list_.clearWatches();
}

bool WatchWindow::saveWatchList(const std::string& filename) {
    return watch_list_.saveToFile(filename);
}

bool WatchWindow::loadWatchList(const std::string& filename) {
    return watch_list_.loadFromFile(filename);
}

void WatchWindow::updateAllWatches() {
    watch_list_.updateAllValues();
}

void WatchWindow::applyAllFrozenValues() {
    watch_list_.applyAllFrozenValues();
}

void WatchWindow::unfreezeAllWatches() {
    watch_list_.unfreezeAll();
}

} // namespace LuaEngineWatchList