contract C
{
	function f(uint x, address payable a, address payable b) public {
		require(a != b);
		require(x == 100);
		require(x == a.balance);
		require(a.balance == b.balance);
		bool success;
		(success, ) = a.call{value:600}("");
		(success, ) = b.call{value:100}("");
		// Fails since a == this is possible.
		assert(a.balance > b.balance);
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (318-347): CHC: Assertion violation happens here.\nCounterexample:\n\nx = 100\na = 0x0\nb = 0xffffffffffffffffffffffffffffffffffffdf52\nsuccess = false\n\nTransaction trace:\nC.constructor()\nC.f(100, 0x0, 0xffffffffffffffffffffffffffffffffffffdf52)\n    a.call{value:600}("") -- untrusted external call\n    b.call{value:100}("") -- untrusted external call
