// Compile check for the generated Sigma tables (tools/sigma_data.py) that no hook reads yet:
// mxl_dungeons.h (dungeon lockout and entry requirements) and mxl_goblins.h (treasure goblins).
#include "mxl_dungeons.h"
#include "mxl_goblins.h"

static_assert(mxl_dungeons::EntryCount > 0, "mxl_dungeons.h is empty");
static_assert(mxl_goblins::ChanceCount > 0 && mxl_goblins::GoblinCount > 0, "mxl_goblins.h is empty");
