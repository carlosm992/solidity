contract C {
	address payable recipient;

	function f() public payable {
		require(msg.value > 1);
		bool success;
		recipient.call{value: 1}("");
	}
}
// ====
// SMTEngine: all
// ----
// Warning 9302: (117-145): Return value of low-level calls not used.
// Warning 2072: (101-113): Unused local variable.
