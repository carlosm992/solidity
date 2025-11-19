library L {
	function l(address payable a) internal {
		require(a != address(this));
		bool success;
		(success, ) = a.call{value: 1}("");
	}
}

contract C {
	using L for address payable;
	uint x;
	function f(address payable a) public payable {
		require(msg.value > 1);
		uint b1 = address(this).balance;
		a.l();
		uint b2 = address(this).balance;
		assert(b1 == b2); // should fail
		assert(b1 == b2 + 1); // should hold
		assert(x == 0); // should hold
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (352-368): CHC: Assertion violation happens here.
// Warning 6328: (387-407): CHC: Assertion violation happens here.
// Info 1391: CHC: 2 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
