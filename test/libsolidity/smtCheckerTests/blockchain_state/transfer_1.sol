contract C {
	function f(address payable a) public {
		require(address(this).balance > 1000);
		bool success;
		(success, ) = a.call{value:666}("");
		assert(address(this).balance > 100);
		// Fails.
		assert(address(this).balance > 500);
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (202-237): CHC: Assertion violation happens here.\nCounterexample:\n\na = 0x0\nsuccess = false\n\nTransaction trace:\nC.constructor()\nC.f(0x0)\n    a.call{value:666}("") -- untrusted external call
// Info 1391: CHC: 1 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
