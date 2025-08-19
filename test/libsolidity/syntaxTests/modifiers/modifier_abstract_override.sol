contract A {
    modifier m() virtual { _; }
}
abstract contract B is A {
    modifier m() virtual override;
}
contract C is B {
    function f() m public {}
}
// ----
// Warning 8429: (17-44): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// Warning 8429: (78-108): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// TypeError 4593: (78-108): Overriding an implemented modifier with an unimplemented modifier is not allowed.
