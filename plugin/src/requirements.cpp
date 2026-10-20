#include "requirements.h"

#include "mxl_requirements.h"

namespace mxl_requirements {
namespace {

constexpr int MaxDepth = 16;  // the table has 33 rows; deeper means a cycle

auto Eval(int row, const Queries& q, int depth) noexcept -> bool {
	if (row <= 0) {
		return true;
	}
	if (row >= RowCount || depth > MaxDepth) {
		return false;
	}
	const Row& r = Rows[row];
	switch (r.type) {
	case 0:  // none
		return true;
	case 1:  // OR of the children (0 = unused slot)
	{
		for (const int32_t child : r.child) {
			if (child != 0 && Eval(child, q, depth + 1)) {
				return true;
			}
		}
		return false;
	}
	case 2:  // AND of the children
	{
		for (const int32_t child : r.child) {
			if (child != 0 && !Eval(child, q, depth + 1)) {
				return false;
			}
		}
		return true;
	}
	case 9:  // quest bit set: a = quest, b = bit, c = difficulty (-1 = the game's)
		return q.questBit != nullptr && q.questBit(q.user, r.a, r.b, r.c);
	case 30:  // player class == a
		return q.playerClass != nullptr && q.playerClass(q.user) == r.a;
	case 35:  // does NOT hold an item with this code
		return q.holdsItem != nullptr && !q.holdsItem(q.user, r.code);
	case 36:  // constant false
		return false;
	default:
		return false;
	}
}

}  // namespace

auto Evaluate(int row, const Queries& queries) noexcept -> bool {
	return Eval(row, queries, 0);
}

}  // namespace mxl_requirements
