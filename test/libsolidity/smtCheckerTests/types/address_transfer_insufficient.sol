contract C
{
	function f(address payable a, address payable b) public {
		require(a.balance == 0);
		bool success;
		(success, ) = a.call{value:600}("");
		(success, ) = b.call{value:1000}("");
		// Fails since a == this is possible.
		assert(a.balance == 600);
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (236-260): CHC: Assertion violation happens here.\nCounterexample:\n\na = 0x0\nb = 0x0\nsuccess = false\n\nTransaction trace:\nC.constructor()\nC.f(0x0, 0x0)\n    a.call{value:600}("") -- untrusted external call\n    b.call{value:1000}("") -- untrusted external call
