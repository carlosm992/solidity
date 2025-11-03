#include "fmt/ranges.h"
#include "range/v3/view/drop.hpp"


#include <libyul/backends/evm/ssa/Stack.h>

namespace solidity::yul::ssa
{

std::string slotToString(StackSlot const& _slot)
{
	switch (_slot.kind())
	{
	case StackSlot::Kind::ValueID:
		return fmt::format("{}", _slot.valueID());
	case StackSlot::Kind::Junk:
		return "JUNK";
	case StackSlot::Kind::FunctionCallReturnLabel:
		return fmt::format("FunctionCallReturnLabel[{}]", _slot.functionCallReturnLabel());
	case StackSlot::Kind::FunctionReturnLabel:
		return fmt::format("ReturnLabel[{}]", _slot.functionReturnLabel());
	}
	util::unreachable();
}

std::string slotToString(StackSlot const& _slot, SSACFG const& _cfg)
{
	if (_slot.kind() == StackSlot::Kind::ValueID)
		return _slot.valueID().str(_cfg);

	return slotToString(_slot);
}

std::string stackToString(StackData const& _stackData)
{
	return fmt::format(
		"[{}]",
		fmt::join(_stackData | ranges::views::transform([&](auto const& _slot) { return slotToString(_slot); }), ", ")
	);
}

}
