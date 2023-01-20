## Debug tiles
# Debug tiled access pattern on the framebuffer using VPM writes to the framebuffer
# Visualizes how the different QPUs traverse the screen in tiled mode
# Use mode tiled (-m tiled) to execute

.include "vc4.qinc"

# Register names
.set    srcAddr,   ra0
.set    tgtAddr,   ra1
.set    lineWidth, ra2
.set    lineCount, ra3
.set    srcPtr,    ra4
.set    tgtPtr,    ra5
.set    srcPtr0,   ra6
.set    srcPtr1,   ra7
.set    srcPtr2,   ra8
.set    srcPtr3,   ra9
.set    y,         ra10
.set    x,         ra11

.set    num8,   ra27
.set    num32,  ra28
.set    num16a, ra29
.set    num64,  ra30
.set    num256, ra31

.set    srcStride,     rb0
.set    tgtStride,     rb1
.set    vpmSetup,      rb2
.set    vdwSetup,      rb3
.set    vdwStride,     rb4

.set    num1024b,   rb26
.set    num256b,    rb27
.set    num16,      rb28
.set    num64b,     rb29
.set    num128b,    rb30
.set    num192b,    rb31

# Register Constants
ldi     num8,       8;
ldi     num16,      16;
ldi     num32,      32;
ldi     num16a,     16;
ldi     num64,      64;
ldi     num64b,     64;
ldi     num128b,    128;
ldi     num192b,    192;
ldi     num256,     256;
ldi     num256b,    256;
ldi     num1024b,   1024;

# Uniforms
mov     srcAddr,    unif;
mov     tgtAddr,    unif;
mov	    srcStride,  unif;
mov	    tgtStride,  unif;
mov	    lineWidth,  unif;
mov     lineCount,  unif;

# Create VPM Setup
# 32 bit words with horizontal lane, VPM addr increment 1
# Set the starting address in the VPM to QPU * 4
#
# Each QPU gets its own four rows in the VPM
ldi     r0,         vpm_setup(0, 1, h32(0));
ldi     r1,         4;
mul24   r1,         qpu_num,    r1;
add     vpmSetup,   r0,         r1;

# Create VPM DMA Basic setup
# Using qpu * 4, 
#       units   depth l h   basey   bx mde
#  10 0010000 0000100 0 0 0qqqq00 0000 000
#
#
#   Parameters are UNITS, DEPTH
#   VPM block can be read horizontally or vertically.
#   When horiz, UNITS refers to rows of the VPM, when vert, to columns of VPM
#   DEPTH is the converse (cols when horiz, rosw when vert). 
#
# So vdw_setup_0(4, 16, dma_h32(0, 0)) means
# 4 rows of 16 elements, output as 32 bit words (no packing)

shl     r1,         r1,         7;
ldi     r0,         vdw_setup_0(4, 16, dma_h32(0, 0));
add     vdwSetup,   r0,         r1;

# Create VPM DMA Stride setup
#                               stride
#  11 0000000000000 0 0000000000000000
ldi     vdwStride,  vdw_setup_1(0);

mov     tgtPtr,     tgtAddr;
mov     srcPtr,     srcAddr;

# Add in lane address offsets
mul24   r2,         elem_num,   4
add     srcPtr,     srcPtr,     r2
nop
mov     srcPtr0,    srcPtr
add     srcPtr1,    srcPtr,     num64b
add     srcPtr2,    srcPtr,     num128b
add     srcPtr3,    srcPtr,     num192b

# Adjust stride, removing the written bytes in each line
mul24   r0,         lineWidth,  num256b
sub     srcStride,  srcStride,  r0
sub     tgtStride,  tgtStride,  r0

# Line Iterator - at least one line else loop will break
mov     y,          lineCount

:y # Loop over lines
    # Setup VPM for writing

    mov     x,          lineWidth

    :x  # Loop over 64 pixel chunks. Each chunk is 256 bytes

        read    vw_wait;
        mov     vw_setup,   vpmSetup;

        mov     t0s,        srcPtr0;         
        mov     t0s,        srcPtr1;         
        mov     t0s,        srcPtr2;         
        mov     t0s,        srcPtr3;         

        ldtmu0
        mov     vpm,    r4;     add srcPtr0,    srcPtr0,    num256b;          ldtmu0
        mov     vpm,    r4;     add srcPtr1,    srcPtr1,    num256b;          ldtmu0
        mov     vpm,    r4;     add srcPtr2,    srcPtr2,    num256b;          ldtmu0
        mov     vpm,    r4;     add srcPtr3,    srcPtr3,    num256b;


        # Write VPM to memory
        mov     vw_setup,   vdwSetup;
        mov     vw_setup,   vdwStride;
        mov     vw_addr,    tgtPtr;


        # Wait for DMA to complete
        read    vw_wait;

        sub.setf        x,      x,      1
        brr.anynz       -,      :x
        add             tgtPtr,     tgtPtr,     num256b
        nop
        nop

    

	add     srcPtr0,    srcPtr0,    srcStride;
	add     srcPtr1,    srcPtr1,    srcStride;
	add     srcPtr2,    srcPtr2,    srcStride;
	add     srcPtr3,    srcPtr3,    srcStride;

	# Line loop :y
	sub.setf        y,  y,  1;
	brr.anynz       -,  :y
    # Increase adresses to next line (always happens because in branch delay slot)
	add     tgtPtr,     tgtPtr,     tgtStride;
	nop
    nop


mov.setf irq, nop;

nop; thrend
nop
nop
