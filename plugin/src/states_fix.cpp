// Room for more than 512 states (docs/plugin-spec.md, "States limit").
//
// D2R keeps each unit's state bits inline in its extended stat list (pool 0x2a9a4d0, 0x2b00 bytes): the current
// states at +0xaf0 and the "changed" flags right after them (+0xaf0 + blocks*4), with room for 16 blocks (512 states)
// each. Median has 1,014 states (32 blocks), so the "changed" flags overwrite the fields that follow on every state
// change. This moves both arrays to a new tail, +0x2b00 (current) and +0x2b00 + blocks*4 (changed), 64 blocks of
// room in all, by growing the pool element to 0x2c00 and redirecting every access: check/set/pointer functions, the
// "changed" pointer (a dword index, 0x2bc) and the copy routine's 32 dwords (now the whole 32-block state array).
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
};

constexpr Site Sites[] = {
	{0x3eb79, 0x2b00, 0x2c00},  // pool constructor: element size
	{0x2f66ee, 0x2b00, 0x2c00},  // extended stat list creation: memset size
	{0x2f62ba, 0xaf0, 0x2b00},  // check state
	{0x2f7f8f, 0xaf0, 0x2b00},  // set/clear state: read
	{0x2f7fb0, 0xaf0, 0x2b00},  // set/clear state: write
	{0x2f7fff, 0xaf0, 0x2b00},  // set state: "changed" bit
	{0x2f63c6, 0xaf0, 0x2b00},  // state array pointer
	{0x2f634f, 0x2bc, 0xac0},  // "changed" array pointer (dword index)
	{0x2f1063, 0xaf0, 0x2b00},
	{0x2f1069, 0xaf0, 0x2b00},
	{0x2f1070, 0xaf4, 0x2b04},
	{0x2f1076, 0xaf4, 0x2b04},
	{0x2f107d, 0xaf8, 0x2b08},
	{0x2f1083, 0xaf8, 0x2b08},
	{0x2f108a, 0xafc, 0x2b0c},
	{0x2f1090, 0xafc, 0x2b0c},
	{0x2f1097, 0xb00, 0x2b10},
	{0x2f109d, 0xb00, 0x2b10},
	{0x2f10a4, 0xb04, 0x2b14},
	{0x2f10aa, 0xb04, 0x2b14},
	{0x2f10b1, 0xb08, 0x2b18},
	{0x2f10b7, 0xb08, 0x2b18},
	{0x2f10be, 0xb0c, 0x2b1c},
	{0x2f10c4, 0xb0c, 0x2b1c},
	{0x2f10cb, 0xb10, 0x2b20},
	{0x2f10d1, 0xb10, 0x2b20},
	{0x2f10d8, 0xb14, 0x2b24},
	{0x2f10de, 0xb14, 0x2b24},
	{0x2f10e5, 0xb18, 0x2b28},
	{0x2f10eb, 0xb18, 0x2b28},
	{0x2f10f2, 0xb1c, 0x2b2c},
	{0x2f10f8, 0xb1c, 0x2b2c},
	{0x2f10ff, 0xb20, 0x2b30},
	{0x2f1105, 0xb20, 0x2b30},
	{0x2f110c, 0xb24, 0x2b34},
	{0x2f1112, 0xb24, 0x2b34},
	{0x2f1119, 0xb28, 0x2b38},
	{0x2f111f, 0xb28, 0x2b38},
	{0x2f1126, 0xb2c, 0x2b3c},
	{0x2f112c, 0xb2c, 0x2b3c},
	{0x2f1133, 0xb30, 0x2b40},
	{0x2f1139, 0xb30, 0x2b40},
	{0x2f1140, 0xb34, 0x2b44},
	{0x2f1146, 0xb34, 0x2b44},
	{0x2f114d, 0xb38, 0x2b48},
	{0x2f1153, 0xb38, 0x2b48},
	{0x2f115a, 0xb3c, 0x2b4c},
	{0x2f116a, 0xb3c, 0x2b4c},
	{0x2f1171, 0xb40, 0x2b50},
	{0x2f1177, 0xb40, 0x2b50},
	{0x2f117e, 0xb44, 0x2b54},
	{0x2f1184, 0xb44, 0x2b54},
	{0x2f118b, 0xb48, 0x2b58},
	{0x2f1191, 0xb48, 0x2b58},
	{0x2f1198, 0xb4c, 0x2b5c},
	{0x2f119e, 0xb4c, 0x2b5c},
	{0x2f11a5, 0xb50, 0x2b60},
	{0x2f11ab, 0xb50, 0x2b60},
	{0x2f11b2, 0xb54, 0x2b64},
	{0x2f11b8, 0xb54, 0x2b64},
	{0x2f11bf, 0xb58, 0x2b68},
	{0x2f11c5, 0xb58, 0x2b68},
	{0x2f11cc, 0xb5c, 0x2b6c},
	{0x2f11d2, 0xb5c, 0x2b6c},
	{0x2f11d9, 0xb60, 0x2b70},
	{0x2f11df, 0xb60, 0x2b70},
	{0x2f11e6, 0xb64, 0x2b74},
	{0x2f11ec, 0xb64, 0x2b74},
	{0x2f11f3, 0xb68, 0x2b78},
	{0x2f11f9, 0xb68, 0x2b78},
	{0x2f1200, 0xb6c, 0x2b7c},
	{0x2f1206, 0xb6c, 0x2b7c},
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

auto InstallStatesFix(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
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
	for (const Site& s : Sites) {
		staged = staged && stage(s.rva, s.from, s.to);
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
	std::snprintf(report, reportSize, "on: %zu sites, state arrays at +0x2b00 (room for 1024 states)",
	              sizeof(Sites) / sizeof(Sites[0]) + (liveSize == OldSize ? 1 : 0));
	return true;
}
