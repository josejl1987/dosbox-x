#include "ram_search_window.h"
#include "imgui.h"

namespace LuaEngineRamSearch {

RamSearchWindow::RamSearchWindow() 
    : selected_operator_(SearchOperator::EQUAL),
      selected_compare_type_(CompareType::PREVIOUS_VALUE),
      search_value_(0),
      selected_size_(WatchSize::BYTE_4),
      selected_display_type_(WatchDisplayType::UNSIGNED),
      show_window_(true),
      results_page_(0),
      results_per_page_(1000) {
    
    // Initialize input buffer
    search_input_text_[0] = '\0';
}

RamSearchWindow::~RamSearchWindow() {
    // Cleanup if needed
}

void RamSearchWindow::initialize(LuaEngineMemoryDomains::MemoryDomainManager* memory_mgr) {
    engine_.initialize(memory_mgr);
}

void RamSearchWindow::render() {
    if (!show_window_) return;
    
    if (ImGui::Begin("RAM Search", &show_window_)) {
        ImGui::Text("RAM Search Window");
        ImGui::Text("Results: %zu", engine_.getResultCount());
        
        // Basic search controls
        ImGui::InputText("Search Value", search_input_text_, sizeof(search_input_text_));
        
        if (ImGui::Button("New Search")) {
            engine_.startNewSearch();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Search")) {
            engine_.doSearch(selected_operator_, selected_compare_type_, search_value_);
        }
        
        // Results display
        ImGui::Separator();
        ImGui::Text("Results:");
        
        const auto& results = engine_.getResults();
        for (size_t i = 0; i < std::min(results.size(), results_per_page_); ++i) {
            const auto& result = results[i];
            ImGui::Text("0x%08X: %llu", result.address, result.current_value);
        }
    }
    ImGui::End();
}

void RamSearchWindow::show() {
    show_window_ = true;
}

void RamSearchWindow::hide() {
    show_window_ = false;
}

bool RamSearchWindow::isVisible() const {
    return show_window_;
}

void RamSearchWindow::setResultsPerPage(size_t results_per_page) {
    if (results_per_page > 0) {
        results_per_page_ = results_per_page;
    }
}

} // namespace LuaEngineRamSearch