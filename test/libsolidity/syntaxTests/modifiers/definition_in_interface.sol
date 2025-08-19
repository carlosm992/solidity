interface I {
    modifier m { _; }
    modifier mu;
    modifier mv virtual { _; }
    modifier muv virtual;
}
// ----
// Warning 8429: (57-83): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// Warning 8429: (88-109): Virtual modifiers are deprecated and scheduled for removal in the next breaking version (0.9).
// TypeError 6408: (18-35): Modifiers cannot be defined or declared in interfaces.
// TypeError 6408: (40-52): Modifiers cannot be defined or declared in interfaces.
// TypeError 8063: (40-52): Modifiers without implementation must be marked virtual.
// TypeError 6408: (57-83): Modifiers cannot be defined or declared in interfaces.
// TypeError 6408: (88-109): Modifiers cannot be defined or declared in interfaces.
