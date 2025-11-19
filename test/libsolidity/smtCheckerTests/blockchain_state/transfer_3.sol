contract C {
	address payable recipient;

	function shouldFail() public {
		(bool success, ) = recipient.call{value: 1}("");
		require(success);
	}
}
// ====
// SMTEngine: all
// ----
