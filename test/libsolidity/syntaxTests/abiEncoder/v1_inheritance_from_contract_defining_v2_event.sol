==== Source: A ====
pragma abicoder               v2;

struct Item {
    uint x;
}

contract C {
    event Ev(Item);
}
==== Source: B ====
pragma abicoder v1;
import "A";

contract D is C {}
// ====
// bytecodeFormat: legacy
// ----
// Warning 9511: (B:0-19): ABI coder v1 is deprecated and scheduled for removal in the next breaking version (0.9). Use ABI coder v2 instead.
