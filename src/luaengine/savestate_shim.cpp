#include <cstdint>

// Stub implementations for configurations where the core save-state
// helpers are not linked in. These satisfy LuaEngine references while
// keeping behaviour benign (no-op or default state).

extern "C" {

void SaveGameState(bool /*pressed*/) {}
void LoadGameState(bool /*pressed*/) {}
std::uint64_t GetGameState() { return 0; }
void SetGameState(int /*slot*/) {}

} // extern "C"
