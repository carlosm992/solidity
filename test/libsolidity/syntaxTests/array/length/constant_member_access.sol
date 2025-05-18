contract A {
    uint constant INHERITED = 42;
}

contract C is A {
    uint constant CONST = 64;
    uint[A.INHERITED] x;
    uint[C.CONST] y;
}
// ----
