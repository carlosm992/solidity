#include <libyul/backends/evm/ssa/BackwardStackLayoutGenerator.h>

#include "AStarShuffler.h"
#include "ExactShuffler.h"
#include "libsolutil/Algorithms.h"
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

bool canBeFreelyGenerated(BackwardStackLayoutGenerator::Slot const& _slot, SSACFG const& _cfg)
{
	// todo it could happen that a phi value only points to other phis and literals, in which case it is push-able
	return !std::holds_alternative<SSACFG::ValueId>(_slot) || _cfg.isLiteralValue(std::get<SSACFG::ValueId>(_slot));
}

BackwardStackLayoutGenerator::StackData compressStack(BackwardStackLayoutGenerator::StackData _stack, size_t _reachableStackDepth, SSACFG const& _cfg)
{
	std::optional<size_t> firstDupOffset;
	do
	{
		if (firstDupOffset)
		{
			std::swap(_stack.at(*firstDupOffset), _stack.back());
			_stack.pop_back();
			firstDupOffset.reset();
		}
		for (auto&& [depth, slot]: _stack | ranges::views::reverse | ranges::views::enumerate)
			if (canBeFreelyGenerated(slot, _cfg))
			{
				firstDupOffset = _stack.size() - depth - 1;
				break;
			}
			else if (auto dupDepth = solidity::util::findOffset(_stack | ranges::views::reverse | ranges::views::drop(depth + 1), slot))
				if (depth + *dupDepth <= _reachableStackDepth)
				{
					firstDupOffset = _stack.size() - depth - 1;
					break;
				}
	}
	while (firstDupOffset);
	return _stack;
}

/*template<typename Swap, typename PushOrDup, typename Pop>
void createStackLayout(
	BackwardStackLayoutGenerator::StackData& _currentStack,
	BackwardStackLayoutGenerator::StackData const& _targetStack,
	Swap _swap,
	PushOrDup _pushOrDup,
	Pop _pop,
	size_t _reachableStackDepth
)
{
	struct ShuffleOperations
	{
		BackwardStackLayoutGenerator::StackData& currentStack;
		BackwardStackLayoutGenerator::StackData const& targetStack;
		Swap swapCallback;
		PushOrDup pushOrDupCallback;
		Pop popCallback;
		Multiplicity multiplicity;
		size_t reachableStackDepth;
		ShuffleOperations(
			BackwardStackLayoutGenerator::StackData& _currentStack,
			BackwardStackLayoutGenerator::StackData const& _targetStack,
			Swap _swap,
			PushOrDup _pushOrDup,
			Pop _pop,
			size_t _reachableStackDepth
		):
			currentStack(_currentStack),
			targetStack(_targetStack),
			swapCallback(_swap),
			pushOrDupCallback(_pushOrDup),
			popCallback(_pop),
			reachableStackDepth(_reachableStackDepth)
		{
			for (auto const& slot: currentStack)
				--multiplicity[slot];
			for (auto&& [offset, slot]: targetStack | ranges::views::enumerate)
				if (std::holds_alternative<JunkSlot>(slot) && offset < currentStack.size())
					++multiplicity[currentStack.at(offset)];
				else
					++multiplicity[slot];
		}
		bool isCompatible(size_t _source, size_t _target)
		{
			return
				_source < currentStack.size() &&
				_target < targetStack.size() &&
				(
					std::holds_alternative<JunkSlot>(targetStack.at(_target)) ||
					currentStack.at(_source) == targetStack.at(_target)
				);
		}
		bool sourceIsSame(size_t _lhs, size_t _rhs) { return currentStack.at(_lhs) == currentStack.at(_rhs); }
		int sourceMultiplicity(size_t _offset) { return multiplicity.at(currentStack.at(_offset)); }
		int targetMultiplicity(size_t _offset) { return multiplicity.at(targetStack.at(_offset)); }
		bool targetIsArbitrary(size_t offset)
		{
			return offset < targetStack.size() && std::holds_alternative<JunkSlot>(targetStack.at(offset));
		}
		void swap(size_t _i)
		{
			swapCallback(static_cast<unsigned>(_i));
			std::swap(currentStack.at(currentStack.size() - _i - 1), currentStack.back());
		}
		size_t sourceSize() { return currentStack.size(); }
		size_t targetSize() { return targetStack.size(); }
		void pop()
		{
			popCallback();
			currentStack.pop_back();
		}
		void pushOrDupTarget(size_t _offset)
		{
			auto const& targetSlot = targetStack.at(_offset);
			pushOrDupCallback(targetSlot);
			currentStack.push_back(targetSlot);
		}
	};

	Shuffler<ShuffleOperations>::shuffle(_currentStack, _targetStack, _swap, _pushOrDup, _pop, _reachableStackDepth);

	yulAssert(_currentStack.size() == _targetStack.size(), "");
	for (auto&& [current, target]: ranges::zip_view(_currentStack, _targetStack))
		if (std::holds_alternative<JunkSlot>(target))
			current = JunkSlot{};
		else
			yulAssert(current == target, "");
}*/

BackwardStackLayoutGenerator::StackData combineStack(BackwardStackLayoutGenerator::StackData const& _stack1, BackwardStackLayoutGenerator::StackData const& _stack2, size_t _reachableStackDepth, SSACFG const& _cfg)
{
	// TODO: it would be nicer to replace this by a constructive algorithm.
	// Currently it uses a reduced version of the Heap Algorithm to partly brute-force, which seems
	// to work decently well.

	BackwardStackLayoutGenerator::StackData commonPrefix;
	for (auto&& [slot1, slot2]: ranges::zip_view(_stack1, _stack2))
	{
		if (!(slot1 == slot2))
			break;
		commonPrefix.emplace_back(slot1);
	}

	auto stack1Tail = _stack1 | ranges::views::drop(commonPrefix.size()) | ranges::to<BackwardStackLayoutGenerator::StackData>;
	auto stack2Tail = _stack2 | ranges::views::drop(commonPrefix.size()) | ranges::to<BackwardStackLayoutGenerator::StackData>;

	if (stack1Tail.empty())
		return commonPrefix + compressStack(stack2Tail, _reachableStackDepth, _cfg);
	if (stack2Tail.empty())
		return commonPrefix + compressStack(stack1Tail, _reachableStackDepth, _cfg);

	BackwardStackLayoutGenerator::StackData candidate;
	for (auto slot: stack1Tail)
		if (!solidity::util::contains(candidate, slot))
			candidate.emplace_back(slot);
	for (auto slot: stack2Tail)
		if (!solidity::util::contains(candidate, slot))
			candidate.emplace_back(slot);
	std::erase_if(candidate, [&_cfg](BackwardStackLayoutGenerator::Slot const& slot) {
		return (std::holds_alternative<SSACFG::ValueId>(slot) && _cfg.isLiteralValue(std::get<SSACFG::ValueId>(slot))) || std::holds_alternative<FunctionReturnLabel>(slot);
	});

	struct CountingCallback
	{
		using Slot = BackwardStackLayoutGenerator::Slot;
		size_t numOps = 0;

		void swap(size_t _depth)
		{
			++numOps;
			if (_depth > 16) numOps += 1000;
		}

		void dup(size_t _depth)
		{
			++numOps;
			if (_depth >= 16) numOps += 1000;
		}

		void push(BackwardStackLayoutGenerator::Slot const&)
		{
			++numOps;
		}

		void pop()
		{
			++numOps;
		}
	};

	auto evaluate = [&](BackwardStackLayoutGenerator::StackData const& _candidate) -> size_t {
		size_t numOps = 0;
		BackwardStackLayoutGenerator::StackData testStack = _candidate;
		using CountingStack = ssa::Stack<BackwardStackLayoutGenerator::Slot, CountingCallback>;
		{
			CountingStack stack(testStack, CountingCallback{}, {&_cfg});
			DanielShuffler<CountingStack>::shuffle(stack, {}, stack1Tail);
			numOps += stack.callbacks().numOps;
		}
		testStack = _candidate;
		{
			CountingStack stack(testStack, CountingCallback{}, {&_cfg});
			DanielShuffler<CountingStack>::shuffle(stack, {}, stack2Tail);
			numOps += stack.callbacks().numOps;
		}
		return numOps;
	};

	// See https://en.wikipedia.org/wiki/Heap's_algorithm
	size_t n = candidate.size();
	BackwardStackLayoutGenerator::StackData bestCandidate = candidate;
	size_t bestCost = evaluate(candidate);
	std::vector<size_t> c(n, 0);
	size_t i = 1;
	while (i < n)
	{
		if (c[i] < i)
		{
			if (i & 1)
				std::swap(candidate.front(), candidate[i]);
			else
				std::swap(candidate[c[i]], candidate[i]);
			size_t cost = evaluate(candidate);
			if (cost < bestCost)
			{
				bestCost = cost;
				bestCandidate = candidate;
			}
			++c[i];
			// Note that for a proper implementation of the Heap algorithm this would need to revert back to ``i = 1.``
			// However, the incorrect implementation produces decent result and the proper version would have n!
			// complexity and is thereby not feasible.
			++i;
		}
		else
		{
			c[i] = 0;
			++i;
		}
	}

	return commonPrefix + bestCandidate;
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
void BackwardStackLayoutGenerator::visitBlocks()
{
	std::list toVisit{m_cfg.entry};
	std::set<SSACFG::BlockId> visited;

	// TODO: check whether visiting only a subset of these in the outer iteration below is enough.
	// std::list<std::pair<CFG::BasicBlock const*, CFG::BasicBlock const*>> backwardsJumps = collectBackwardsJumps(_entry);

	while (!toVisit.empty())
	{
		// First calculate stack layouts without walking backwards jumps, i.e. assuming the current preliminary
		// entry layout of the backwards jump target as the initial exit layout of the backwards-jumping block.

		// todo collect these a priori
		std::vector<SSACFG::Edge> backEdges;
		while (!toVisit.empty())
		{
			auto const blockId = *toVisit.begin();
			toVisit.pop_front();

			if (visited.contains(blockId))
				continue;

			auto const& block = m_cfg.block(blockId);

			// get exit layout or stage dependencies
			auto exitLayout = std::visit(util::GenericVisitor{
				[](SSACFG::BasicBlock::MainExit const&) { return std::make_optional(SSACFGStackLayout::Stack{}); },
				[](SSACFG::BasicBlock::Terminated const&) { return std::make_optional(SSACFGStackLayout::Stack{}); },
				[](SSACFG::BasicBlock::FunctionReturn const& _functionReturn)
				{
					return std::make_optional(SSACFGStackLayout::Stack{_functionReturn.returnValues.begin(), _functionReturn.returnValues.end()});
				},
				[](SSACFG::BasicBlock::JumpTable const&) -> std::optional<SSACFGStackLayout::Stack> { yulAssert(false); },
				[&](SSACFG::BasicBlock::Jump const& _jump) -> std::optional<SSACFGStackLayout::Stack>
				{
					if (m_liveness.topologicalSort().backEdge(blockId, _jump.target))
					{
						backEdges.emplace_back(blockId, _jump.target);
						if (m_blockHasStackInDefined[_jump.target.value])
							return m_stackLayout[_jump.target].stackIn;
						return {};
					}

					if (visited.contains(_jump.target))
						return m_stackLayout[_jump.target].stackIn;

					toVisit.emplace_front(_jump.target);
					return std::nullopt;
				},
				[&](SSACFG::BasicBlock::ConditionalJump const& _conditionalJump) -> std::optional<SSACFGStackLayout::Stack>
				{
					bool const zeroVisited = visited.contains(_conditionalJump.zero);
					bool const nonZeroVisited = visited.contains(_conditionalJump.nonZero);
					if (zeroVisited && nonZeroVisited)
					{
						// If the current iteration has already visited both jump targets, start from its entry layout.
						SSACFGStackLayout::Stack stack = combineStack(
							m_stackLayout[_conditionalJump.zero].stackIn,
							m_stackLayout[_conditionalJump.nonZero].stackIn,
							16,
							m_cfg
						);
						// Additionally, the jump condition has to be at the stack top at exit.
						stack.emplace_back(_conditionalJump.condition);
						return stack;
					}
					// If one of the jump targets has not been visited, stage it for visit and defer the current block.
					if (!zeroVisited)
						toVisit.emplace_front(_conditionalJump.zero);
					if (!nonZeroVisited)
						toVisit.emplace_front(_conditionalJump.nonZero);
					return std::nullopt;
				},

			}, block.exit);

			if (exitLayout)
			{
				visited.emplace(blockId);
				auto& info = m_stackLayout[blockId];
				info.stackOut = *exitLayout;
				// info.stackIn = propagateStackThroughBlock(info.stackOut, block);

				for (auto entry: block.entries)
					toVisit.emplace_back(entry);
			}
			else
				continue;
		}

		// Determine which backwards jumps still require fixing and stage revisits of appropriate nodes.
		for (auto const& [backEdgeSource, backEdgeTarget]: backEdges)
		{
			// This block jumps backwards, but does not provide all slots required by the jump target on exit.
			// Therefore we need to visit the subgraph between ``target`` and ``jumpingBlock`` again.
			if (ranges::any_of(
				m_stackLayout[backEdgeTarget].stackIn,
				[exitLayout = m_stackLayout[backEdgeSource].stackOut](StackSlot const& _slot) {
					return !util::contains(exitLayout, _slot);
				}
			))
			{
				// In particular we can visit backwards starting from ``jumpingBlock`` and mark all entries to-be-visited-
				// again until we hit ``target``.
				toVisit.emplace_front(backEdgeSource);
				// Since we are likely to permute the entry layout of ``target``, we also visit its entries again.
				// This is not required for correctness, since the set of stack slots will match, but it may move some
				// required stack shuffling from the loop condition to outside the loop.
				visited -= m_cfg.block(backEdgeTarget).entries;
				util::BreadthFirstSearch<SSACFG::BlockId>{{backEdgeSource}}.run(
					[this, &visited, target = backEdgeTarget](SSACFG::BlockId const& _block, auto _addChild) {
						visited.erase(_block);
						if (_block == target)
							return;
						for (auto const& entry: m_cfg.block(_block).entries)
							_addChild(entry);
					}
				);
				// While the shuffled layout for ``target`` will be compatible, it can be worthwhile propagating
				// it further up once more.
				// This would mean not stopping at _block == target above, resp. even doing visited.clear() here, revisiting the entire graph.
				// This is a tradeoff between the runtime of this process and the optimality of the result.
				// Also note that while visiting the entire graph again *can* be helpful, it can also be detrimental.
			}
		}
	}

	// todo re-enable
	/*stitchConditionalJumps(_entry);
	fillInJunk(_entry, _functionInfo);*/
}

