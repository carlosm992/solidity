contract C
{
	function f(address payable a) public {
		require(1000 == address(this).balance);
		require(100 == a.balance);
		bool success;
		(success, ) = a.call{value:600}("");
		// a == this is not possible because address(this).balance == 1000
		// and a.balance == 100,
		// so this should hold in CHC, ignoring the transfer revert.
		assert(a.balance == 700);
	}
}
// ====
// SMTEngine: all
// ----
// Warning 6328: (340-364): CHC: Assertion violation happens here.\nCounterexample:\n\na = 0x20ae\nsuccess = false\n\nTransaction trace:\nC.constructor()\nC.f(0x20ae)\n    a.call{value:600}("") -- untrusted external call
