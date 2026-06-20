#include "cheat_window.h"
#include "debugger_session.h"
#include "debug_utils.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace LuaEngineCheatEngine {

//=============================================================================
// CheatWindow Implementation
//=============================================================================

CheatWindow::CheatWindow(LuaEngineDebugTools::DebuggerSession* session) 
    : cheat_engine_(session ? session->cheats() : nullptr),
      show_window_(false), show_add_dialog_(false), show_edit_dialog_(false),
      show_code_dialog_(false), show_pointer_dialog_(false), selected_cheat_index_(-1),
      size_combo_index_(0), type_combo_index_(0), trigger_combo_index_(0) {
    
    // Initialize input buffers
    name_input_[0] = '\0';
    description_input_[0] = '\0';
    address_input_[0] = '\0';
    domain_input_[0] = '\0';
    value_input_[0] = '\0';
    condition_input_[0] = '\0';
    delta_input_[0] = '\0';
    range_size_input_[0] = '\0';
    original_code_input_[0] = '\0';
    modified_code_input_[0] = '\0';
    pointer_path_input_[0] = '\0';
}

CheatWindow::~CheatWindow() {
}

void CheatWindow::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    // ponytail: cheat_engine_ is now owned by DebuggerSession; this method is a no-op
    // for backward compat.
    (void)memory_mgr;
}

void CheatWindow::render() {
    if (!show_window_) return;
    if (!cheat_engine_) return; // null-guard: session not initialized
    
    if (ImGui::Begin("Cheat Engine", &show_window_, ImGuiWindowFlags_MenuBar)) {
        renderMenuBar();
        renderToolbar();
        renderCheatTable();
    }
    ImGui::End();
    
    // Render dialogs
    if (show_add_dialog_) {
        renderAddDialog();
    }
    
    if (show_edit_dialog_) {
        renderEditDialog();
    }
    
    if (show_code_dialog_) {
        renderCodeDialog();
    }
    
    if (show_pointer_dialog_) {
        renderPointerDialog();
    }
}

void CheatWindow::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Cheat List", "Ctrl+N")) {
                clearCheats();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Cheat List...", "Ctrl+O")) {
                // TODO: Open file dialog
                loadCheatFile("cheats.cht");
            }
            if (ImGui::MenuItem("Save Cheat List", "Ctrl+S")) {
                // TODO: Save file dialog
                saveCheatFile("cheats.cht");
            }
            if (ImGui::MenuItem("Save Cheat List As...", "Ctrl+Shift+S")) {
                // TODO: Save file dialog
                saveCheatFile("cheats.cht");
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Add Cheat...", "Ctrl+A")) {
                openAddDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Enable All Cheats", "Ctrl+E")) {
                enableAllCheats(true);
            }
            if (ImGui::MenuItem("Disable All Cheats", "Ctrl+D")) {
                enableAllCheats(false);
            }
            if (ImGui::MenuItem("Reset All Cheats", "Ctrl+R")) {
                resetAllCheats();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear All Cheats", "Ctrl+L")) {
                clearCheats();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Code Cheat Builder...")) {
                openCodeDialog();
            }
            if (ImGui::MenuItem("Pointer Cheat Builder...")) {
                openPointerDialog();
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
}

void CheatWindow::renderToolbar() {
    // Engine toggle
    bool engine_enabled = cheat_engine_->isEnabled();
    if (ImGui::Checkbox("Engine Enabled", &engine_enabled)) {
        cheat_engine_->setEnabled(engine_enabled);
    }
    
    ImGui::SameLine();
    
    // Add cheat button
    if (ImGui::Button("Add Cheat")) {
        openAddDialog();
    }
    
    ImGui::SameLine();
    
    // Apply cheats button
    if (ImGui::Button("Apply Now")) {
        applyCheats();
    }
    
    ImGui::SameLine();
    
    // Reset all button
    if (ImGui::Button("Reset All")) {
        resetAllCheats();
    }
    
    // Status information
    ImGui::Text("Cheats: %zu | Enabled: %zu | Frame: %u | Applied: %u", 
                getCheatCount(), getEnabledCheatCount(), 
                cheat_engine_->getFrameCount(), cheat_engine_->getCheatsAppliedThisFrame());
}

void CheatWindow::renderCheatTable() {
    // Search functionality
    static ImGuiTextFilter cheat_filter;
    cheat_filter.Draw("Search Cheats");
    
    if (ImGui::BeginTable("CheatTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
        
        const auto& cheats = cheat_engine_->getCheats();
        size_t visible_count = 0;
        
        for (size_t i = 0; i < cheats.size(); ++i) {
            const auto& cheat = cheats[i];
            
            // Apply search filter
            if (!cheat_filter.PassFilter(cheat->getName().c_str())) {
                continue;
            }
            
            visible_count++;
            
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            
            // Enabled checkbox
            ImGui::TableNextColumn();
            bool enabled = cheat->isEnabled();
            if (ImGui::Checkbox("##Enabled", &enabled)) {
                cheat_engine_->enableCheat(cheat->getId(), enabled);
            }
            
            // Name (with search highlighting)
            ImGui::TableNextColumn();
            if (cheat_filter.IsActive()) {
                // Highlight matching text
                const char* name_end = cheat->getName().c_str() + cheat->getName().length();
                const char* name_start = cheat->getName().c_str();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", cheat->getName().c_str());
                
                // Draw search highlight background
                if (cheat_filter.PassFilter(cheat->getName().c_str())) {
                    ImVec2 text_pos = ImGui::GetCursorScreenPos();
                    ImVec2 text_size = ImGui::CalcTextSize(cheat->getName().c_str());
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    draw_list->AddRectFilled(
                        ImVec2(text_pos.x - 2, text_pos.y - 1),
                        ImVec2(text_pos.x + text_size.x + 2, text_pos.y + text_size.y + 1),
                        IM_COL32(255, 255, 0, 80),  // Yellow highlight
                        3.0f,  // Corner radius
                        ImDrawFlags_RoundCornersAll
                    );
                }
            } else {
                ImGui::Text("%s", cheat->getName().c_str());
            }
            
            // Address
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", cheat->getAddress());
            
            // Value
            ImGui::TableNextColumn();
            ImGui::Text("%s", cheat->getValueString().c_str());
            
            // Type
            ImGui::TableNextColumn();
            ImGui::Text("%s", cheat->getTypeString().c_str());
            
            // Hit count
            ImGui::TableNextColumn();
            ImGui::Text("%u", cheat->getHitCount());
            
            // Status
            ImGui::TableNextColumn();
            ImGui::Text("%s", cheat->getStatusString().c_str());
            
            // Context menu
            if (ImGui::BeginPopupContextItem("CheatWindowContextMenu")) {
                if (ImGui::MenuItem("Edit...")) {
                    openEditDialog(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Remove")) {
                    onRemoveCheat(static_cast<int>(i));
                }
                ImGui::Separator();
                if (cheat->isEnabled()) {
                    if (ImGui::MenuItem("Disable")) {
                        onToggleCheat(static_cast<int>(i));
                    }
                } else {
                    if (ImGui::MenuItem("Enable")) {
                        onToggleCheat(static_cast<int>(i));
                    }
                }
                if (ImGui::MenuItem("Reset")) {
                    onResetCheat(static_cast<int>(i));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate")) {
                    onDuplicateCheat(static_cast<int>(i));
                }
                ImGui::EndPopup();
            }
            
            ImGui::PopID();
        }
        
        ImGui::EndTable();
        
        // Show search results summary
        if (cheat_filter.IsActive()) {
            ImGui::Separator();
            ImGui::Text("Showing %zu of %zu cheats (filtered: %zu)",
                       visible_count, cheats.size(), cheats.size() - visible_count);
        } else {
            ImGui::Separator();
            ImGui::Text("Total cheats: %zu", cheats.size());
        }
    }
}

void CheatWindow::renderAddDialog() {
    if (ImGui::BeginPopupModal("Add Cheat", &show_add_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Name:");
        ImGui::InputText("##Name", name_input_, sizeof(name_input_));
        
        ImGui::Text("Description:");
        ImGui::InputTextMultiline("##Description", description_input_, sizeof(description_input_), ImVec2(0, 60));
        
        ImGui::Text("Address:");
        ImGui::InputText("##Address", address_input_, sizeof(address_input_));
        
        ImGui::Text("Domain:");
        ImGui::InputText("##Domain", domain_input_, sizeof(domain_input_));
        
        ImGui::Text("Size:");
        const char* size_names[] = {"1 Byte", "2 Bytes", "4 Bytes", "8 Bytes"};
        ImGui::Combo("##Size", &size_combo_index_, size_names, IM_ARRAYSIZE(size_names));
        
        ImGui::Text("Type:");
        const char* type_names[] = {"Static Value", "Pointer Value", "Code Modification", "Conditional", "Increase/Decrease", "Freeze Range"};
        ImGui::Combo("##Type", &type_combo_index_, type_names, IM_ARRAYSIZE(type_names));
        
        ImGui::Text("Trigger:");
        const char* trigger_names[] = {"Always", "On Load", "On Frame", "On Condition", "On Hotkey"};
        ImGui::Combo("##Trigger", &trigger_combo_index_, trigger_names, IM_ARRAYSIZE(trigger_names));
        
        // Type-specific inputs
        CheatType selected_type = static_cast<CheatType>(type_combo_index_);
        
        switch (selected_type) {
            case CheatType::STATIC_VALUE:
            case CheatType::POINTER_VALUE:
                ImGui::Text("Value:");
                ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                break;
            case CheatType::CONDITIONAL:
                ImGui::Text("Value:");
                ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                ImGui::Text("Condition:");
                ImGui::InputText("##Condition", condition_input_, sizeof(condition_input_));
                break;
            case CheatType::INCREASE_DECREASE:
                ImGui::Text("Delta:");
                ImGui::InputText("##Delta", delta_input_, sizeof(delta_input_));
                break;
            case CheatType::FREEZE_RANGE:
                ImGui::Text("Value:");
                ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                ImGui::Text("Range Size:");
                ImGui::InputText("##RangeSize", range_size_input_, sizeof(range_size_input_));
                break;
            case CheatType::CODE_MODIFICATION:
                ImGui::Text("See Code Cheat Builder for complex modifications");
                break;
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            uint32_t address = parseAddress(address_input_);
            std::string domain = domain_input_;
            auto size = static_cast<LuaEngineRamSearch::WatchSize>(size_combo_index_ + 1);
            
            uint32_t id = cheat_engine_->addCheat(name_input_, address, domain, size);
            auto* cheat = cheat_engine_->findCheat(id);
            if (cheat) {
                updateCheatFromDialog(cheat);
            }
            
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

void CheatWindow::renderEditDialog() {
    if (ImGui::BeginPopupModal("Edit Cheat", &show_edit_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_cheat_index_ >= 0 && selected_cheat_index_ < static_cast<int>(cheat_engine_->getCheatCount())) {
            auto* cheat = cheat_engine_->getCheat(selected_cheat_index_);
            
            ImGui::Text("Name:");
            ImGui::InputText("##Name", name_input_, sizeof(name_input_));
            
            ImGui::Text("Description:");
            ImGui::InputTextMultiline("##Description", description_input_, sizeof(description_input_), ImVec2(0, 60));
            
            ImGui::Text("Address:");
            ImGui::InputText("##Address", address_input_, sizeof(address_input_));
            
            ImGui::Text("Domain:");
            ImGui::InputText("##Domain", domain_input_, sizeof(domain_input_));
            
            ImGui::Text("Size:");
            const char* size_names[] = {"1 Byte", "2 Bytes", "4 Bytes", "8 Bytes"};
            ImGui::Combo("##Size", &size_combo_index_, size_names, IM_ARRAYSIZE(size_names));
            
            ImGui::Text("Type:");
            const char* type_names[] = {"Static Value", "Pointer Value", "Code Modification", "Conditional", "Increase/Decrease", "Freeze Range"};
            ImGui::Combo("##Type", &type_combo_index_, type_names, IM_ARRAYSIZE(type_names));
            
            ImGui::Text("Trigger:");
            const char* trigger_names[] = {"Always", "On Load", "On Frame", "On Condition", "On Hotkey"};
            ImGui::Combo("##Trigger", &trigger_combo_index_, trigger_names, IM_ARRAYSIZE(trigger_names));
            
            // Type-specific inputs (similar to add dialog)
            CheatType selected_type = static_cast<CheatType>(type_combo_index_);
            
            switch (selected_type) {
                case CheatType::STATIC_VALUE:
                case CheatType::POINTER_VALUE:
                    ImGui::Text("Value:");
                    ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                    break;
                case CheatType::CONDITIONAL:
                    ImGui::Text("Value:");
                    ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                    ImGui::Text("Condition:");
                    ImGui::InputText("##Condition", condition_input_, sizeof(condition_input_));
                    break;
                case CheatType::INCREASE_DECREASE:
                    ImGui::Text("Delta:");
                    ImGui::InputText("##Delta", delta_input_, sizeof(delta_input_));
                    break;
                case CheatType::FREEZE_RANGE:
                    ImGui::Text("Value:");
                    ImGui::InputText("##Value", value_input_, sizeof(value_input_));
                    ImGui::Text("Range Size:");
                    ImGui::InputText("##RangeSize", range_size_input_, sizeof(range_size_input_));
                    break;
                case CheatType::CODE_MODIFICATION:
                    ImGui::Text("See Code Cheat Builder for complex modifications");
                    break;
            }
            
            ImGui::Separator();
            
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                updateCheatFromDialog(cheat);
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

void CheatWindow::renderCodeDialog() {
    if (ImGui::BeginPopupModal("Code Cheat Builder", &show_code_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Code Cheat Builder");
        ImGui::Text("Create cheats that modify assembly code");
        
        ImGui::Text("Name:");
        ImGui::InputText("##CodeName", name_input_, sizeof(name_input_));
        
        ImGui::Text("Address:");
        ImGui::InputText("##CodeAddress", address_input_, sizeof(address_input_));
        
        ImGui::Text("Domain:");
        ImGui::InputText("##CodeDomain", domain_input_, sizeof(domain_input_));
        
        ImGui::Text("Original Code (hex):");
        ImGui::InputTextMultiline("##OriginalCode", original_code_input_, sizeof(original_code_input_), ImVec2(0, 60));
        
        ImGui::Text("Modified Code (hex):");
        ImGui::InputTextMultiline("##ModifiedCode", modified_code_input_, sizeof(modified_code_input_), ImVec2(0, 60));
        
        ImGui::Separator();
        
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            uint32_t address = parseAddress(address_input_);
            std::string domain = domain_input_;
            auto original = parseHexString(original_code_input_);
            auto modified = parseHexString(modified_code_input_);
            
            createCodeCheat(name_input_, address, domain, original, modified);
            
            show_code_dialog_ = false;
            resetCodeDialog();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_code_dialog_ = false;
            resetCodeDialog();
        }
        
        ImGui::EndPopup();
    }
}

void CheatWindow::renderPointerDialog() {
    if (ImGui::BeginPopupModal("Pointer Cheat Builder", &show_pointer_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Pointer Cheat Builder");
        ImGui::Text("Create cheats that follow pointer chains");
        
        ImGui::Text("Name:");
        ImGui::InputText("##PointerName", name_input_, sizeof(name_input_));
        
        ImGui::Text("Base Address:");
        ImGui::InputText("##PointerAddress", address_input_, sizeof(address_input_));
        
        ImGui::Text("Domain:");
        ImGui::InputText("##PointerDomain", domain_input_, sizeof(domain_input_));
        
        ImGui::Text("Pointer Path (offsets separated by commas):");
        ImGui::InputText("##PointerPath", pointer_path_input_, sizeof(pointer_path_input_));
        
        ImGui::Text("Value:");
        ImGui::InputText("##PointerValue", value_input_, sizeof(value_input_));
        
        ImGui::Text("Size:");
        const char* size_names[] = {"1 Byte", "2 Bytes", "4 Bytes", "8 Bytes"};
        ImGui::Combo("##PointerSize", &size_combo_index_, size_names, IM_ARRAYSIZE(size_names));
        
        ImGui::Separator();
        
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            uint32_t address = parseAddress(address_input_);
            std::string domain = domain_input_;
            auto pointer_path = parsePointerPath(pointer_path_input_);
            auto size = static_cast<LuaEngineRamSearch::WatchSize>(size_combo_index_ + 1);
            uint64_t value = parseValue(value_input_);
            
            createPointerCheat(name_input_, pointer_path, domain, size, value);
            
            show_pointer_dialog_ = false;
            resetPointerDialog();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            show_pointer_dialog_ = false;
            resetPointerDialog();
        }
        
        ImGui::EndPopup();
    }
}

//=============================================================================
// Helper Methods
//=============================================================================

void CheatWindow::resetAddDialog() {
    name_input_[0] = '\0';
    description_input_[0] = '\0';
    address_input_[0] = '\0';
    strncpy(domain_input_, "DOS Conventional", sizeof(domain_input_) - 1);
    domain_input_[sizeof(domain_input_) - 1] = '\0';
    value_input_[0] = '\0';
    condition_input_[0] = '\0';
    delta_input_[0] = '\0';
    range_size_input_[0] = '\0';
    size_combo_index_ = 0;
    type_combo_index_ = 0;
    trigger_combo_index_ = 0;
}

void CheatWindow::resetEditDialog() {
    resetAddDialog();
    selected_cheat_index_ = -1;
}

void CheatWindow::resetCodeDialog() {
    name_input_[0] = '\0';
    address_input_[0] = '\0';
    strncpy(domain_input_, "DOS Conventional", sizeof(domain_input_) - 1);
    domain_input_[sizeof(domain_input_) - 1] = '\0';
    original_code_input_[0] = '\0';
    modified_code_input_[0] = '\0';
}

void CheatWindow::resetPointerDialog() {
    name_input_[0] = '\0';
    address_input_[0] = '\0';
    strncpy(domain_input_, "DOS Conventional", sizeof(domain_input_) - 1);
    domain_input_[sizeof(domain_input_) - 1] = '\0';
    pointer_path_input_[0] = '\0';
    value_input_[0] = '\0';
    size_combo_index_ = 0;
}

void CheatWindow::openAddDialog() {
    resetAddDialog();
    show_add_dialog_ = true;
    ImGui::OpenPopup("Add Cheat");
}

void CheatWindow::openEditDialog(int cheat_index) {
    if (cheat_index >= 0 && cheat_index < static_cast<int>(cheat_engine_->getCheatCount())) {
        auto* cheat = cheat_engine_->getCheat(cheat_index);
        fillDialogFromCheat(cheat);
        selected_cheat_index_ = cheat_index;
        show_edit_dialog_ = true;
        ImGui::OpenPopup("Edit Cheat");
    }
}

void CheatWindow::openCodeDialog() {
    resetCodeDialog();
    show_code_dialog_ = true;
    ImGui::OpenPopup("Code Cheat Builder");
}

void CheatWindow::openPointerDialog() {
    resetPointerDialog();
    show_pointer_dialog_ = true;
    ImGui::OpenPopup("Pointer Cheat Builder");
}

void CheatWindow::onEditCheat(int index) {
    openEditDialog(index);
}

void CheatWindow::onRemoveCheat(int index) {
    if (index >= 0 && index < static_cast<int>(cheat_engine_->getCheatCount())) {
        auto* cheat = cheat_engine_->getCheat(index);
        cheat_engine_->removeCheat(cheat->getId());
    }
}

void CheatWindow::onToggleCheat(int index) {
    if (index >= 0 && index < static_cast<int>(cheat_engine_->getCheatCount())) {
        auto* cheat = cheat_engine_->getCheat(index);
        cheat_engine_->toggleCheat(cheat->getId());
    }
}

void CheatWindow::onDuplicateCheat(int index) {
    if (index >= 0 && index < static_cast<int>(cheat_engine_->getCheatCount())) {
        auto* original = cheat_engine_->getCheat(index);
        std::string new_name = original->getName() + " (Copy)";
        
        uint32_t id = cheat_engine_->addCheat(new_name, original->getAddress(), 
                                           original->getDomain(), original->getSize());
        auto* duplicate = cheat_engine_->findCheat(id);
        if (duplicate) {
            duplicate->setDescription(original->getDescription());
            duplicate->setType(original->getType());
            duplicate->setTrigger(original->getTrigger());
            duplicate->setStaticValue(original->getValue());
        }
    }
}

void CheatWindow::onResetCheat(int index) {
    if (index >= 0 && index < static_cast<int>(cheat_engine_->getCheatCount())) {
        auto* cheat = cheat_engine_->getCheat(index);
        cheat->reset();
    }
}

uint32_t CheatWindow::parseAddress(const std::string& input) {
    return LuaEngineDebugUtils::parseAddress(input);
}

uint64_t CheatWindow::parseValue(const std::string& input) {
    return LuaEngineDebugUtils::parseValue(input);
}

std::vector<uint8_t> CheatWindow::parseHexString(const std::string& input) {
    return LuaEngineDebugUtils::parseHexString(input);
}

std::vector<uint32_t> CheatWindow::parsePointerPath(const std::string& input) {
    return LuaEngineDebugUtils::parsePointerPath(input);
}

void CheatWindow::updateCheatFromDialog(Cheat* cheat) {
    if (!cheat) return;
    
    cheat->setName(name_input_);
    cheat->setDescription(description_input_);
    cheat->setType(static_cast<CheatType>(type_combo_index_));
    cheat->setTrigger(static_cast<CheatTrigger>(trigger_combo_index_));
    
    CheatType type = static_cast<CheatType>(type_combo_index_);
    
    switch (type) {
        case CheatType::STATIC_VALUE:
            cheat->setStaticValue(parseValue(value_input_));
            break;
        case CheatType::CONDITIONAL:
            cheat->setStaticValue(parseValue(value_input_));
            cheat->setCondition(condition_input_);
            break;
        case CheatType::INCREASE_DECREASE:
            cheat->setDeltaValue(static_cast<int64_t>(parseValue(delta_input_)));
            break;
        case CheatType::FREEZE_RANGE:
            cheat->setStaticValue(parseValue(value_input_));
            cheat->setRangeSize(static_cast<uint32_t>(parseValue(range_size_input_)));
            break;
        default:
            break;
    }
}

void CheatWindow::fillDialogFromCheat(const Cheat* cheat) {
    if (!cheat) return;
    
    strncpy(name_input_, cheat->getName().c_str(), sizeof(name_input_) - 1);
    strncpy(description_input_, cheat->getDescription().c_str(), sizeof(description_input_) - 1);
    snprintf(address_input_, sizeof(address_input_), "0x%08X", cheat->getAddress());
    strncpy(domain_input_, cheat->getDomain().c_str(), sizeof(domain_input_) - 1);
    snprintf(value_input_, sizeof(value_input_), "%llu", cheat->getValue());
    strncpy(condition_input_, cheat->getCondition().c_str(), sizeof(condition_input_) - 1);
    snprintf(delta_input_, sizeof(delta_input_), "%lld", cheat->getDeltaValue());
    snprintf(range_size_input_, sizeof(range_size_input_), "%u", cheat->getRangeSize());
    
    size_combo_index_ = static_cast<int>(cheat->getSize()) - 1;
    type_combo_index_ = static_cast<int>(cheat->getType());
    trigger_combo_index_ = static_cast<int>(cheat->getTrigger());
}

//=============================================================================
// Public Interface
//=============================================================================

void CheatWindow::show() {
    show_window_ = true;
}

void CheatWindow::hide() {
    show_window_ = false;
}

bool CheatWindow::isVisible() const {
    return show_window_;
}

uint32_t CheatWindow::addCheat(const std::string& name, uint32_t address, const std::string& domain, 
                              LuaEngineRamSearch::WatchSize size) {
    if (!cheat_engine_) return 0;
    return cheat_engine_->addCheat(name, address, domain, size);
}

void CheatWindow::removeCheat(uint32_t id) {
    if (!cheat_engine_) return;
    cheat_engine_->removeCheat(id);
}

void CheatWindow::toggleCheat(uint32_t id) {
    if (!cheat_engine_) return;
    cheat_engine_->toggleCheat(id);
}

void CheatWindow::clearCheats() {
    if (!cheat_engine_) return;
    cheat_engine_->removeAllCheats();
}

bool CheatWindow::saveCheatFile(const std::string& filename) {
    if (!cheat_engine_) return false;
    return cheat_engine_->saveCheatFile(filename);
}

bool CheatWindow::loadCheatFile(const std::string& filename) {
    if (!cheat_engine_) return false;
    return cheat_engine_->loadCheatFile(filename);
}

void CheatWindow::enableAllCheats(bool enabled) {
    if (!cheat_engine_) return;
    cheat_engine_->enableAllCheats(enabled);
}

void CheatWindow::resetAllCheats() {
    if (!cheat_engine_) return;
    cheat_engine_->resetAllCheats();
}

void CheatWindow::applyCheats() {
    if (!cheat_engine_) return;
    cheat_engine_->applyCheats();
}

void CheatWindow::freezeAddress(uint32_t address, uint64_t value, const std::string& domain, 
                              LuaEngineRamSearch::WatchSize size) {
    if (!cheat_engine_) return;
    cheat_engine_->freezeAddress(address, value, domain, size);
    
    if (onFreezeAddressCallback) {
        onFreezeAddressCallback(address, value, size, domain);
    }
}

void CheatWindow::unfreezeAddress(uint32_t address, const std::string& domain) {
    if (!cheat_engine_) return;
    cheat_engine_->unfreezeAddress(address, domain);
    
    if (onUnfreezeAddressCallback) {
        onUnfreezeAddressCallback(address, domain);
    }
}

uint32_t CheatWindow::createStaticCheat(const std::string& name, uint32_t address, const std::string& domain,
                                       LuaEngineRamSearch::WatchSize size, uint64_t value) {
    if (!cheat_engine_) return 0;
    return cheat_engine_->createStaticCheat(name, address, domain, size, value);
}

uint32_t CheatWindow::createPointerCheat(const std::string& name, const std::vector<uint32_t>& pointer_path,
                                        const std::string& domain, LuaEngineRamSearch::WatchSize size, uint64_t value) {
    if (!cheat_engine_) return 0;
    return cheat_engine_->createPointerCheat(name, pointer_path, domain, size, value);
}

uint32_t CheatWindow::createCodeCheat(const std::string& name, uint32_t address, const std::string& domain,
                                     const std::vector<uint8_t>& original, const std::vector<uint8_t>& modified) {
    if (!cheat_engine_) return 0;
    return cheat_engine_->createCodeCheat(name, address, domain, original, modified);
}

} // namespace LuaEngineCheatEngine