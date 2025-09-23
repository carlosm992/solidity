#include <libyul/backends/evm/ssa/BackwardStackLayoutGenerator.h>

#include "AStarShuffler.h"
#include "ExactShuffler.h"
#include "libyul/backends/evm/SSACFGLiveness.h"
#include "libyul/backends/evm/SSACFGStackShuffler.h"
#include "range/v3/algorithm/equal.hpp"
#include "range/v3/algorithm/min_element.hpp"
#include "range/v3/algorithm/none_of.hpp"
#include "range/v3/algorithm/replace.hpp"
#include "range/v3/algorithm/sort.hpp"
#include "range/v3/view/drop.hpp"

#include <libyul/backends/evm/ssa/OperationForwardShuffler.h>

#include <queue>
#include <ranges>

using namespace solidity::yul;
using namespace solidity::yul::ssa;

#if !defined(NDEBUG)
bool BackwardStackLayoutGenerator::StackManipulationCallbacks::writeCallbackOutput = true;
#else
bool BackwardStackLayoutGenerator::StackManipulationCallbacks::writeCallbackOutput = false;
#endif

namespace
{
#if !defined(NDEBUG)
bool constexpr debugOutput = true;
#else
bool constexpr debugOutput = false;
#endif
template<typename Slot>
[[maybe_unused]] std::vector<Slot> pileOfJunk(size_t const _size)
{
	return std::vector<Slot>(_size, ssa::JunkSlot{});
}

/*class IsSSACFGLiteral
{
public:
	explicit IsSSACFGLiteral(SSACFG const& _cfg): m_cfg(_cfg) {}

	bool operator()(SSACFG::ValueId const _valueId) const { return m_cfg.isLiteralValue(_valueId); }
	bool operator()(SSACFGStackLayout::Slot const& _slot) const
	{
		return std::holds_alternative<SSACFG::ValueId>(_slot) && (*this)(std::get<SSACFG::ValueId>(_slot));
	}

private:
	SSACFG const& m_cfg;
};*/

void declareJunk(BackwardStackLayoutGenerator::StackType& _stack, SSACFGLiveness::LivenessData const& _live)
{
	for (size_t depth = 0; depth < _stack.size(); ++depth)
		if (auto const* valueId = std::get_if<SSACFG::ValueId>(&_stack.slot(depth)))
			if (!_live.contains(*valueId))
				_stack.declareJunk(depth);
}

}


SSACFGStackLayout BackwardStackLayoutGenerator::generate(SSACFGLiveness const& _cfgLiveness, SSACFGJunkBlockFinder const& _junkBlockFinder)
{

}
BackwardStackLayoutGenerator::BackwardStackLayoutGenerator(SSACFGLiveness const& _liveness, SSACFGJunkBlockFinder const& _junkBlockFinder):
	m_liveness(_liveness),
	m_cfg(_liveness.cfg()),
	m_junkBlockFinder(_junkBlockFinder),
	m_blockIsGenerated(m_cfg.numBlocks(), false),
	m_blockHasStackInDefined(m_cfg.numBlocks(), false)
{
}
