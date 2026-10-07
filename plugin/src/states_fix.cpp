// Room for more than 512 states (docs/plugin-spec.md, "States limit").
//
// D2R keeps each unit's state bits inline in its extended stat list (pool 0x2a9a4d0, 0x2b00 bytes): the current
// states at +0xaf0 and the "changed" flags right after them (+0xaf0 + blocks*4), with room for 16 blocks (512 states)
// each. Median has 1,014 states (32 blocks), so the "changed" flags overwrite the fields that follow on every state
// change. This moves both arrays to a new tail, +0x2b00 (current) and +0x2b00 + blocks*4 (changed), 64 blocks of
// room in all, by growing the pool element to 0x2c00 and redirecting every access: check/set/pointer functions, the
// "changed" pointer (a dword index, 0x2bc). The creation memset (grown to the new size) zeroes the new tail; the
// copy routine that seeds new lists from a stack template is left alone (bug #21).
// The sites were found in the dump (research notes in the spec) and are checked byte for byte before patching; it is
// all-or-nothing through one D2RLoader mutation transaction, and skipped when the pool already holds stat lists.
#include "states_fix.h"

#include <D2RLPlugin/mutations.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct Site {
	uint64_t rva;  // of a 32-bit immediate or displacement
	uint32_t from;
	uint32_t to;
	bool current;  // a current-states access (left alone when the current array stays in place)
};

constexpr Site Sites[] = {
	{0x3eb79, 0x2b00, 0x2c00, false},  // pool constructor: element size
	{0x2f66ee, 0x2b00, 0x2c00, false},  // extended stat list creation: memset size
	{0x2f62ba, 0xaf0, 0x2b00, true},  // check state
	{0x2f7f8f, 0xaf0, 0x2b00, true},  // set/clear state: read
	{0x2f7fb0, 0xaf0, 0x2b00, true},  // set/clear state: write
	{0x2f7fff, 0xaf0, 0x2b00, false},  // set state: "changed" bit
	{0x2f63c6, 0xaf0, 0x2b00, true},  // state array pointer
	{0x2f634f, 0x2bc, 0xac0, false},  // "changed" array pointer (dword index)
	// Not the copy routine 0x2f0d80 (its 32 dword moves at 0x2f1060-0x2f120a): its only caller, the creation
	// function 0x2f65d0, copies from a template on its stack whose state area (+0xaf0..+0xb70) is zeroed there.
	// Redirected, it read 0x80 bytes past that template into other stack data, so new stat lists started with
	// random states (bug #21). The new tail is zeroed by the creation memset (0x2f66ee) instead.
};

constexpr uint64_t PoolRva = 0x2a9a4d0;   // pool object: +8 element size, +0x18 / +0x28 chunk lists
constexpr uint32_t OldSize = 0x2b00;
constexpr uint32_t NewSize = 0x2c00;

auto At64(uint64_t base, uint64_t rva) noexcept -> uint64_t {
	return *reinterpret_cast<const volatile uint64_t*>(base + rva);
}

auto ListEmpty(uint64_t base, uint64_t rva) noexcept -> bool {  // never constructed (0) or self-linked
	const uint64_t head = At64(base, rva);
	return head == 0 || head == base + rva;
}

}  // namespace

auto InstallStatesFix(const D2RL::PluginContext* context, bool keepCurrent, char* report, size_t reportSize) noexcept
	-> bool {
	const uint64_t base = context->exeBase;
	if (!ListEmpty(base, PoolRva + 0x18) || !ListEmpty(base, PoolRva + 0x28)) {
		std::snprintf(report, reportSize, "off: the stat-list pool is already in use");
		return false;
	}
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success || mutations == nullptr ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no mutation service");
		return false;
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		std::snprintf(report, reportSize, "off: no transaction");
		return false;
	}
	auto stage = [&](uint64_t rva, uint32_t from, uint32_t to) noexcept -> bool {
		const D2RL::Mutations::BytePatchRequest request {
			.structSize   = D2RL::Mutations::BytePatchRequestSize,
			.flags        = 0,
			.rva          = rva,
			.expected     = &from,
			.expectedSize = sizeof(from),
			.reserved     = 0,
			.bytes        = &to,
			.size         = sizeof(to),
			.reserved2    = 0,
		};
		D2RL::Mutations::OperationHandle operation = 0;
		return mutations->stageBytePatch(context, transaction, &request, &operation) == D2RL::Mutations::Result::Success;
	};
	bool staged = true;
	int sites = 0;
	for (const Site& s : Sites) {
		if (!(keepCurrent && s.current)) {
			staged = staged && stage(s.rva, s.from, s.to);
			++sites;
		}
	}
	// The live pool's element size: 0x2b00 once its static constructor has run, 0 before (then the patched
	// constructor writes 0x2c00 itself).
	const uint32_t liveSize = *reinterpret_cast<const volatile uint32_t*>(base + PoolRva + 8);
	if (liveSize == OldSize) {
		staged = staged && stage(PoolRva + 8, OldSize, NewSize);
	} else if (liveSize != 0) {
		staged = false;
	}
	if (!staged) {
		mutations->cancelTransaction(context, transaction);
		std::snprintf(report, reportSize, "off: a patch site did not match (pool size 0x%x)", liveSize);
		return false;
	}
	D2RL::Mutations::CommitResult result {.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		std::snprintf(report, reportSize, "off: commit failed (reason %u)", static_cast<unsigned>(result.failureReason));
		return false;
	}
	std::snprintf(report, reportSize, keepCurrent ? "on: %d sites, current states stay at +0xaf0 (32 blocks), \"changed\" flags at +0x2b00 + blocks*4 (room for 1024 states)"
	                                              : "on: %d sites, state arrays at +0x2b00 (room for 1024 states)",
	              sites + (liveSize == OldSize ? 1 : 0));
	return true;
}
