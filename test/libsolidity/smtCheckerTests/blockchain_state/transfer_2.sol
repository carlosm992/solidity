contract C {
	address payable recipient;
	uint amount;

	function shouldHold() public {
		uint tempAmount = address(this).balance;
		(bool success, ) = recipient.call{value: tempAmount}("");
		require(success);
		(success, ) = recipient.call{value: amount}("");
		require(success);
	}
}
// ====
// SMTEngine: chc
// ----
