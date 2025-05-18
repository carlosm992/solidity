contract A {
    uint constant INHERITED = 1;
}
contract C is A {
    uint constant CONST = 2 + A.INHERITED;
    uint[C.CONST] array;
    uint[1 + A.INHERITED + 2] array2;
}
// ----
