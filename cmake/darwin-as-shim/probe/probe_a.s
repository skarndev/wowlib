// Half of the Mach-O literal-atom probe; see cmake/DarwinAtomProbe.cmake.
//
// Hand-written because it must reproduce gcc's exact output shape: a
// coalescable weak symbol in a const section, immediately followed by a string
// literal carrying an assembler-*temporary* (uppercase-L) label. probe_b.s is
// identical except for the literal's contents.
//
// _probe_wk is byte-identical in both TUs, so ld coalesces them and keeps one.
// Without the shim the two literals are not atoms of their own, they ride
// _probe_wk at +4, and whichever TU loses coalescing has its literal silently
// replaced by the winner's -- exactly the failure that corrupted libstdc++'s
// to_chars digit table.
	.section __TEXT,__const
	.globl _probe_wk
	.weak_definition _probe_wk
_probe_wk:
	.ascii "WWWW"
L.str.900:
	.ascii "0123456789abcdef"

	.text
	.align 2
	.globl _probe_a
_probe_a:
	adrp	x0, L.str.900@PAGE
	add	x0, x0, L.str.900@PAGEOFF
	ret
	.subsections_via_symbols
