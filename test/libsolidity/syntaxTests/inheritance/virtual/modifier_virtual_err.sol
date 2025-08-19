library test {
    modifier m virtual;
    function f() m public {
    }
}
// ----
// Warning 8429: (19-38): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// TypeError 3275: (19-38): Modifiers in a library cannot be virtual.
