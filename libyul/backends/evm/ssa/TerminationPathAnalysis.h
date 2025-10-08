/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include <libyul/backends/evm/ssa/SSACFGTopologicalSort.h>
#include <libyul/backends/evm/ssa/SSACFG.h>

#include <cstdint>
#include <vector>

namespace solidity::yul::ssa
{

/// Identifies blocks where stack balance constraints can be relaxed.
/// These are blocks that either terminate execution or are bridge vertices
/// with no path back to a function return. In such blocks, adding extra stack slots ("junk")
/// doesn't affect correctness since the stack state never needs to reconcile with other paths.
class TerminationPathAnalysis
{
public:
	explicit TerminationPathAnalysis(SSACFG const& _cfg, ForwardSSACFGTopologicalSort const& _topologicalSort);
	bool blockAllowsAdditionOfJunk(SSACFG::BlockId const& _blockId) const;
private:
	std::vector<std::uint8_t> m_blockAllowsJunk;
};

}
