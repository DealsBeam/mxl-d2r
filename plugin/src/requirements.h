// D2Sigma's NPC service requirements (D2Requirements.cpp; docs/plugin-spec.md, "Compiled-in Sigma tables").
// A pure evaluator over the generated table mxl_requirements.h: it touches no game memory, the three game queries
// come in as callbacks, so the hooks that gate shop / gamble / hire / NPC actions only supply those.
#pragma once

#include <cstdint>

namespace mxl_requirements {

// Game queries. `user` is passed through untouched (the player unit, a test fixture...).
struct Queries {
	void* user = nullptr;
	// Quest `quest`, bit `bit` is set for the player. difficulty: -1 = the game's own, else 0 Normal, 1 Nightmare, 2 Hell.
	bool (*questBit)(void* user, int quest, int bit, int difficulty) = nullptr;
	// Player class (0 Amazon .. 6 Assassin).
	int (*playerClass)(void* user) = nullptr;
	// The player holds an item whose 4-character code is `code` (NUL-terminated, e.g. "box ").
	bool (*holdsItem)(void* user, const char* code) = nullptr;
};

// Is the service allowed? Row 0 and negative indexes ("none") allow. An index past the table, a condition type the
// table does not use (anything but OR/AND/quest bit/class/no-item/false), a missing callback or a reference cycle
// deny (D2Sigma bounds-checks the row fetch; the denial is this port's choice).
auto Evaluate(int row, const Queries& queries) noexcept -> bool;

}  // namespace mxl_requirements
