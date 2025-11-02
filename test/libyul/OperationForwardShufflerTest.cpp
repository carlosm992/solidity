#include <libyul/backends/evm/ssa/OperationForwardShuffler.h>

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace
{
using Liveness = solidity::yul::ssa::LivenessAnalysis::LivenessData;
using Slot = solidity::yul::ssa::StackSlot;
using ValueId = solidity::yul::ssa::SSACFG::ValueId;

/// Parse a value ID token like "v172", "phi109", "lit7"
/// Returns std::nullopt for "JUNK"
std::optional<ValueId> parseValueToken(std::string const& token)
{
	if (token == "JUNK")
		return std::nullopt;

	if (token.starts_with("v"))
	{
		size_t num = std::stoull(token.substr(1));
		return ValueId::makeVariable(num);
	}

	if (token.starts_with("phi"))
	{
		size_t num = std::stoull(token.substr(3));
		return ValueId::makePhi(num);
	}

	if (token.starts_with("lit"))
	{
		size_t num = std::stoull(token.substr(3));
		return ValueId::makeLiteral(num);
	}
	throw std::runtime_error("Unknown token: " + token);
}

/// Parse a string like "[v172, phi109, lit7, JUNK]" into Stack::Data
std::vector<Slot> parseStackData(std::string_view _input)
{
	std::vector<Slot> result;
	std::string input(_input);

	// Remove whitespace
	input.erase(std::ranges::remove_if(input, ::isspace).begin(), input.end());

	// Remove brackets
	if (!input.empty() && input.front() == '[')
		input.erase(input.begin());
	if (!input.empty() && input.back() == ']')
		input.pop_back();

	// Split by comma
	std::stringstream ss(input);
	std::string token;

	while (std::getline(ss, token, ','))
	{
		if (token.empty())
			continue;

		if (auto valueId = parseValueToken(token))
			result.push_back(Slot::makeValueID(*valueId));
		else
			result.push_back(Slot::makeJunk());
	}

	return result;
}

/// Parse liveness like "[phi109, phi150, v172]"
/// Returns Liveness with reference count 1 for each value
Liveness parseLiveness(std::string_view _input)
{
	std::vector<std::pair<ValueId, uint32_t>> liveCounts;
	std::string input(_input);

	// Remove whitespace
	input.erase(std::ranges::remove_if(input, ::isspace).begin(), input.end());

	// Remove brackets
	if (!input.empty() && input.front() == '[')
		input.erase(input.begin());
	if (!input.empty() && input.back() == ']')
		input.pop_back();

	// Split by comma
	std::stringstream ss(input);
	std::string token;

	while (std::getline(ss, token, ','))
	{
		if (token.empty())
			continue;

		auto valueId = parseValueToken(token);
		if (valueId)
			liveCounts.emplace_back(*valueId, 1);  // Default reference count of 1
	}

	return Liveness(liveCounts.begin(), liveCounts.end());
}

struct StackManipulationCallbacks
{
	size_t numOps = 0;
	void swap(size_t _depth)
	{
		++numOps;
		std::cout << "SWAP" << _depth << std::flush << " + ";
	}
	void dup(size_t _depth)
	{
		++numOps;
		std::cout << "DUP" << _depth << std::flush << " + ";
	}
	void push(Slot const& _slot)
	{
		++numOps;
		std::cout << "PUSH " << slotToString(_slot) << std::flush << " + ";
	}
	void pop()
	{
		++numOps;
		std::cout << "POP" << std::flush << " + ";
	}
};
using Stack = solidity::yul::ssa::Stack<StackManipulationCallbacks>;
}

namespace solidity::yul::test
{
BOOST_AUTO_TEST_SUITE(OperationForwardShufflerTest)

BOOST_AUTO_TEST_CASE(TestCycle)
{
	Stack::Data data = parseStackData("[JUNK, v172, v172, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, phi109, JUNK, phi150, JUNK, lit7, v172, lit2, v174, lit0, v169, JUNK]");
	Stack::Data args = parseStackData("[lit7, v172, lit2, v172, lit0, v169, v174]");
	Liveness liveness = parseLiveness("[phi109, phi150, v172]");

	Stack stack(data, {});
	ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 23, false);
}

BOOST_AUTO_TEST_SUITE_END()
}
