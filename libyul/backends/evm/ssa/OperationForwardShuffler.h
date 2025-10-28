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
			targetSize(_targetSize),
			tailSize(_targetSize - _args.size())
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
		std::size_t const tailSize;
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
			return _sourceOffset < stack.size() && offsetInTargetArgsRegion(_targetOffset) && stack[_sourceOffset] == targetStats.args[_targetOffset - targetStats.tailSize];
		}

		bool isSourceCompatible(size_t _sourceOffset1, size_t _sourceOffset2) const
		{
			return _sourceOffset1 < stack.size() && _sourceOffset2 < stack.size() && stack[_sourceOffset1] == stack[_sourceOffset2];
		}

		bool needsMoreSlots() const
		{
			for (auto const& arg: targetStats.args)
				if (stackStats.totalCount(arg) < targetMinCount(arg))
					return true;
			return false;
		}

		size_t sourceOffsetToDepth(size_t _offset) const
		{
			yulAssert(_offset < stack.size(), "Offset out of range");
			return stack.size() - _offset - 1;
		}

		// todo do i need this even
		size_t targetOffsetToDepth(size_t _offset) const
		{
			yulAssert(_offset < targetStats.targetSize, "Offset out of range");
			return targetStats.targetSize - _offset - 1;
		}

		bool compress(bool _canPopTop = false)
		{
			// from deep to shallow check if something can be popped, then pop it
			auto const depthRange = ranges::views::iota(_canPopTop ? 0u : 1u, ReachableStackDepth + 1u);
			for (size_t depth: depthRange | ranges::views::reverse)
				if (depth < stack.size() && canBePopped(stack.slot(depth)))
				{
					if (depth > 0)
						stack.swap(depth);
					stack.pop();
					return true;
				}
			return false;
		}

		StackStats stackStats;
		Stack<Callback>& stack;
		TargetStats const& targetStats;
	};

	// If dupping an ideal slot causes a slot that will still be required to become unreachable, then dup
	// the latter slot first.
	// @returns true, if it performed a dup.
	static bool dupDeepSlotIfRequired(Ops const& _ops, bool const _generateJunk)
	{
		// Check if the stack is large enough for anything to potentially become unreachable.
		if (_ops.stack.size() < ReachableStackDepth - 1)
			return false;
		// Check whether any deep slot might still be needed later (i.e. we still need to reach it with a DUP or SWAP).
		for (size_t const sourceOffset: ranges::views::iota(0u, _ops.stack.size() - (ReachableStackDepth - 1)))
		{
			auto const sourceDepth = _ops.stack.size() - sourceOffset - 1;
			// This slot needs to be moved into args and there is no tail slot of the same kind further up in the stack.
			auto const& slot = _ops.stack.slot(sourceDepth);
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
					for (size_t offset = sourceOffset + 1; offset < _ops.stack.size(); ++offset)
					{
						if (_ops.stack[offset] == slot)
							return std::make_tuple(_ops.stack.size() - offset - 1 >= _ops.targetStats.args.size(), true);
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
							!_ops.requiredInArgs(_ops.stack.top()) || // current top can go into tail, ie it's not required as arg or
							_ops.stackStats.reachableCount(_ops.stack.top()) > 1 // there's more of it in reachable stack depth
						)
					)
					{
						// top can go into the tail bit, swap it down
						_ops.stack.swap(sourceDepth);
					}
					else
					{
						// we need more of the slot that is about to go out of reach, dup it
						_ops.stack.pushOrDup(slot);
						return true;
					}
				}
				else
				{
					// the slot we need something in the args region of is unreachable, try compressing the stack,
					// first looking at the top
					if (_ops.canBePopped(_ops.stack.top()) || _ops.stack.top().isJunk())
					{
						_ops.stack.pop();
						return true;
					}
					for (size_t depth = 1; depth < std::min(_ops.stack.size(), ReachableStackDepth); ++depth)
						// junk is prioritized
						if (_ops.stack.slot(depth).isJunk())
						{
							_ops.stack.swap(depth);
							_ops.stack.pop();
							return true;
						}
					for (size_t depth = 1; depth < std::min(_ops.stack.size(), ReachableStackDepth); ++depth)
						// then check if we have too much of a variable
						if (_ops.canBePopped(_ops.stack.slot(depth)))
						{
							_ops.stack.swap(depth);
							_ops.stack.pop();
							return true;
						}
					for (size_t depth = 1; depth < std::min(_ops.stack.size(), ReachableStackDepth); ++depth)
						// worst case try popping literals and/or other stuff (return labels etc)
						if (_ops.stack.canBeFreelyGenerated(_ops.stack.slot(depth)))
						{
							_ops.stack.swap(depth);
							_ops.stack.pop();
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
		Ops ops(_stack, _targetStats);

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
				// if we're already too large, try to swap and pop something instead of growing even more
				if (ops.compress())
					return true;

				// todo whatever we have to dup might not be reachable, check the implications of this in the algorithm:
				//		we could go stack-too-deep or we could try to compress the args (which might end in a loop?)
				for (size_t depth: ranges::views::iota(0u, ReachableStackDepth) | ranges::views::reverse)
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
			_stack.size() > ops.targetStats.targetSize && // todo we might not want to pop this if we need to reach a higher stack size either ?
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
				if (!dupDeepSlotIfRequired(ops, _generateJunk))
				{
					_stack.pushOrDup(arg);
					return true;
				}
				return true;
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
			if (_generateJunk && _stack.size() < _targetStats.targetSize)
			{
				if (!dupDeepSlotIfRequired(ops, _generateJunk))
					_stack.pushOrDup(_targetStats.args.back());
				return true;
			}

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
					_stack.swap(ops.sourceOffsetToDepth(offset));
					return true;
				}

			// if the top is in args region and can be freely generated, and we don't already have enough of it, generate it
			if (ops.offsetInTargetArgsRegion(_stack.size()) && _stack.canBeFreelyGenerated(_targetStats.args[_stack.size() - (_targetStats.targetSize - _targetStats.args.size())]))
			{
				_stack.push(_targetStats.args.back());
				return true;
			}

			// check if any slot in the args offset is compatible with the stack top and not already in position
			// and then swap
			for (size_t argsOffset = ops.targetStats.targetSize - ops.targetStats.args.size(); argsOffset < ops.targetStats.targetSize; ++argsOffset)
			{
				if (
					ops.isArgsCompatible(_stack.size() - 1, argsOffset) &&  // top is compatible with args offset
					!ops.isArgsCompatible(argsOffset, argsOffset) &&  // args offset is out of position
					argsOffset < _stack.size() &&
					ops.sourceOffsetToDepth(argsOffset) <= ReachableStackDepth  // reachable
				)
				{
					_stack.swap(ops.sourceOffsetToDepth(argsOffset));
					return true;
				}
			}

			// take the deepest args target slot that doesn’t hold an identical value and isn't in position
			bool haveOutOfPositionSlot = false;
			for (size_t argsOffset = ops.targetStats.targetSize - ops.targetStats.args.size(); argsOffset < ops.targetStats.targetSize; ++argsOffset)
				if (
					!ops.isArgsCompatible(argsOffset, argsOffset) &&  // slot at args offset is out of position
					!ops.isSourceCompatible(_stack.size() - 1, argsOffset) &&  // current top isn't compatible with that one
					argsOffset < _stack.size()
				)
				{
					// check that there is a compatible slot somewhere on the stack that isn't already finalized
					for (size_t stackArgOffset = _targetStats.tailSize; stackArgOffset < _stack.size(); ++stackArgOffset)
						if (ops.isArgsCompatible(argsOffset, stackArgOffset) && !ops.isArgsCompatible(stackArgOffset, stackArgOffset))
						{
							haveOutOfPositionSlot = true;
							if (ops.sourceOffsetToDepth(argsOffset) <= ReachableStackDepth)
							{
								_stack.swap(ops.sourceOffsetToDepth(argsOffset));
								return true;
							}
						}
				}

			// we can’t swap that deep, park current slot in a reachable slot that can be removed (too many of it or junk) and pop the head afterwards
			if (haveOutOfPositionSlot && ops.compress())
				return true;

			// todo unnecessary?
			/*// try finding a reachable out-of-position arg slot that fixes the top
			for (size_t depth = 1; depth < std::min(_stack.size(), ops.targetStats.args.size()); ++depth)
				if (ops.isArgsCompatible(depth, 0) && !ops.isArgsCompatible(depth, depth))
				{
					_stack.swap(depth);
					return true;
				}*/

			// If there is a reachable slot to be removed, park the current top there.
			/*for (size_t swapDepth: ranges::views::iota(1u, ReachableStackDepth + 1u) | ranges::views::reverse)
				if (swapDepth < _stack.size() && ops.canBePopped(_stack.slot(swapDepth)))
				{
					_stack.swap(swapDepth);
					_stack.pop();
					return true;
				}*/
		}

		// if we're still too large, there should be something we can dispose of
		if (_stack.size() > _targetStats.targetSize)
		{
			if (ops.compress(true))
				return true;

			yulAssert("Couldn't reach target size.");
		}

		// stack size > target size cannot be true anymore, since if the source top is no longer required,
		// we already popped it, and if it is required in args, we already swapped it down to a suitable target position.
		yulAssert(_stack.size() <= _targetStats.targetSize, fmt::format("oops: {}, {}", _targetStats.targetSize, stackToString(_stack.data())));

		/*yulAssert(
			_stack.empty() ||  // stack can be empty
			_stack.top().isJunk() ||  // top may be junk
			ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) ||  // top is in position (?) todo dont think this is true anymore, we might be smaller
			ops.requiredInTail(_stack.top()) // top needs to go to tail (perhaps also into args)
		);*/

		// if the stack is so small that it's not reaching into the args region, try to dup something up that either fixes
		// the top (in the case of size is just below args) or just dup the deepest elem
		if (_stack.size() <= _targetStats.tailSize) {
			// the stack is so that the top is just below the args region
			if (_stack.size() == _targetStats.tailSize)
			{
				// if there's something that will run out of scope by duping, dup that now
				if (dupDeepSlotIfRequired(ops, _generateJunk))
					return true;

				// dup something that is compatible with the deepest arg
				std::optional<size_t> dupDepth = _stack.slotDepth(_targetStats.args.back());
				if (dupDepth.has_value())
				{
					if (*dupDepth < ReachableStackDepth)
					{
						_stack.dup(_stack.slot(*dupDepth));
						return true;
					}

					// in case of literals, we try to dup
					// todo this should get a special case for lit 0
					if (_stack.canBeFreelyGenerated(_targetStats.args.back()) && !_targetStats.args.back().isValueID())
					{
						_stack.push(_targetStats.args.back());
						return true;
					}

					if (ops.compress())
						return true;

					// potential stack too deep, just dup and let the callback handle it
					_stack.pushOrDup(_targetStats.args.back());
					return true;
				}

				// didn't find the slot, has to be something that can be pushed or we are in an invalid state
				_stack.push(_targetStats.args.back());
				return true;
			}

			// dup up the deepest slot that is required in args (or compress if unreachable)
			for (size_t offset: ranges::views::iota(0u, _stack.size()) | ranges::views::reverse)
			{
				// if we need the slot in args and there's no slot of the same kind further up
				if (ops.requiredInArgs(_stack[offset]) && std::find(_stack.begin() + offset + 1, _stack.end(), _stack[offset]) == _stack.end())
				{
					// swap if top isn't needed
					if (offset == ReachableStackDepth && !ops.requiredInArgs(_stack.top()))
					{
						_stack.swap(ops.sourceOffsetToDepth(offset));
						return true;
					}

					// dup if we can
					if (ops.sourceOffsetToDepth(offset) < ReachableStackDepth)
					{
						_stack.dup(_stack[offset]);
						return true;
					}

					// can it be freely generated but unreachable? just push a 0, we can push it later
					if (_stack.canBeFreelyGenerated(_stack[offset]))
					{
						_stack.push(Stack<Callback>::Slot::makeJunk());
						return true;
					}

					// try to compress
					if (ops.compress())
						return true;

					// stack too deep
					_stack.dup(_stack[offset]);
					return true;
				}
			}

			// push junk, didnt find anything suitable
			_stack.push(Stack<Callback>::Slot::makeJunk());
			return true;
		}

		// we have filled the stack so it reaches into the args region
		yulAssert(_stack.size() > _targetStats.targetSize - _targetStats.args.size());
		// the stack size does not exceed the target size
		yulAssert(_stack.size() <= _targetStats.targetSize);

		// stack can't be empty anymore at this point, as its size is so that at least one element is in the
		// args region and the args region is non-empty
		yulAssert(!_stack.empty());

		// If the top isn’t correct and not required in args,
		// find a slot that is compatible with the target top and swap it up, next step
		if (
			!ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) &&  // it's not in position
			!ops.requiredInArgs(_stack.top())  // not needed in args at all, can be swapped into tail
		)
		{
			auto const tailEnd = std::min(_stack.begin() + _targetStats.tailSize, _stack.end());
			for (size_t offset: ranges::views::iota(0u, _stack.size() - 1))
				// It makes sense to swap to a lower position, if
				if (
					(!ops.offsetInTargetArgsRegion(offset) || !ops.isArgsCompatible(offset, offset)) && // The lower slot is not already in position.
					!ops.isSourceCompatible(offset, _stack.size() - 1) && // We would not just swap identical slots.
					ops.isArgsCompatible(offset, _stack.size() - 1) && // The lower position wants to be at the top
					std::find(
						std::min(_stack.begin() + offset + 1, tailEnd),
						tailEnd,
						_stack[offset]
					) == tailEnd  // there is no same thing in the tail part further up the stack
				)
				{
					if (offset <= ReachableStackDepth)
					{
						_stack.swap(ops.sourceOffsetToDepth(offset));
						return true;
					}

					// We cannot swap that deep.
					if (ops.compress())
						return true;

					if (_stack.canBeFreelyGenerated(_stack[offset]))
					{
						_stack.pop();
						_stack.push(_stack[offset]);
						return true;
					}

					// stack too deep
					_stack.swap(ops.sourceOffsetToDepth(offset));
					return true;
				}

			// from here on we didn't find a suitable slot in the tail to swap up, so let's get rid of the top

			// the top isn't correct and not required in args and if the stack top isn't required in tail
			// we can just pop it and return true
			if (!ops.requiredInTail(_stack.top()))
			{
				_stack.pop();
				return true;
			}

			if (ops.requiredInTail(_stack.top()))
			{
				// the top is required in tail and already there, just pop it too
				if (ops.stackStats.tailCount(_stack.top()) > 0)
				{
					_stack.pop();
					return true;
				}

				// bring it down by swapping something up that wants to be in args or can be popped
				for (size_t tailOffset: ranges::views::iota(0u, _targetStats.tailSize) | ranges::views::reverse)
				{
					if (ops.stackStats.argsCount(_stack[tailOffset]) < ops.targetArgsCount(_stack[tailOffset]))
					{
						if (ops.sourceOffsetToDepth(tailOffset) > ReachableStackDepth)
						{
							if (ops.compress())
								return true;
							// stack too deep
						}

						_stack.swap(ops.sourceOffsetToDepth(tailOffset));
						return true;
					}
				}

				for (size_t tailOffset: ranges::views::iota(0u, _targetStats.tailSize) | ranges::views::reverse)
				{
					if (ops.canBePopped(_stack[tailOffset]))
					{
						_stack.swap(ops.sourceOffsetToDepth(tailOffset));
						_stack.pop();
						return true;
					}
				}
			}

			// it is required in tail and not already there, try finding something that can be popped
			for (size_t tailOffset: ranges::views::iota(0u, _targetStats.tailSize) | ranges::views::reverse)
			{
				if (ops.canBePopped(_stack[tailOffset]))
				{
					if (ops.sourceOffsetToDepth(tailOffset) <= ReachableStackDepth)
					{
						_stack.swap(ops.sourceOffsetToDepth(tailOffset));
						_stack.pop();
						return true;
					}
				}
			}

			// it is required in tail and not already there, try finding something that can be popped by pushing it again (bad case)
			for (size_t tailOffset: ranges::views::iota(0u, _targetStats.tailSize) | ranges::views::reverse)
			{
				if (_stack.canBeFreelyGenerated(_stack[tailOffset]))
				{
					if (ops.sourceOffsetToDepth(tailOffset) <= ReachableStackDepth)
					{
						_stack.swap(ops.sourceOffsetToDepth(tailOffset));
						_stack.pop();
						return true;
					}
				}
			}

			// now we are in a situation where the only thing we can pop is too deep in stack to reach
			// let's swap with it anyway and stack-to-deep handling deal with it
			for (size_t tailOffset: ranges::views::iota(0u, _targetStats.tailSize) | ranges::views::reverse)
			{
				if (ops.canBePopped(_stack[tailOffset]))
				{
					_stack.swap(ops.sourceOffsetToDepth(tailOffset));
					_stack.pop();
					return true;
				}
			}

			yulAssert(false, "the top is not required in args but required in tail but we can't pop _anything_ in tail, this shouldn't be possible.");
		}

		yulAssert(
			(
				_stack.empty() ||
				ops.isArgsCompatible(_stack.size() - 1, _stack.size() - 1) ||
				ops.requiredInArgs(_stack.top()) ||
				_stack.top().isJunk()
			),
			fmt::format("Current stack: {}", stackToString(_stack.data()))
		);

		// if we haven't matched the target size, try to dup things into their right position
		if (_stack.size() < _targetStats.targetSize)
		{
			if (!dupDeepSlotIfRequired(ops, _generateJunk))
			{
				_stack.pushOrDup(_targetStats.args[_stack.size() - _targetStats.tailSize]);
				return true;
			}
		}

		// if there is any slot we need more of to populate args, dup that, next step
		for (auto const& arg: ops.targetStats.args)
			if (ops.targetMinCount(arg) > ops.stackStats.totalCount(arg))
			{
				if (!dupDeepSlotIfRequired(ops, _generateJunk))
				{
					_stack.pushOrDup(arg);
					return true;
				}
				return true;
			}

		// now all required slots are present in required quantity
		for (auto const& [targetSlot, targetSlotMinCount]: ops.targetStats.targetMinCounts)
			yulAssert(ops.stackStats.totalCount(targetSlot) >= targetSlotMinCount);
		// also the size should be correct
		yulAssert(_stack.size() == _targetStats.targetSize);

		auto swappableOffsets = ranges::views::iota(_stack.size() > ReachableStackDepth + 1u ? _stack.size() - (ReachableStackDepth + 1u) : 0u, _stack.size());

		// If we find a lower slot that is out of position, but also compatible with the top, swap that up.
		for (size_t offset: swappableOffsets)
			if (
				!ops.isArgsCompatible(offset, offset) &&
				!ops.isSourceCompatible(offset, _stack.size() - 1) &&
				ops.isArgsCompatible(offset, _stack.size() - 1)
			)
			{
				_stack.swap(ops.sourceOffsetToDepth(offset));
				return true;
			}

		// Swap up any reachable slot that is still out of position.
		for (size_t offset: swappableOffsets)
			if (ops.sourceOffsetToDepth(offset) < _targetStats.args.size())
			{
				if (ops.offsetInTargetArgsRegion(offset) && !ops.isArgsCompatible(offset, offset))
				{
					_stack.swap(ops.sourceOffsetToDepth(offset));
					return true;
				}
			}
			else
			{
				if (ops.requiredInArgs(_stack[offset]) && ops.stackStats.argsCount(_stack[offset]) < ops.targetArgsCount(_stack[offset]))
				{
					_stack.swap(ops.sourceOffsetToDepth(offset));
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
		for (size_t depth: swappableOffsets)
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
