library L {
    modifier mu;
    modifier muv virtual;
}
// ----
// Warning 8429: (33-54): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// TypeError 8063: (16-28): Modifiers without implementation must be marked virtual.
// TypeError 3275: (33-54): Modifiers in a library cannot be virtual.
