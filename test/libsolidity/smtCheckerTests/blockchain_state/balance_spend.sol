contract C {
	constructor() payable {
		require(msg.value > 100);
	}
	uint c;
	function f(address payable _a, uint _v) public {
		require(_v < 10);
		require(c < 2);
		++c;
		(bool success, ) = _a.call{value: _v}("");
		require(success);
	}
	function inv() public view {
		assert(address(this).balance > 80); // should hold
		assert(address(this).balance > 90); // should fail
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (273-307): CHC: Assertion violation might happen here.
// Warning 6328: (326-360): CHC: Assertion violation happens here.\nCounterexample:\nc = 2\n\nTransaction trace:\nC.constructor(){ msg.value: 101 }\nState: c = 0\nC.f(0x0, 9)\n    _a.call{value: _v}("") -- untrusted external call\nState: c = 1\nC.f(0x0, 9)\n    _a.call{value: _v}("") -- untrusted external call\nState: c = 2\nC.inv()
// Info 1391: CHC: 1 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
// Warning 4661: (273-307): BMC: Assertion violation happens here.
