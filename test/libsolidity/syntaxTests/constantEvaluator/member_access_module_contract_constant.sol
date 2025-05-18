==== Source: A.sol ====
contract A {
    uint constant CONST = 2;
}
==== Source: B.sol ====
import "A.sol" as M;
contract C is M.A {
    uint[M.A.CONST] array;
}
// ----
