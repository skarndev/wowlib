// Second half of the Mach-O literal-atom probe; see probe_a.s for the rationale.
	.section __TEXT,__const
	.globl _probe_wk
	.weak_definition _probe_wk
_probe_wk:
	.ascii "WWWW"
L.str.901:
	.ascii "ZZZZZZZZZZZZZZZZ"

	.text
	.align 2
	.globl _probe_b
_probe_b:
	adrp	x0, L.str.901@PAGE
	add	x0, x0, L.str.901@PAGEOFF
	ret
	.subsections_via_symbols
