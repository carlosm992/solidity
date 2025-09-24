#pragma once

#include <libyul/backends/evm/SSACFGJunkBlockFinder.h>
#include <libyul/backends/evm/SSACFGLiveness.h>
#include <libyul/backends/evm/SSACFGStackLayout.h>
#include <libyul/backends/evm/SSAControlFlowGraph.h>

namespace solidity::yul::ssa
{

class ReversePhiFunctionTransform;

class BackwardStackLayoutGenerator
{

public:
	using Slot = StackSlot;
	struct StackManipulationCallbacks
	{
		static bool writeCallbackOutput;
		size_t numOps = 0;
		using Slot = Slot;
		void swap(size_t _depth)
		{
			++numOps;
			if (writeCallbackOutput)
				std::cout << "SWAP" << _depth << std::flush << " + ";
		}
		void dup(size_t _depth)
		{
			++numOps;
			if (writeCallbackOutput)
				std::cout << "DUP" << _depth << std::flush << " + ";
		}
		void push(Slot const& _slot)
		{
			++numOps;
			if (writeCallbackOutput)
				std::cout << "PUSH " << slotToString(_slot) << std::flush << " + ";
		}
		void pop()
		{
			++numOps;
			if (writeCallbackOutput)
				std::cout << "POP" << std::flush << " + ";
		}
	};
	using StackType = Stack<Slot, StackManipulationCallbacks>;
	using StackData = StackType::Data;

	static SSACFGStackLayout generate(SSACFGLiveness const& _cfgLiveness, SSACFGJunkBlockFinder const& _junkBlockFinder);

private:
	static void handlePhiFunctions(StackData& _stackData, ReversePhiFunctionTransform const& _phiInverse, SSACFGLiveness::LivenessData const& _liveness);

	explicit BackwardStackLayoutGenerator(SSACFGLiveness const& _liveness, SSACFGJunkBlockFinder const& _junkBlockFinder);

	void visitBlocks();

	SSACFGStackLayout const& computeStackLayout();
	void defineStackIn(SSACFG::BlockId const& _blockId);
	void visitBlock(SSACFG::BlockId const& _blockId);

	SSACFGLiveness const& m_liveness;
	SSACFG const& m_cfg;

	std::vector<bool> m_blockIsGenerated;
	std::vector<bool> m_blockHasStackInDefined;
	SSACFGJunkBlockFinder const& m_junkBlockFinder;

	SSACFGStackLayout m_stackLayout;
};

}
