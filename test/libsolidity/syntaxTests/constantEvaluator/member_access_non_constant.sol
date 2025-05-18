contract A {
    uint notConst = 1;
}
contract C is A {
    uint[A.notConst] array;
}
// ----
// TypeError 5462: (65-75): Invalid array length, expected integer literal or constant expression.
