

.include "vc4.qinc"

# Register aliases
.set    tgt_addr,       ra0
.set    stride,         ra1

.set    m00xf,          ra2
.set    m10xf,          ra3
.set    m20xf,          ra4
.set    m01,            ra5
.set    m11,            ra6
.set    x,              ra7

.set    stride4,        rb0
.set    m00,            rb1
# .set    m01,            rb2
.set    m02,            rb3
.set    m10,            rb4
# .set    m11,            rb5
.set    m12,            rb6
.set    m20,            rb7
.set    m21,            rb8
.set    m22,            rb9
.set    vpm_set,        rb10
.set    vdw_set,        rb11
.set    vdw_stride,     rb12


.set    xf,             rb14
.set    yf,             rb15
# .set    x,              rb16
.set    y,              rb17
.set    f1_0,           rb18

mov     tgt_addr,       unif
mov     stride,         unif
mov     m00,            unif
mov     m01,            unif
mov     m02,            unif
mov     m10,            unif
mov     m11,            unif
mov     m12,            unif
mov     m20,            unif
mov     m21,            unif
mov     m22,            unif

mov     f1_0,           1.0        

shl     stride4,        stride,     2

# Create VPM Setup
# 32 bit words with horizontal lane, VPM addr increment 1
# Set the starting address in the VPM to QPU * 4
mov     r0,         vpm_setup(0, 1, h32(0));
mov     r1,         4;
mul24   r1,         qpu_num,    r1;
add     vpm_set,    r0,         r1;

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

shl     r1,         r1,     7;
mov     r0,         vdw_setup_0(4, 16, dma_h32(0, 0));
add     vdw_set,    r0,     r1;

# Create VPM DMA Stride setup
#                               stride
#  11 0000000000000 0 0000000000010000


mov     vdw_stride, vdw_setup_1(448);

# When performing texture lookups, we write to t, then s. Each write performs a single uniform 
# read, so two uniforms per texture lookup.
#
#
# Ah! It seems that the QPU program can reset the uniforms pointer, so we just set up a few uniforms, 
# then reset once we've done e.g. this lines reads


# Pre multiply unchanging partials
itof    r0,         elem_num
fadd    r0,         r0,         0.5
mov     r1,         0.5 

fmul    m00xf,      m00,        r0
fmul    m10xf,      m10,        r0
fmul    m20xf,      m20,        r0



.macro do_row
# Do matrix multiply
fmul        r2,         m21,        r1
fadd        r2,         r2,         m22;        fmul        r3,         m11,        r1
fadd        recip,      r2,         m20xf

fadd        r3,         r3,         m12;        fmul        r2,         m01,        r1
fadd        r2,         r2,         m02
fadd        r2,         r2,         m00xf
# Rescale and next row
fmul        r2,         r2,         r4;         fadd        r3,         r3,         m10xf
fmul        r3,         r3,         r4;         fadd        r1,         r1,         f1_0


# Fetch row
mov         t0t,            r3
mov         t0s,            r2
mov         unif_addr_rel,  -2

.endm

.rep loop, 4
# Setup VPM for writing
read        vw_wait;
mov         vw_setup,       vpm_set;

do_row
do_row
do_row
do_row

ldtmu0
mov         vpm,    r4; ldtmu0
mov         vpm,    r4; ldtmu0
mov         vpm,    r4; ldtmu0
mov         vpm,    r4


# Write VPM to memory, this writes 64 pixels, four rows of the fiducial
mov         vw_setup,       vdw_set
mov         vw_setup,       vdw_stride
mov         vw_addr,        tgt_addr

add         tgt_addr,       tgt_addr,   stride4
.endrep






nop
nop
nop
nop
nop
nop


mov.setf irq, nop;

nop; thrend
nop
nop
