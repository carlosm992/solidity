contract C {
	function f(address payable a) public {
		(bool success, ) = a.call{value: 200}("");
		require(success);
	}
}
// ====
// SMTEngine: bmc
// ----
