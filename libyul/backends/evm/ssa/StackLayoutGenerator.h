#pragma once

#include "libyul/backends/evm/ControlFlow.h"


#include <libyul/backends/evm/SSACFGJunkBlockFinder.h>
#include <libyul/backends/evm/SSACFGLiveness.h>
#include <libyul/backends/evm/SSACFGStackLayout.h>
#include <libyul/backends/evm/SSAControlFlowGraph.h>

namespace solidity::yul::ssa
{

class StackLayoutGenerator
{

public:
	using Slot = StackSlot;
#if !defined(NDEBUG)
	struct StackManipulationCallbacks
	{
		static bool writeCallbackOutput;
		using Slot = Slot;
		static void swap(size_t _depth)
		{
			if (writeCallbackOutput)
				std::cout << "SWAP" << _depth << std::flush << " + ";
		}
		static void dup(size_t _depth)
		{
			if (writeCallbackOutput)
				std::cout << "DUP" << _depth << std::flush << " + ";
		}
		static void push(Slot const& _slot)
		{
			if (writeCallbackOutput)
				std::cout << "PUSH " << slotToString(_slot) << std::flush << " + ";
		}
		static void pop()
		{
			if (writeCallbackOutput)
				std::cout << "POP" << std::flush << " + ";
		}
	};
#else
	using StackManipulationCallbacks = NoOpStackManipulationCallbacks<StackSlot>;
#endif
	using StackType = Stack<Slot, StackManipulationCallbacks>;
	using StackData = StackType::Data;

	static ControlFlowLayout generate(ControlFlowLiveness const& _controlFlowLiveness);
	static SSACFGStackLayout generate(SSACFGLiveness const& _cfgLiveness);

private:
	static void handlePhiFunctions(StackData& _stackData, ReversePhiFunctionTransform const& _phiInverse, SSACFGLiveness::LivenessData const& _liveness);

	explicit StackLayoutGenerator(SSACFGLiveness const& _liveness);

	SSACFGStackLayout const& computeStackLayout();
	void defineStackIn(SSACFG::BlockId const& _blockId);
	void visitBlock(SSACFG::BlockId const& _blockId);

	SSACFGLiveness const& m_liveness;
	SSACFG const& m_cfg;

	std::vector<bool> m_blockIsGenerated;
	std::vector<bool> m_blockHasStackInDefined;
	SSACFGJunkBlockFinder m_junkBlockFinder;

	SSACFGStackLayout m_stackLayout;
};

}
