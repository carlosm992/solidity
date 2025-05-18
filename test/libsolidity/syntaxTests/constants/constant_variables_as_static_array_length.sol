uint256 constant MAX = 1;

library L1 {
    uint256 internal constant INT = 100;
}

contract C1 {
    uint256 internal constant CONST1 = L1.INT;

    uint256[L1.INT] internal arr1;
    uint256[L2.INT] internal arr2; // error, forward reference
}

contract C2 is C1 {
    uint256 internal constant CONST2 = CONST1;

    uint256[CONST1] internal arr3;
    uint256[CONST2] internal arr4;
}

library L2 {
    uint256 internal constant INT = 100;
}

// ----
// TypeError 5462: (193-199): Invalid array length, expected integer literal or constant expression.
