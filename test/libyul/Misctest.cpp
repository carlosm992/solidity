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

	auto const v1 = cfg->newVariable({0});
	auto const v2 = cfg->newVariable({0});
	auto const v3 = cfg->newVariable({0});
	auto const v4 = cfg->newVariable({0});
	auto const v5 = cfg->newVariable({0});
	auto const v6 = cfg->newVariable({0});
	auto const v19 = cfg->newVariable({0});
	auto const v98 = cfg->newVariable({0});
	auto const v99 = cfg->newVariable({0});
	auto const v58 = cfg->newVariable({0});
	auto const v59 = cfg->newVariable({0});
	auto const v67 = cfg->newVariable({0});
	auto const v68 = cfg->newVariable({0});
	auto const v76 = cfg->newVariable({0});
	auto const v77 = cfg->newVariable({0});
	auto const v79 = cfg->newVariable({0});
	auto const v188 = cfg->newVariable({0});
	auto const lit = cfg->newLiteral(langutil::DebugData::create(), 42);

	std::shared_ptr<TestStack> stack;
	PrintCallback callback([&stack, &cfg]
	{
		if (stack)
			std::cout << ssa::stackToString(stack->data(), *cfg) << std::endl;
	});
	SlotCanBeFreelyGenerated canBeFreelyGenerated{.canBeFreelyGenerated = {cfg.get()}};

	FunctionCall funDeposit {
		.debugData = nullptr,
		.functionName = Identifier{nullptr, YulString{"fun_deposit"}},
		.arguments = {}
	};

	std::vector<SourceSlot> slots {
		ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{}, ssa::JunkSlot{},
		v58, v59, ssa::JunkSlot{}, v67, v68, ssa::JunkSlot{}, v76, v77, v79
	};
	stack = std::make_shared<TestStack>(slots, callback, canBeFreelyGenerated);

	//   fun_deposit([JUNK x 6, v58, v59, JUNK, v67, v68, JUNK, v76, v77, v79] -> { [] } + [ReturnLabel[fun_deposit], v79, v77, v76, v68, v67, v59, v58])

	std::cout << fmt::format("--- start shuffle with {} ---", ssa::stackToString(stack->data(), *cfg)) << std::endl;
	//SSACFGLiveness::LivenessData liveness; // ({{v0, 1}, {v19, 1}});
	SSACFGLiveness::LivenessData liveness ({{v79, 1}, {v68, 1}});
	//TestStack::Data args{ssa::FunctionReturnLabel{&funDeposit}, v79, v77, v76, v68, v67, v59, v58};
	TestStack::Data args{v79, v68};
	{
		auto const t = ranges::views::transform([&](TestStack::Slot const& k) { return ssa::slotToString(k, *cfg); });
		auto r = liveness | ranges::views::keys | t;
		std::cout
			<< ">>> target: "
			<< fmt::format("{{ {} }} + ", fmt::join(r, ", "))
			<< fmt::format("[ {} ]\n", fmt::join(args | t, ", "));
	}
	ssa::OperationForwardShuffler<TestStack>::shuffle(*stack, args, liveness, true);
	std::cout << "--- fin ---" << std::endl;
	std::cout << ssa::stackToString(stack->data(), *cfg) << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}
