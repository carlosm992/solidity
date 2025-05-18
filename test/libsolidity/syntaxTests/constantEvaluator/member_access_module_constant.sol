==== Source: A.sol ====
uint constant CONST = 2;
==== Source: B.sol ====
import "A.sol" as M;
contract C {
    uint[M.CONST] array1;
}
// ----
