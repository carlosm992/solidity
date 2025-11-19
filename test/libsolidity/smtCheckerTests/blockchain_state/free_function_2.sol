function l(address payable a) {
	(bool success, ) = a.call{value: 1}("");
	require(success);
}

contract C {
	uint x;
	function f(address payable a) public payable {
		require(msg.value > 1);
		uint b1 = address(this).balance;
		require(a != address(this));
		l(a);
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
// Warning 6328: (303-319): CHC: Assertion violation happens here.\nCounterexample:\nx = 0\na = 0x0\nb1 = 38\nb2 = 37\n\nTransaction trace:\nC.constructor()\nState: x = 0\nC.f(0x0){ msg.value: 10 }\n    l(0x0) -- internal call\n        a.call{value: 1}("") -- untrusted external call
// Warning 6328: (338-358): CHC: Assertion violation happens here.
// Info 1391: CHC: 2 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
