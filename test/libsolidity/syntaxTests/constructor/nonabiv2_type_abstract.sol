pragma abicoder v1;
abstract contract C {
	constructor(uint[][][] memory t) {}
}
// ====
// bytecodeFormat: legacy
// ----
// Warning 9511: (0-19): ABI coder v1 is deprecated and scheduled for removal in the next breaking version (0.9). Use ABI coder v2 instead.
