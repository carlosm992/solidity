uint256 constant MAX = 1;

library L1 {
    uint256 internal constant INT = 100;
}

contract C1 {
    uint256 internal constant CONST = 10 + L1.INT; // backward reference
    uint256 internal constant CONST2 = 20 + L2.INT; // forward reference
    uint256 internal constant LIMIT = MAX * L1.INT;  // same file & external library constant
    uint256 internal constant NESTED = LIMIT + CONST; // nested & same contract constant

    uint256[L1.INT] internal arr1; // ok, backward reference
    uint256[L2.INT] internal arr2; // error, forward reference
    uint256[CONST2] internal arr3; // error, computed with forward reference
}

contract C2 is C1 {
    uint256 internal constant INHERITED = NESTED + CONST * LIMIT; // inherited constants
}

contract C3 is C2 {
    uint256 internal constant NESTED_INHERITED = INHERITED + NESTED + CONST * LIMIT; // nest-inherited constants

    uint256[CONST] internal arr4;            // nest-inherited constants
    uint256[NESTED_INHERITED] internal arr5; // same contract constant
}

library L2 {
    uint256 internal constant INT = 100;
}

// ----
// TypeError 5462: (501-507): Invalid array length, expected integer literal or constant expression.
// TypeError 5462: (564-570): Invalid array length, expected integer literal or constant expression.
