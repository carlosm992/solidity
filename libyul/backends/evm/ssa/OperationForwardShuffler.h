#pragma once

#include "range/v3/view/enumerate.hpp"


#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/ssa/Stack.h>

#include <range/v3/algorithm/equal.hpp>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/none_of.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/range_concepts.hpp>

#include <boost/container/flat_map.hpp>

namespace solidity::yul::ssa
{

template<StackManipulationCallbackConcept Callback, size_t ReachableStackDepth=16>
class OperationForwardShuffler
{
	using Slot = StackSlot;

public:
	static void shuffle(
		Stack<Callback>& _stack,
		std::vector<Slot> const& _args,
		LivenessAnalysis::LivenessData const& _liveOut,
		std::size_t _targetSize,
		bool _generateJunk
	)
	{
		yulAssert(ranges::none_of(_args, [](auto const& _slot) { return _slot.isJunk(); }));

		TargetStats const targetStats(_args, _liveOut, _targetSize);
		constexpr std::size_t maxIterations = 1000;
		std::size_t i = 0;
		for (; i < maxIterations && shuffleStep(_stack, targetStats, _generateJunk); ++i) {}
		yulAssert(i < maxIterations, fmt::format("Maximum iterations reached on {}", stackToString(_stack.data())));
	}

private:
	struct StackStats
	{
		StackStats(Stack<Callback> const& _stack, size_t _argsRegionSize)
		{
			histogram.reserve(_stack.size());
			histogramTail.reserve(_stack.size());
			histogramArgs.reserve(_argsRegionSize);
			for (auto const& [i, slot]: _stack | ranges::views::enumerate)
			{
				++histogram[slot];
				if (_stack.size() >= _argsRegionSize && i < _stack.size() - _argsRegionSize)
					++histogramTail[slot];
				else
					++histogramArgs[slot];
				if (_stack.size() - i - 1 < ReachableStackDepth)
					++histogramReachable[slot];
			}
		}

		size_t totalCount(Slot const& _slot) const
		{
			return util::valueOrDefault(histogram, _slot, static_cast<size_t>(0));
		}

		size_t argsCount(Slot const& _slot) const
		{
			return util::valueOrDefault(histogramArgs, _slot, static_cast<size_t>(0));
		}

		size_t tailCount(Slot const& _slot) const
		{
			return util::valueOrDefault(histogramTail, _slot, static_cast<size_t>(0));
		}

		size_t reachableCount(Slot const& _slot) const
		{
			return util::valueOrDefault(histogramReachable, _slot, static_cast<size_t>(0));
		}

	private:
		boost::container::flat_map<Slot, size_t> histogramTail;
		boost::container::flat_map<Slot, size_t> histogramArgs;
		boost::container::flat_map<Slot, size_t> histogramReachable;
		boost::container::flat_map<Slot, size_t> histogram;
	};

	struct TargetStats
	{
		TargetStats(std::vector<Slot> const& _args, LivenessAnalysis::LivenessData const& _liveOut, std::size_t const _targetSize):
			args(_args),
			liveOut(_liveOut),
			targetSize(_targetSize)
		{
			targetMinCounts.reserve(_args.size() + _liveOut.size());
			for (auto const& arg: _args)
				++targetMinCounts[arg];
			for (auto const& _liveValueId: _liveOut | ranges::views::keys)
				++targetMinCounts[Slot::makeValueID(_liveValueId)];
		}

		std::vector<Slot> const& args;
		LivenessAnalysis::LivenessData const& liveOut;
		std::size_t const targetSize;
		boost::container::flat_map<Slot, size_t> targetMinCounts;
	};
	struct Ops
	{
		Ops(Stack<Callback>& _stack, TargetStats const& _targetStats):
			stackStats(_stack, _targetStats.args.size()),
			stack(_stack),
			targetStats(_targetStats)
		{}

		bool argsRegionIsCorrect() const
		{
			return targetStats.targetSize == stack.size() && ranges::equal(
				stack.data().rbegin(), stack.data().rbegin() + static_cast<std::ptrdiff_t>(targetStats.args.size()),
				targetStats.args.rbegin(), targetStats.args.rend()
			);
		}

		bool requiredInArgs(Slot const& _slot) const
		{
			return ranges::find(targetStats.args, _slot) != ranges::end(targetStats.args);
		}

		bool requiredInTail(Slot const& _slot) const
		{
			return _slot.isValueID() && targetStats.liveOut.contains(_slot.valueID());
		}

		bool distributionIsCorrect() const
		{
			for (auto const& [targetSlot, targetMinCount]: targetStats.targetMinCounts)
				if (stackStats.totalCount(targetSlot) < targetMinCount)
					return false;
			return true;
		}

		size_t targetMinCount(Slot const& _slot) const
		{
			return util::valueOrDefault(targetStats.targetMinCounts, _slot, size_t{0});
		}

		size_t targetArgsCount(Slot const& _slot) const
		{
			return static_cast<size_t>(ranges::count_if(targetStats.args, [&](auto const& _arg) { return _arg == _slot; }));
		}

		bool stackAdmissible() const
		{
			return argsRegionIsCorrect() && distributionIsCorrect();
		}

		bool canBePopped(Slot const& _slot) const
		{
			return stackStats.totalCount(_slot) > targetMinCount(_slot); // todo  || stack.canBeFreelyGenerated(_slot)?
		}

		bool offsetInTargetArgsRegion(size_t _offset) const
		{
			return _offset >= targetStats.targetSize - targetStats.args.size() && _offset < targetStats.targetSize;
		}

		bool isArgsCompatible(size_t _sourceOffset, size_t _targetOffset) const
		{
			return _sourceOffset < stack.size() && offsetInTargetArgsRegion(_targetOffset) && stack[_sourceOffset] == targetStats.args[_targetOffset - (targetStats.targetSize - targetStats.args.size() - 1 /*todo need -1?*/ )];
		}

		bool isSourceCompatible(size_t _sourceDepth1, size_t _sourceDepth2) const
		{
			return _sourceDepth1 < stack.size() && _sourceDepth2 < stack.size() && stack.slot(_sourceDepth1) == stack.slot(_sourceDepth2);
		}

		bool needsMoreSlots() const
		{
			for (auto const& arg: targetStats.args)
				if (stackStats.totalCount(arg) < targetMinCount(arg))
					return true;
			return false;
		}

		StackStats stackStats;
		Stack<Callback> const& stack;
		TargetStats const& targetStats;
	};

	// If dupping an ideal slot causes a slot that will still be required to become unreachable, then dup
	// the latter slot first.
	// @returns true, if it performed a dup.
	static bool dupDeepSlotIfRequired(Ops const& _ops, Stack<Callback>& _stack, bool const _generateJunk)
	{
		// Check if the stack is large enough for anything to potentially become unreachable.
		if (_stack.size() < ReachableStackDepth - 1)
			return false;
		// Check whether any deep slot might still be needed later (i.e. we still need to reach it with a DUP or SWAP).
		for (size_t const sourceOffset: ranges::views::iota(0u, _stack.size() - (ReachableStackDepth - 1)))
		{
			auto const sourceDepth = _stack.size() - sourceOffset - 1;
			// This slot needs to be moved into args and there is no tail slot of the same kind further up in the stack.
			auto const& slot = _stack.slot(sourceDepth);
			// check if we have more of the same slot further up in the stack
			bool const neededInArgs = _ops.targetArgsCount(slot) > _ops.stackStats.argsCount(slot);
			bool const needMore = _ops.targetMinCount(slot) > _ops.stackStats.totalCount(slot);
			if (neededInArgs || needMore)
			{
				// if we ever need more of a slot then this can only happen if it is something we require
				// in the arguments
				yulAssert(_ops.requiredInArgs(slot));

				auto const [haveMoreAboveWithoutArgs, haveMoreAbove] = [&]
				{
					for (size_t offset = sourceOffset + 1; offset < _stack.size(); ++offset)
					{
						if (_stack[offset] == slot)
							return std::make_tuple(_stack.size() - offset - 1 >= _ops.targetStats.args.size(), true);
					}
					return std::make_tuple(false, false);
				}();

				// if we have more of the same further above, just unconditionally skip this one
				if (haveMoreAboveWithoutArgs)
					continue;

				// if we need this in args and we have the same above but outside args, or we can introduce junk and
				// there is more of the same further up in the stack, skip it
				if ((neededInArgs && haveMoreAboveWithoutArgs) || (_generateJunk && haveMoreAbove))
					continue;

				bool const reachable = sourceDepth < ReachableStackDepth;
				if (reachable)
				{
					if (
						!_ops.isArgsCompatible(sourceOffset, sourceOffset) &&  // the offset isn't already in the right position wrt args
						(
							!_ops.requiredInArgs(_stack.top()) || // current top can go into tail, ie it's not required as arg or
							_ops.stackStats.reachableCount(_stack.top()) > 1 // there's more of it in reachable stack depth
						)
					)
					{
						// top can go into the tail bit, swap it down
						_stack.swap(sourceDepth);
					}
					else
					{
						// we need more of the slot that is about to go out of reach, dup it
						_stack.pushOrDup(slot);
						return true;
					}
				}
				else
				{
					// the slot we need something in the args region of is unreachable, try compressing the stack,
					// first looking at the top
					if (_ops.canBePopped(_stack.top()) || _stack.top().isJunk())
					{
						_stack.pop();
						return true;
					}
					for (size_t depth = 1; depth < std::min(_stack.size(), ReachableStackDepth); ++depth)
						// junk is prioritized
						if (_stack.slot(depth).isJunk())
						{
							_stack.swap(depth);
							_stack.pop();
							return true;
						}
					for (size_t depth = 1; depth < std::min(_stack.size(), ReachableStackDepth); ++depth)
						// then check if we have too much of a variable
						if (_ops.canBePopped(_stack.slot(depth)))
						{
							_stack.swap(depth);
							_stack.pop();
							return true;
						}
					for (size_t depth = 1; depth < std::min(_stack.size(), ReachableStackDepth); ++depth)
						// worst case try popping literals and/or other stuff (return labels etc)
						if (_stack.canBeFreelyGenerated(_stack.slot(depth)))
						{
							_stack.swap(depth);
							_stack.pop();
							return true;
						}
					// todo stack too deep of `slot`. :(
					return false;
				}
			}
		}
		return false;
	}

	static bool shuffleStep(
		Stack<Callback>& _stack,
		TargetStats const& _targetStats,
		bool const _generateJunk
	)
	{
		Ops const ops(_stack, _targetStats);

		// Check if we have the required top and size already
		if (ops.argsRegionIsCorrect())
		{
			// Check if any of the args are required in the tail and not there yet
			bool const needMoreOfAnyArg = [&]
			{
				for (auto const& arg: ops.targetStats.args)
					if (ops.stackStats.totalCount(arg) < ops.targetMinCount(arg))
						return true;
				return false;
			}();
			if (needMoreOfAnyArg)
			{
				// todo whatever we have to dup might not be reachable, check the implications of this in the algorithm:
				//		we could go stack-too-deep or we could try to compress the args (which might end in a loop?)
				for (size_t depth: ranges::views::iota(0u, ops.targetStats.args.size()) | ranges::views::reverse)
					if (ops.stackStats.totalCount(_stack.slot(depth)) < ops.targetMinCount(_stack.slot(depth)))
					{
						// shortcut: if we need more of the top and we only have two args, we can get away with
						// two ops
						// todo this might not be a shortcut if we have already reached target size!
						/*if (depth == 0 && _targetStats.args.size() == 2)
						{
							_stack.swap(1);
							_stack.dup(_stack.slot(1));
							return true;
						}*/
						_stack.pushOrDup(_stack.slot(depth));
						return true;
					}
				yulAssert(false);
			}
			yulAssert(ops.stackAdmissible(), fmt::format("No admissible stack reached: {}", stackToString(_stack.data())));
			return false;
		}

		yulAssert(!_targetStats.args.empty(), "From here on out, we need slots to be required in the top. Otherwise we should've terminated already.");
		yulAssert(_targetStats.targetSize > 0, "Direct consequence from args not being empty");

		// If we no longer need the current stack top, we pop it
		if (
			!_stack.empty() &&  // stack can't be empty if we want to pop things
			ops.stackStats.totalCount(_stack.top()) > ops.targetMinCount(_stack.top()) &&  // there's too much of the top
			!ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) &&  // it's not compatible with its current offset
			_stack.size() < ops.targetStats.targetSize && // todo we might not want to pop this if we need to reach a higher stack size either ?
			!_stack.top().isJunk())  // it's not junk (we might want to swap this later)
		{
			_stack.pop();
			return true;
		}

		// the top is either required in args or in tail or is junk or the stack is empty
		yulAssert(
			_stack.empty() ||										// the stack is either empty
			_stack.top().isJunk() ||								// the stack top is junk
			ops.stackStats.argsCount(_stack.top()) > 0 ||		// the stack top is ... required in args
			ops.stackStats.tailCount(_stack.top()) > 0 ||		//					... required in tail
			ops.isArgsCompatible(_stack.size()-1, _stack.size()-1)		// ... in position (todo this should be implied by the two above)
		);

		// if the top is junk and popping it fixes more positions in args than not popping it, pop it, next step
		// want: [arg3, arg2, arg1]
		// have: [arg3, arg2, arg1, JUNK]
		// -> popping JUNK fixes three arg positions immediately
		// todo this doesnt really work anymore w/ target size
		/*if (!_stack.empty() && _stack.top().isJunk())
		{
			std::ptrdiff_t score = 0;
			// check how many positions in args are currently fine
			for (size_t depth = 0; depth < std::min(ops.targetStats.args.size(), _stack.size()); ++depth)
				score += ops.isArgsCompatible(_stack.size() - depth - 1, _stack.size() - depth - 1);
			// check how many positions we'd fix by popping the top
			for (size_t depth = 0; depth < std::min(ops.targetStats.args.size(), _stack.size()); ++depth)
				score -= ops.isArgsCompatible(_stack.size() - depth - 2, depth);
			if (score < 0)
			{
				_stack.pop();
				return true;
			}
		}*/

		// if there is any slot that we need more of (in args), dup/push it now
		for (auto const& arg: _targetStats.args)
		{
			if (ops.stackStats.totalCount(arg) < ops.targetMinCount(arg))
			{
				if (!dupDeepSlotIfRequired(ops, _stack, _generateJunk))
				{
					_stack.pushOrDup(arg);
					return true;
				}
			}
		}

		// if the top is out of position and required in args
		if (
			!_stack.empty() &&  // if we have an empty stack, we don't need to go down this branch any further
			!ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) &&  // the stack top is not in the args region or inside but out of position
			ops.stackStats.argsCount(_stack.top()) <= ops.targetArgsCount(_stack.top())  // the stack top is needed in args at least as often as is the case right now
		)
		{
			// shortcut
			// todo see if this still works w/ fixed target size
			/*{
				// if the top is required in the second slot position and we require something at the top that isn’t
				// already sufficiently often in the args section and (we can introduce junk or the target top is also
				// required for the tail), try duping a deeper element
				if (ops.isArgsCompatible(0, 1) && !ops.needsMoreSlots())
				{
					if (ops.requiredInTail(_targetStats.args.back()) && ops.stackStats.argsCount(_targetStats.args.back()) < ops.targetArgsCount(_targetStats.args.back())) //
					{
						// dup up whatever wants to be at the top
						for (size_t depth = 1; depth < ReachableStackDepth && depth < _stack.size(); ++depth)
							if (ops.isArgsCompatible(depth, 0))
							{
								if (!dupDeepSlotIfRequired(ops, _stack, _generateJunk))
								{
									_stack.dup(_stack.slot(depth));
									return true;
								}
							}
						if (_stack.canBeFreelyGenerated(_targetStats.args.back()))
						{
							if (!dupDeepSlotIfRequired(ops, _stack, _generateJunk))
							{
								_stack.push(_targetStats.args.back());
								return true;
							}
						}
					}
				}
			}*/

			// if we can introduce junk and we're not overshooting the target size, just try to dup it up
			if (_generateJunk && _stack.size() < _targetStats.targetSize && dupDeepSlotIfRequired(ops, _stack, _generateJunk))
				_stack.pushOrDup(_targetStats.args.back());

			// if we need more of whatever goes to the top, and it's reachable, just dup it
			if (ops.targetMinCount(_targetStats.args.back()) > ops.stackStats.totalCount(_targetStats.args.back()))
				if (auto const depth = _stack.slotDepth(_targetStats.args.back()))
					if (*depth < ReachableStackDepth)
					{
						_stack.dup(_targetStats.args.back());
						return true;
					}

			// try finding a reachable out-of-position target position that, if swapped to, also fixes the top
			for (std::size_t offset = ops.targetStats.targetSize - ops.targetStats.args.size(); offset < ops.targetStats.targetSize && offset < _stack.size(); ++offset)
				if (
					ops.targetStats.targetSize == _stack.size() &&
					ops.isArgsCompatible(offset, ops.targetStats.targetSize - 1) &&  // slot at offset is compatible with target top
					ops.isArgsCompatible(_stack.size() - 1, offset) &&  // top also fixes slot at offset
					!ops.isArgsCompatible(offset, offset))  // offset slot isn't in the right position already
				{
					_stack.swap(_stack.size() - offset - 1);
					return true;
				}

			// if the top can be freely generated and we don't already have enough of it, generate it
			if (_stack.canBeFreelyGenerated(_targetStats.args.back()) && ops.stackStats.totalCount(_targetStats.args.back()) < ops.targetMinCount(_targetStats.args.back()))
			{
				_stack.push(_targetStats.args.back());
				return true;
			}

			// otherwise take the deepest args target slot that doesn’t hold an identical value and isn't in position
			for (size_t depth = 1; depth < std::min(_stack.size(), ops.targetStats.args.size()); ++depth)
				if (ops.isArgsCompatible(0, depth) && !ops.isSourceCompatible(0, depth) && !ops.isArgsCompatible(depth, depth))
				{
					_stack.swap(depth);
					return true;
				}

			// we can’t swap that deep, park current slot in a reachable slot that can be removed (too many of it or junk) and pop the head afterwards
			for (size_t depth = 1; depth < ReachableStackDepth + 1; ++depth)
				if (depth < _stack.size() && ops.canBePopped(_stack.slot(depth)))
				{
					_stack.swap(depth);
					_stack.pop();
					return true;
				}

			// try finding a reachable out-of-position arg slot that fixes the top
			for (size_t depth = 1; depth < std::min(_stack.size(), ops.targetStats.args.size()); ++depth)
				if (ops.isArgsCompatible(depth, 0) && !ops.isArgsCompatible(depth, depth))
				{
					_stack.swap(depth);
					return true;
				}

			// If there is a reachable slot to be removed, park the current top there.
			for (size_t swapDepth: ranges::views::iota(1u, ReachableStackDepth + 1u) | ranges::views::reverse)
				if (swapDepth < _stack.size() && ops.canBePopped(_stack.slot(swapDepth)))
				{
					_stack.swap(swapDepth);
					_stack.pop();
					return true;
				}

			yulAssert(false, fmt::format("stack too deep: {}", stackToString(_stack.data())));
		}

		// stack size > target size cannot be true anymore, since if the source top is no longer required,
		// we already popped it, and if it is required, we already swapped it down to a suitable target position.
		yulAssert(_stack.size() <= _targetStats.targetSize);

		yulAssert(_stack.empty() || _stack.top().isJunk() || ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) || ops.requiredInTail(_stack.top()));

		// if there is a slot that needs to be swapped up or duped but is on the verge of being unreachable, try swapping/duping it
		if (dupDeepSlotIfRequired(ops, _stack, _generateJunk))
			return true;

		// If the top isn’t correct and not required in args, find a slot that is compatible with the target top and swap it up, next step
		if (!_stack.empty() && !ops.isArgsCompatible(0, 0) && ops.requiredInArgs(_stack.top()))
		{
			for (size_t depth: ranges::views::iota(1u, _stack.size()) | ranges::views::reverse)
				// It makes sense to swap to a lower position, if
				if (
					(depth >= _targetStats.args.size() || !ops.isArgsCompatible(depth, depth)) && // The lower slot is not already in position.
					_stack.slot(depth) != _stack.top() && // We would not just swap identical slots.
					ops.isArgsCompatible(depth, 0) // The lower position wants to be at the top
				)
				{
					// We cannot swap that deep.
					if (depth > ReachableStackDepth)
					{
						// If there is a reachable slot to be removed, park the current top there.
						for (size_t swapDepth: ranges::views::iota(1u, ReachableStackDepth + 1u) | ranges::views::reverse)
							if (ops.canBePopped(_stack.slot(swapDepth)))
							{
								_stack.swap(swapDepth);
								if (_stack.top().isJunk())
									// Usually we keep a slot that is to-be-removed, if the current top is arbitrary.
									// However, since we are in a stack-too-deep situation, pop it immediately
									// to compress the stack (we can always push back junk in the end).
									_stack.pop();
								return true;
							}
						// Otherwise, we rely on stack compression or stack-to-memory.
					}
					_stack.swap(depth);
					return true;
				}
			if (_stack.canBeFreelyGenerated(_targetStats.args.back()))
			{
				_stack.pushOrDup(_targetStats.args.back());
				return true;
			}
		}

		yulAssert(_stack.empty() || ops.isArgsCompatible(0, 0) || ops.requiredInTail(_stack.top()) || _stack.top().isJunk(), fmt::format("Current stack: {}", stackToString(_stack.data())));

		// if there is any slot we need more of to populate args, dup that, next step
		for (auto const& arg: ops.targetStats.args)
			if (ops.targetMinCount(arg) > ops.stackStats.totalCount(arg))
				if (!dupDeepSlotIfRequired(ops, _stack, _generateJunk))
				{
					_stack.pushOrDup(arg);
					return true;
				}

		// now all required slots are present in required quantity
		for (auto const& [targetSlot, targetSlotMinCount]: ops.targetStats.targetMinCounts)
			yulAssert(ops.stackStats.totalCount(targetSlot) >= targetSlotMinCount);

		auto swappableDepthRange = ranges::views::iota(0u, std::min(ReachableStackDepth + 1u, _stack.size())) | ranges::views::reverse;

		// If we find a lower slot that is out of position, but also compatible with the top, swap that up.
		for (size_t depth: swappableDepthRange)
			if (!ops.isArgsCompatible(depth, depth) && ops.isArgsCompatible(0, depth))
			{
				_stack.swap(depth);
				return true;
			}

		// Swap up any reachable slot that is still out of position.
		for (size_t depth: swappableDepthRange)
			if (depth < _targetStats.args.size())
			{
				if (!ops.isArgsCompatible(depth, depth) && _stack.slot(depth) != _stack.top())
				{
					_stack.swap(depth);
					return true;
				}
			}
			else
			{
				if (ops.requiredInArgs(_stack.slot(depth)) && ops.stackStats.argsCount(_stack.slot(depth)) < ops.targetArgsCount(_stack.slot(depth)))
				{
					_stack.swap(depth);
					return true;
				}
			}

		// We are in a stack-too-deep situation and try to reduce the stack size.
		// If the current top is merely kept since the target slot is arbitrary, pop it.
		if (ops.canBePopped(_stack.top()))
		{
			_stack.pop();
			return true;
		}

		// If any reachable slot is merely kept, since the target slot is arbitrary, swap it up and pop it.
		for (size_t depth: swappableDepthRange)
			if (_stack.slot(depth).isJunk())
			{
				_stack.swap(depth);
				_stack.pop();
				return true;
			}
		yulAssert(false, "reached final and forbidden state");
	}
};

}
