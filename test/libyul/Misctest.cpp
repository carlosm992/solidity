#include "libyul/backends/evm/ssa/OperationForwardShuffler.h"


#include <boost/test/unit_test.hpp>

#include <libyul/backends/evm/SSACFGLiveness.h>
#include <libyul/backends/evm/SSACFGStack.h>
#include <libyul/backends/evm/SSACFGStackShuffler.h>

#include <range/v3/view/concat.hpp>

namespace
{

using SourceSlot = solidity::yul::ssa::StackSlot;
struct PrintCallback
{
	PrintCallback(std::function<void()> _callback): callback(_callback) {}

	using Slot = SourceSlot;
	void swap(size_t _depth)
	{
		++numOps;
		fmt::print("swap {}: ", _depth);
		callback();
	}
	void dup(size_t _depth)
	{
		++numOps;
		fmt::print("dup {}: ", _depth);
		callback();
	}
	void push(Slot const&)
	{
		++numOps;
		fmt::print("push: ");
		callback();
	}
	void pop()
	{
		++numOps;
		fmt::print("pop: ");
		callback();
	}

	size_t numOps{};
	std::function<void()> callback;
};

struct SlotCanBeFreelyGenerated
{
	using Slot = SourceSlot;
	bool operator()(Slot const& _slot) const
	{
		return canBeFreelyGenerated(_slot);
	}

	solidity::yul::ssa::SlotCanBeFreelyGenerated<solidity::yul::ssa::StackSlot> canBeFreelyGenerated;
};

using TestStack = solidity::yul::ssa::Stack<SourceSlot, PrintCallback, SlotCanBeFreelyGenerated>;



}

namespace solidity::yul::test
{
BOOST_AUTO_TEST_SUITE(MiscTest)

BOOST_AUTO_TEST_CASE(yo)
{
	auto cfg = std::make_unique<SSACFG>();
	cfg->debugData = langutil::DebugData::create();
	cfg->entry = cfg->makeBlock(langutil::DebugData::create());
	cfg->block(cfg->entry).exit = SSACFG::BasicBlock::MainExit{};

	auto const v0 = cfg->newVariable({0});
	auto const v1 = cfg->newVariable({0});
	auto const v2 = cfg->newVariable({0});
	auto const v3 = cfg->newVariable({0});
	auto const v4 = cfg->newVariable({0});
	auto const v5 = cfg->newVariable({0});
	auto const v6 = cfg->newVariable({0});
	auto const v19 = cfg->newVariable({0});
	auto const v98 = cfg->newVariable({0});
	auto const v99 = cfg->newVariable({0});
	auto const v187 = cfg->newVariable({0});
	auto const v188 = cfg->newVariable({0});
	auto const lit = cfg->newLiteral(langutil::DebugData::create(), 42);

	std::shared_ptr<TestStack> stack;
	std::vector<SourceSlot> slots {v0, ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{}, v19};
	PrintCallback callback([&stack, &cfg]
	{
		if (stack)
			std::cout << ssa::stackToString(stack->data(), *cfg) << std::endl;
	});
	SlotCanBeFreelyGenerated canBeFreelyGenerated{.canBeFreelyGenerated = {cfg.get()}};
	stack = std::make_shared<TestStack>(slots, callback, canBeFreelyGenerated);

	std::cout << "--- start shuffle ---" << std::endl;
	SSACFGLiveness::LivenessData liveness ({{v0, 1}, {v19, 1}});
	ssa::OperationForwardShuffler<TestStack>::shuffle(*stack, {lit, v19}, liveness, false);
	std::cout << "--- fin ---" << std::endl;
	std::cout << ssa::stackToString(stack->data(), *cfg) << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}
