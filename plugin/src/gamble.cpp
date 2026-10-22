// Gamble window offers (Median XL rules, gamble_logic.h): the loop bound of the offer generator 0x541880 goes from 14 to 32,
// as Median's D2Sigma does at 6fcde2a0 + 0x...615. The generator builds the list on every gamble request (client packet 0x38
// kind 2), so the player's list at player data +0x108 is replaced each time the window opens.
#include "gamble.h"

#include "gamble_logic.h"

#include <cstdint>

auto InstallGamble(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool ok = context->PatchBytes(gamble::LoopBoundRva, gamble::LoopBoundOriginal, sizeof(gamble::LoopBoundOriginal), gamble::LoopBoundPatched,
	                                    sizeof(gamble::LoopBoundPatched));
	gamble::FormatReport(report, reportSize, ok);
	return ok;
}
