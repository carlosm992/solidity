#pragma once
#include "libyul/backends/evm/SSACFGStackLayout.h"
#include "libyul/backends/evm/SSACFGStackShuffler.h"
#include "libyul/backends/evm/ssa/SSACFG.h"


#include <optional>

namespace solidity::yul::ssa
{

/// If Block `_from` -> Block `_to` and `_to` has phi functions `v_k := phi(..., _from => v_i, ...)`, this transform
/// pulls values `v_k` back to `v_i`.
class ReversePhiFunctionTransform
{
public:
	ReversePhiFunctionTransform() = default;
	ReversePhiFunctionTransform(SSACFG const& _cfg, SSACFG::BlockId _from, SSACFG::BlockId _to);

	/// whether the transform is guaranteed to be a no-op, ie, there is no phi function in `_to`
	bool noOp() const;
	SSACFG::ValueId operator()(SSACFG::ValueId _valueId) const;

	// if we have a variant with value id contained in the type union
	template<typename... T>
	std::variant<T...> operator()(std::variant<T...> const& _someSlot) const
	{
		static bool constexpr variantContainsValueId = std::disjunction_v<std::is_same<SSACFG::ValueId, T>...>;
		static_assert(variantContainsValueId);
		if (auto valueId = std::get_if<SSACFG::ValueId>(&_someSlot))
			return (*this)(*valueId);
		return _someSlot;
	}

	std::map<SSACFG::ValueId, SSACFG::ValueId> const& data() const
	{
		return m_reversePhiMap;
	}

private:
	std::map<SSACFG::ValueId, SSACFG::ValueId> m_reversePhiMap = {};
};

template<typename Stack>
void shuffleStackExact(Stack& _stack, typename Stack::Data const& _target, SSACFG const& _cfg, std::optional<SSACFG::Edge> _edge = std::nullopt)
{
	auto const transform = _edge ? ReversePhiFunctionTransform(_cfg, _edge->from, _edge->to) : ReversePhiFunctionTransform{};
	auto const transformedTarget = [&]
	{
		if (transform.noOp())
			return _target;
		return _target | ranges::views::transform(transform) | ranges::to<std::vector>;
	}();
	DanielShuffler<Stack>::shuffle(
		_stack,
		{}, transformedTarget
	);
}

}
