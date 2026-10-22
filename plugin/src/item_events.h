// Median's item event functions (ids 11 / 12 / 32 / 33 / 34) and the monster immunity of open wounds / crushing blow
// on D2R. Notes: docs/subsystems/items-xp.md section 5b.
#pragma once

#include <cstddef>
#include <cstdint>

namespace item_events {

// Repoints the handler slots of Median's functions 33 / 34 and gates 15 / 16 (see item_events.cpp). True when all four are in place.
bool Install(uint64_t exeBase, char* report, size_t reportSize) noexcept;
// Calls seen by the replaced handlers 33 / 34 and the arguments of the last 33 and both return values (11 qwords; harness verb `evtrace calls`).
void Counters(long* heal, long* cast, uint64_t* lastArgs9) noexcept;

// Diagnostics for the harness verb `evtrace`: D2RLoader's unit-event entry points are 10-byte `jmp [slot]` thunks into
// D2RCore.dll; the trace swaps the slot's pointer for a logger that calls the original and records the arguments.
bool TraceStart(uint64_t exeBase) noexcept;
void TraceStop() noexcept;
// Writes one line per recorded call (oldest first, at most `max` of the newest) through `emit`; `slot` -1 = all.
void TraceDump(int slot, int max, void (*emit)(const char* line)) noexcept;
void TraceClear() noexcept;

// Write watch (hardware breakpoint on every thread): logs the code that writes `len` (1/2/4/8) bytes at `address`, with the
// caller chain as exe RVAs. `slot` 0..3. Returns false when a thread could not be set up.
bool WatchSet(int slot, uint64_t address, int len, uint64_t exeBase, bool readToo = false) noexcept;
void WatchClear() noexcept;
bool WatchSelfTest() noexcept;
void WatchDump(void (*emit)(const char* line)) noexcept;

}  // namespace item_events
