contract C
{
	function f(address payable a) public {
		uint x = 100;
		require(x == a.balance);
		bool success;
		(success, ) = a.call{value:600}("");
		// This fails since a == this is possible.
		assert(a.balance == 700);
	}
}
// ====
// SMTEngine: all
// ----
// Warning 6328: (198-222): CHC: Assertion violation happens here.\nCounterexample:\n\na = 0x0\nx = 100\nsuccess = false\n\nTransaction trace:\nC.constructor()\nC.f(0x0)\n    a.call{value:600}("") -- untrusted external call
