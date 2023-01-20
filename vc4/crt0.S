	.text
	.global _start
_start:
	;mov r28, #_fstack
	;sub sp, r28, #4096

	;bl __vc4_init

	; Save r6+

	stm r6,lr,(--sp)
    mov r6, sp
    mov sp, #_fstack
    stm r6,(--sp)

	;mov r0, #_fbss
	;mov r2, r1
	;sub r2, r0
	;mov r1, #0
	;bl memset

	; Set up to call global destructors
	;mov r0, _fini
	;bl atexit

	; Call global constructors
	;bl _init

	; In case main tries to look at argc, argv (some GCC tests do).
	;mov r0, #0
	;mov r1, #0
	;mov r2, #0

	bl main
	;bl exit

	;mov r0, #0xdeadbeef
	;mov r0, #0x7e003004
	;ld r0,(r0)
    ldm r6, (sp++)
    mov sp, r6
    
	ldm r6,pc,(sp++)
	
 
