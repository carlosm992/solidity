contract C {
	constructor() payable {
		require(msg.value > 100);
	}
	function f(address payable _a, uint _v) public {
		require(_v < 10);
		(bool success, ) = _a.call{value: _v}("");
		require(success);
	}
	function inv() public view {
		assert(address(this).balance > 0); // should fail
		assert(address(this).balance > 80); // should fail
		assert(address(this).balance > 90); // should fail
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// SMTIgnoreOS: macos
// ----
// Warning 6328: (239-272): CHC: Assertion violation might happen here.
// Warning 6328: (291-325): CHC: Assertion violation happens here.\nCounterexample:\n\n_a = 0x0\n_v = 8\n\nTransaction trace:\nC.constructor(){ msg.value: 101 }\nC.f(0x0, 9)\n    _a.call{value: _v}("") -- untrusted external call\nC.f(0x0, 9)\n    _a.call{value: _v}("") -- untrusted external call\nC.f(0x0, 8)\n    _a.call{value: _v}("") -- untrusted external call, synthesized as:\n        C.inv() -- reentrant call
// Warning 6328: (344-378): CHC: Assertion violation happens here.
// Warning 4661: (239-272): BMC: Assertion violation happens here.
