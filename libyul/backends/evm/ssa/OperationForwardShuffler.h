#pragma once

#include "range/v3/algorithm/count.hpp"
#include "range/v3/view/enumerate.hpp"


#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/ssa/Stack.h>
#include <ranges>

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
		// yulAssert(ranges::none_of(_args, [](auto const& _slot) { return _slot.isJunk(); }));

		TargetStats const targetStats(_args, _liveOut, _targetSize);
		yulAssert(_liveOut.size() <= targetStats.tailSize, "not enough tail space");
		{
			// admissibility: check that all required values are on stack
			StackStats stackStats(_stack, targetStats.args.size());
			for (const auto& liveVariable: _liveOut | ranges::views::keys | ranges::views::transform(Slot::makeValueID))
				yulAssert(_stack.canBeFreelyGenerated(liveVariable) || stackStats.totalCount(liveVariable) > 0);
			for (const auto& arg: _args)
				yulAssert(_stack.canBeFreelyGenerated(arg) || stackStats.totalCount(arg) > 0);
		}

		constexpr std::size_t maxIterations = 1000;
		std::size_t i = 0;
		for (; i < maxIterations && shuffleStep(_stack, targetStats, _generateJunk); ++i) {}
		yulAssert(i < maxIterations, fmt::format("Maximum iterations reached on {}", stackToString(_stack.data())));
	}

private:
	using StackOffset = Stack<Callback>::Offset;
	using StackDepth = Stack<Callback>::Depth;

	struct StackStats
	{
		StackStats(Stack<Callback> const& _stack, size_t _argsRegionSize)
		{
			histogram.reserve(_stack.size());
			histogramReachable.reserve(ReachableStackDepth);
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
			return static_cast<size_t>(ranges::count(targetStats.args, _slot));
		}

		bool stackAdmissible() const
		{
			return argsRegionIsCorrect() && distributionIsCorrect();
		}

		bool canBePopped(Slot const& _slot) const
		{
			bool enoughQuantity = stackStats.totalCount(_slot) > targetMinCount(_slot);
			return (!requiredInArgs(_slot) && enoughQuantity) || (requiredInArgs(_slot) && stackStats.reachableCount(_slot) > 0); // todo  || stack.canBeFreelyGenerated(_slot)?
		}

		bool offsetInTargetArgsRegion(StackOffset _offset) const
		{
			return _offset.value >= targetStats.targetSize - targetStats.args.size() && _offset.value < targetStats.targetSize;
		}

		Slot const& targetArg(StackOffset _targetOffset) const
		{
			return targetStats.args[_targetOffset.value - targetStats.tailSize];
		}

		bool isArgsCompatible(StackOffset _sourceOffset, StackOffset _targetOffset) const
		{
			if (_sourceOffset >= stack.size() || !offsetInTargetArgsRegion(_targetOffset))
				return false;
			auto const& arg = targetArg(_targetOffset);
			return arg.isJunk() || stack[_sourceOffset] == arg;
		}

		bool isSourceCompatible(StackOffset const& _sourceOffset1, StackOffset const& _sourceOffset2) const
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
		for (StackOffset sourceOffset{0u}; sourceOffset < _ops.stack.size() - (ReachableStackDepth - 1); ++sourceOffset.value)
		{
			// This slot needs to be moved into args and there is no tail slot of the same kind further up in the stack.
			auto const& slot = _ops.stack.slot(sourceOffset);
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
					for (StackOffset offset{sourceOffset.value + 1}; offset < _ops.stack.size(); ++offset.value)
					{
						if (_ops.stack[offset] == slot)
							return std::make_tuple(_ops.stack.size() - offset.value - 1 >= _ops.targetStats.args.size(), true);
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

				if (_ops.stack.dupReachable(sourceOffset))
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
						_ops.stack.swap(sourceOffset);
					}
					else
					{
						// we need more of the slot that is about to go out of reach, dup it
						_ops.stack.dup(sourceOffset);
						return true;
					}
				}
				else
				{
					// the slot we need something in the args region of is unreachable, try compressing the stack,
					// first looking at the top
					if (shrinkStack(_ops.stack, _ops))
						return true;

					// todo stack too deep of `slot`. :(
					return false;
				}
			}
		}
		return false;
	}

	static auto stackArgsRange(Stack<Callback> const& _stack, std::size_t const _tailSize)
	{
		return ranges::views::iota(std::min(_tailSize, _stack.size()), _stack.size()) | ranges::views::transform([](auto _i) { return StackOffset{_i}; });
	}

	static auto stackTailRange(Stack<Callback> const& _stack, std::size_t const _tailSize)
	{
		return ranges::views::iota(0u, std::min(_tailSize, _stack.size())) | ranges::views::transform([](auto _i) { return StackOffset{_i}; });
	}

	static auto stackRange(Stack<Callback> const& _stack)
	{
		return ranges::views::iota(0u, _stack.size()) | ranges::views::transform([&](auto _i) { return StackOffset{_i}; });
	}

	static auto stackSwapReachableRange(Stack<Callback> const& _stack)
	{
		return ranges::views::iota(0u, std::min(_stack.size(), ReachableStackDepth + 1)) | ranges::views::transform([&](auto _i) { return _stack.depthToOffset(StackDepth{_i}); }) | ranges::views::reverse;
	}

	static bool shrinkStack(Stack<Callback>& _stack, Ops const& _ops)
	{
		yulAssert(!_stack.empty(), "Stack is empty, can't shrink");

		StackOffset const stackTop{_stack.size() - 1};
		// pop top if it is junk (ie actual junk, not in args, not in live out)
		if (
			_stack[stackTop].isJunk() ||
			(!_ops.requiredInArgs(_stack[stackTop]) && !_ops.requiredInTail(_stack[stackTop]))
		)
		{
			_stack.pop();
			return true;
		}

		// swap top to suitable position, prioritizing args region
		{
			if (_ops.requiredInArgs(_stack[stackTop]))
			{
				for (StackOffset argsOffset: stackArgsRange(_stack, _ops.targetStats.tailSize))
					if (
						_stack.swapReachable(argsOffset) &&
						_ops.isArgsCompatible(stackTop, argsOffset) &&
						!_ops.isArgsCompatible(argsOffset, argsOffset)
					)
					{
						_stack.swap(argsOffset);
						return true;
					}
			}
			// we don't need it in args but in tail
			if (!_ops.requiredInArgs(_stack[stackTop]) && _ops.requiredInTail(_stack[stackTop]))
			{
				// if it's already in tail, pop
				if (_ops.stackStats.tailCount(_stack[stackTop]) > 1)
				{
					_stack.pop();
					return true;
				}

				// if we need it down there, try to swap down
				for (StackOffset tailOffset: stackTailRange(_stack, _ops.targetStats.tailSize))
					if (
						_stack.swapReachable(tailOffset) &&  // we can reach the offset
						!(_ops.requiredInTail(_stack[tailOffset]) && _ops.stackStats.tailCount(_stack[tailOffset]) <= 1)  // it's okay to swap the tail offset out
					)
					{
						_stack.swap(tailOffset);
						return true;
					}
			}
		}
		// pop junk
		for (StackOffset offset: stackSwapReachableRange(_stack))
			if (_stack[offset].isJunk())
			{
				if (offset != stackTop)
					_stack.swap(offset);
				_stack.pop();
				return true;
			}
		// pop something that can be freely generated except for literals
		for (StackOffset offset: stackSwapReachableRange(_stack))
			if (_stack.canBeFreelyGenerated(_stack[offset]) && !_stack[offset].isLiteralValueID())
			{
				if (offset != stackTop)
					_stack.swap(offset);
				_stack.pop();
				return true;
			}
		// pop anything that isn't in position and we have more than one of
		for (StackOffset offset: stackSwapReachableRange(_stack))
			if (_ops.stackStats.totalCount(_stack[offset]) > _ops.targetMinCount(_stack[offset]))
			{
				if (offset != stackTop)
					_stack.swap(offset);
				_stack.pop();
				return true;
			}
		// pop any literals we can find
		for (StackOffset offset: stackSwapReachableRange(_stack))
			if (_stack[offset].isLiteralValueID())
			{
				if (offset != stackTop)
					_stack.swap(offset);
				_stack.pop();
				return true;
			}
		return false;
	}

	static bool allNecessarySlotsReachableOrFinal(Ops& _ops)
	{
		// check that args are either in position or reachable
		for (StackOffset offset{_ops.targetStats.tailSize}; offset < _ops.targetStats.targetSize; ++offset.value)
			if (
				offset < _ops.stack.size() &&
				!_ops.isArgsCompatible(offset, offset) &&  // the slot isn't in place
				!_ops.stack.canBeFreelyGenerated(_ops.targetArg(offset))  // we can't just push it
			)
			{
				// find first occurrence of the slot
				std::optional<StackDepth> depth = _ops.stack.findSlotDepth(_ops.targetArg(offset));
				// it must exist according to shuffle admissibility criteria
				yulAssert(depth);
				if (!_ops.stack.swapReachable(*depth))
					return false;
			}
		// distribution check: all we have to dup can be duped
		for (StackOffset const offset: stackRange(_ops.stack))
			// we don't have enough of the slot
			if (
				_ops.stackStats.totalCount(_ops.stack[offset]) < _ops.targetMinCount(_ops.stack[offset]) &&
				!_ops.stack.dupReachable(offset)
			)
			{
				// find first occurrence of the slot
				std::optional<StackDepth> depth = _ops.stack.findSlotDepth(_ops.stack[offset]);
				// it must exist
				yulAssert(depth);
				if (!_ops.stack.swapReachable(*depth))
					return false;
			}

		return true;
	}

	static bool fixTailSlot(Ops const& _ops)
	{
		yulAssert(_ops.stack.size() <= _ops.targetStats.targetSize, "this method assumes that the stack isn't too large");
		for (StackOffset offset: stackArgsRange(_ops.stack, _ops.targetStats.tailSize) | ranges::views::reverse)
			if (
				_ops.stack.swapReachable(offset) &&  // if we can swap it up
				_ops.requiredInTail(_ops.stack[offset]) &&  // if we need the slot in tail
				_ops.stackStats.tailCount(_ops.stack[offset]) == 0  // if we don't have the slot in tail right now
			)
			{
				// find the lowest swappable slot in tail that needs to go to args, swap
				for (StackOffset tailOffset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (
						_ops.stack.swapReachable(tailOffset) &&  // we can swap that deep
						(!_ops.requiredInTail(_ops.stack[tailOffset]) || _ops.stackStats.tailCount(_ops.stack[tailOffset]) > 1) &&  // dont need it in tail or it's available more than once
						_ops.requiredInArgs(_ops.stack[tailOffset]) &&  // we need the tail offset slot in args
						_ops.targetArgsCount(_ops.stack[tailOffset]) > _ops.stackStats.argsCount(_ops.stack[tailOffset])  // we don't already have enough of it in args
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_ops.stack.size() - 1})
							_ops.stack.swap(offset);
						// swap offset slot down into tail
						_ops.stack.swap(tailOffset);
						return true;
					}
				// find the lowest swappable slot in tail that can be popped but is no literal, swap
				for (StackOffset tailOffset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (
						_ops.stack.swapReachable(tailOffset) &&
						_ops.stack.canBeFreelyGenerated(_ops.stack[tailOffset]) &&
						!_ops.stack[tailOffset].isLiteralValueID()
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_ops.stack.size() - 1})
							_ops.stack.swap(offset);
						// swap offset slot down into tail
						_ops.stack.swap(tailOffset);
						return true;
					}
				// find the lowest swappable slot in tail that is a literal, swap
				for (StackOffset tailOffset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (
						_ops.stack.swapReachable(tailOffset) &&
						_ops.stack[tailOffset].isLiteralValueID()
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_ops.stack.size() - 1})
							_ops.stack.swap(offset);
						// swap offset slot down into tail
						_ops.stack.swap(tailOffset);
						return true;
					}
			}
		return false;
	}

	static bool dupDeepestRelevantTailSlot(Ops& _ops)
	{
		auto& stack = _ops.stack;

		// dup up the deepest slot that is required in args (or compress if unreachable)
		for (StackOffset offset: stackRange(_ops.stack))
		{
			// if we need the slot in args and there's no slot of the same kind further up
			if (
				_ops.requiredInArgs(_ops.stack[offset]) &&
				std::find(_ops.stack.begin() + offset.value + 1, _ops.stack.end(), _ops.stack[offset]) == _ops.stack.end()
			)
			{
				// dup if we can
				if (_ops.stack.dupReachable(offset))
				{
					_ops.stack.dup(offset);
					return true;
				}

				// try to compress
				if (shrinkStack(_ops.stack, _ops))
					return true;

				// todo stack too deep handling, the slot at offset is required in args but we can't reach it
				yulAssert(false);
			}
		}
		return false;
	}

	static std::optional<StackOffset> suitableArgsOffsetFor(Ops const& _ops, StackOffset const& _outOfPositionOffset)
	{
		yulAssert(!_ops.isArgsCompatible(_outOfPositionOffset, _outOfPositionOffset));
		for (StackOffset offset: stackArgsRange(_ops.stack, _ops.targetStats.tailSize))
			if (
				offset != _outOfPositionOffset &&
				_ops.stack.swapReachable(offset) &&
				_ops.isArgsCompatible(_outOfPositionOffset, offset) &&
				!_ops.isArgsCompatible(offset, offset)
			)
				return offset;

		return std::nullopt;
	}

	static bool fixArgsSlot(Ops const& _ops)
	{
		yulAssert(_ops.stack.size() <= _ops.targetStats.targetSize, "this method assumes that the stack isn't too large");
		if (_ops.stack.size() <= _ops.targetStats.tailSize || _ops.stack.empty())
			return false;

		StackOffset const stackTop{_ops.stack.size() - 1};
		// if the stack top isn't where it likes to be right now, try to put it somewhere more sensible
		if (!_ops.isArgsCompatible(stackTop, stackTop))
		{
			// if the stack top should go into the tail but isn't there yet and we have enough of it in args
			if (
				_ops.requiredInTail(_ops.stack[stackTop]) &&
				_ops.stackStats.tailCount(_ops.stack[stackTop]) == 0 &&
				_ops.stackStats.argsCount(_ops.stack[stackTop]) > _ops.targetArgsCount(_ops.stack[stackTop])
			)
			{
				// try swapping it with something in the tail that also fixes the top
				for (StackOffset offset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (_ops.stack.swapReachable(offset) && _ops.isArgsCompatible(offset, stackTop))
					{
						_ops.stack.swap(offset);
						return true;
					}
				// otherwise try swapping it with something that needs to go into args
				for (StackOffset offset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (_ops.stack.swapReachable(offset) && _ops.stackStats.argsCount(_ops.stack[offset]) < _ops.targetArgsCount(_ops.stack[offset]))
					{
						_ops.stack.swap(offset);
						return true;
					}
				// otherwise try swapping it with something that can be popped
				for (StackOffset offset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (_ops.stack.swapReachable(offset) && _ops.stack.canBeFreelyGenerated(_ops.stack[offset]) && !_ops.stack[offset].isLiteralValueID())
					{
						_ops.stack.swap(offset);
						return true;
					}
				// otherwise try swapping it with a literal
				for (StackOffset offset: stackTailRange(_ops.stack, _ops.targetStats.tailSize))
					if (_ops.stack.swapReachable(offset) && _ops.stack[offset].isLiteralValueID())
					{
						_ops.stack.swap(offset);
						return true;
					}
			}
			// try finding a slot that is compatible with the top and also admits the current top:
			//		- could be that the top slot is used elsewhere in the args
			//		- could be that the top slot is something that is only required in the tail
			for (StackOffset offset: stackArgsRange(_ops.stack, _ops.targetStats.tailSize))
				if (
					offset != stackTop &&
					_ops.stack.swapReachable(offset) &&
					_ops.isArgsCompatible(offset, stackTop) &&
					_ops.isArgsCompatible(stackTop, offset)
				)
				{
					_ops.stack.swap(offset);
					return true;
				}

			// try finding a slot in args that wants to have the top, swap that
			for (StackOffset offset: stackArgsRange(_ops.stack, _ops.targetStats.tailSize))
				if (
					offset != stackTop &&
					_ops.stack.swapReachable(offset) &&
					!_ops.isArgsCompatible(offset, offset) &&
					_ops.isArgsCompatible(stackTop, offset)
				)
				{
					_ops.stack.swap(offset);
					return true;
				}
		}

		// swap up any slot in args that is out of position and has a slot available in args that it can occupy
		for (StackOffset offset: stackArgsRange(_ops.stack, _ops.targetStats.tailSize))
			if (
				_ops.stack.swapReachable(offset) &&
				!_ops.isArgsCompatible(offset, stackTop) && // we wouldn't just be swapping identical things
				!_ops.isArgsCompatible(offset, offset) // the slot at offset isn't final
			)
			{
				if (auto targetOffset = suitableArgsOffsetFor(_ops, offset))
				{
					if (offset != stackTop)
						// swap up slot at offset
						_ops.stack.swap(offset);
					// bring slot at offset into fixed position
					_ops.stack.swap(*targetOffset);
					return true;
				}
			}

		// if we're at size and would have to push or dup something to satisfy args, try shrinking
		if (_ops.stack.size() == _ops.targetStats.targetSize)
		{
			for (auto const& arg: _ops.targetStats.args)
				if (_ops.stackStats.totalCount(arg) < _ops.targetMinCount(arg))
					if (shrinkStack(_ops.stack, _ops))
						return true;

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

		if (_stack.size() > _targetStats.targetSize)
		{
			yulAssert(shrinkStack(_stack, ops), "Couldn't shrink stack to target size");
			return true;
			// todo: in the future we'll want stack too deep handling here and
			//		 dup up the args if possible or mload them by explicitly calling _stack.reportStackTooDeep(arg)
		}
		yulAssert(_stack.size() <= _targetStats.targetSize, "I1 violated: Stack size too large");
		if (!allNecessarySlotsReachableOrFinal(ops))
		{
			// if we need something in the tail, try swapping it down there, there must be a spot
			// that can be swapped out (although it might be unreachable in which case we'll try to fix args
			// and/or compress)
			if (fixTailSlot(ops))
				return true;

			// if the stack reaches into the args region try fixing a slot in there
			if (_stack.size() >= _targetStats.tailSize && fixArgsSlot(ops))
				return true;
			if (shrinkStack(_stack, ops))
				return true;
			// todo: in the future we'll want stack too deep handling here and
			//		 dup up the args if possible or mload them by explicitly calling _stack.reportStackTooDeep(arg)
			yulAssert(_stack.size() < _targetStats.targetSize);
		}

		if (_stack.size() < _targetStats.tailSize)
		{
			// if something is on the verge of going out of scope by duping something, dup that first
			if (dupDeepSlotIfRequired(ops, _generateJunk))
				return true;

			// dup up the deepest slot that needs to go into args so we avoid having to fish it back up later
			if (dupDeepestRelevantTailSlot(ops))
				return true;

			// todo we might also just dup up the deepest live-out variable instead for subsequent
			//		passes and only if we can't push0
			// meh
			_stack.push(Slot::makeJunk());
			return true;
		}

		// we are now in a position that we only have to potentially dup up args and/or fix the existing args slots
		yulAssert(_targetStats.tailSize <= _stack.size() && _stack.size() <= _targetStats.targetSize);

		// if there are no args, we should be done now
		if (_targetStats.args.empty())
		{
			yulAssert(ops.stackAdmissible());
			return false;
		}

		// of the existing args, can we improve the situation?
		if (fixArgsSlot(ops))
			return true;

		if (fixTailSlot(ops))
			return true;

		// dup up whatever is missing
		if (_stack.size() < _targetStats.targetSize)
		{
			if (dupDeepSlotIfRequired(ops, _generateJunk))
				return true;
			{
				StackOffset const targetOffset{_stack.size()};
				if (ops.stackStats.totalCount(ops.targetArg(targetOffset)) < ops.targetMinCount(ops.targetArg(targetOffset)))
				{
					auto const sourceDepth = _stack.findSlotDepth(ops.targetArg(targetOffset));
					if (!sourceDepth)
					{
						_stack.push(ops.targetArg(targetOffset));
						return true;
					}

					if (!_stack.dupReachable(*sourceDepth))
						yulAssert(false, fmt::format("todo: stack too deep handling, couldn't dup up arg {}", slotToString(ops.targetArg(_stack.depthToOffset(*sourceDepth)))));
					_stack.dup(*sourceDepth);
					return true;
				}
			}

			// if we can't directly produce targetOffset, take the deepest arg that we don't have enough of and dup/push that
			for (StackOffset offset{ops.targetStats.tailSize}; offset < ops.targetStats.targetSize; ++offset.value)
			{
				Slot const& arg = ops.targetArg(offset);
				if (ops.stackStats.totalCount(arg) < ops.targetMinCount(arg))
				{
					if (auto sourceDepth = ops.stack.findSlotDepth(arg))
					{
						if (ops.stack.dupReachable(*sourceDepth))
						{
							ops.stack.dup(*sourceDepth);
							return true;
						}
						yulAssert(false, "stack too deep handling");
					}
					yulAssert(_stack.canBeFreelyGenerated(arg));
					_stack.push(arg);
					return true;
				}
			}

			_stack.push(Slot::makeJunk());
			return true;
		}

		yulAssert(_stack.size() == _targetStats.targetSize);

		StackOffset stackTopOffset{_stack.size() - 1};

		if (fixArgsSlot(ops))
			return true;

		// If we find a lower slot that is out of position, but also compatible with the top, swap that up.
		for (StackOffset const offset: stackSwapReachableRange(_stack))
			if (
				!ops.isArgsCompatible(offset, offset) &&
				!ops.isSourceCompatible(offset, stackTopOffset) &&
				ops.isArgsCompatible(offset, stackTopOffset)
			)
			{
				_stack.swap(offset);
				return true;
			}

		// Swap up any reachable slot that is still out of position.
		for (StackOffset const offset: stackSwapReachableRange(_stack))
			if (_stack.offsetToDepth(offset) < _targetStats.args.size())
			{
				if (
					ops.offsetInTargetArgsRegion(offset) &&
					!ops.isArgsCompatible(offset, offset) &&
					!ops.isSourceCompatible(offset, stackTopOffset)
				)
				{
					_stack.swap(offset);
					return true;
				}
			}
			else
			{
				if (
					ops.requiredInArgs(_stack[offset]) &&
					ops.stackStats.argsCount(_stack[offset]) < ops.targetArgsCount(_stack[offset]) &&
					!ops.isSourceCompatible(offset, stackTopOffset)
				)
				{
					_stack.swap(offset);
					return true;
				}
			}

		if (ops.stackAdmissible())
			return false;

		// We are in a stack-too-deep situation and try to reduce the stack size.
		if (shrinkStack(_stack, ops))
			return true;

		yulAssert(false, "reached final and forbidden state");
	}
};

}
