

.include "vc4.qinc"

# Register aliases
.set src_ptr,       ra0
.set tgt_addr,      ra1
.set line_count,    ra2

# These registers end up with 16 pixels at the nine locations we are convolving
#   0 1 2
#   3 4 5
#   6 7 8
.set data0,         ra3
.set data1,         ra4
.set data2,         ra5
.set data3,         ra6
.set data4,         ra7
.set data5,         ra8
.set data6,         ra9
.set data7,         ra10
.set data8,         ra11


.set ta0,           ra12
.set ta1,           ra13


# These registers hold the current three rows of the tile. The are five columns
# because we need to access outside the 64 pixel width of the tile
.set r0c0,          ra14
.set r0c1,          ra15
.set r0c2,          ra16
.set r0c3,          ra17
.set r0c4,          ra18
.set r1c0,          ra19
.set r1c1,          ra20
.set r1c2,          ra21
.set r1c3,          ra22
.set r1c4,          ra23
.set r2c0,          ra24
.set r2c1,          ra25
.set r2c2,          ra26
.set r2c3,          ra27
.set r2c4,          ra28

.set ta2,           ra29

.set scale1_3888888,    ra30
.set scale2_7777777,    ra31



.set tb0,           rb7
.set tb1,           rb8
.set tb2,           rb9
.set tb3,           rb10
.set tb4,           rb11
.set tb5,           rb12
.set tb6,           rb13
.set tb7,           rb14

.set vpm_set,       rb15
.set vdw_set,       rb16
.set vdw_stride,    rb17
.set stride,        rb18

.set temp0,         rb19
.set temp1,         rb20
.set temp2,         rb21
.set temp3,         rb22
.set temp5,         rb23
.set temp7,         rb24

.set corner_thr,    rb25
.set edge_minval,   rb26

.set scale1_3888888b,   rb30
.set num64,             rb31

mov     src_ptr,        unif;
mov     tgt_addr,       unif;
mov	    stride,         unif;
# Read and throw away the uniforms we don't need
mov	    temp0,          unif;
mov	    temp0,          unif;
mov	    line_count,     unif;
mov     corner_thr,     unif
mov     edge_minval,    unif

mov     num64,          64

mov     scale2_7777777,     2.7777777
mov     scale1_3888888,     1.3888888
mov     scale1_3888888b,    1.3888888


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
sub     src_ptr,    src_ptr,        stride


mov     vdw_stride, vdw_setup_1(0);

# Get address offsets into pointer, we start fetching one line up and one pixel to the left
sub     src_ptr,    src_ptr,        4
mul24   r0,         elem_num,       4
add     src_ptr,    src_ptr,        r0

nop

# Fetch first two rows
mov     r0,         src_ptr
mov     t0s,        r0;                     add     r0,         r0,         num64           # 1     r0c0
mov     t0s,        r0;                     add     r0,         r0,         num64           # 2     r0c1
mov     t0s,        r0;                     add     r0,         r0,         num64           # 3     r0c2
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r0c3
add     src_ptr,    src_ptr,    stride;                                             ldtmu0  # 3
mov     t0s,        r0;                                                                     # 4     r0c4

mov     r0c0,       r4;                     mov     r0,         src_ptr;            ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r1c0
mov     r0c1,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r1c1
mov     r0c2,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r1c2
mov     r0c3,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r1c3
mov     r0c4,       r4;
add     src_ptr,    src_ptr,    stride;                                             ldtmu0  # 3
mov     t0s,        r0;                                                                     # 4     r1c4


mov     r1c0,       r4;                     mov     r0,         src_ptr;            ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r2c0
mov     r1c1,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r2c1
mov     r1c2,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r2c2
mov     r1c3,       r4;                                                             ldtmu0  # 3
mov     t0s,        r0;                     add     r0,         r0,         num64           # 4     r2c3
mov     r1c4,       r4;
add     src_ptr,    src_ptr,    stride;                                             ldtmu0  # 3
mov     t0s,        r0                                                                      # 4     r2c4


:y
    # -----------------------
    # Start of loop, load third row, we can only issue addresses after we have used
    # the fixed function block for calculations
    mov         r2c0,       r4;                     mov     r0,         src_ptr;            ldtmu0  # 3
    mov         r2c1,       r4;                                                             ldtmu0  # 2
    mov         r2c2,       r4;                                                             ldtmu0  # 1
    mov         r2c3,       r4;                                                             ldtmu0  # 0
    mov         r2c4,       r4;          

    # Processing..
    read        vw_wait
    mov         vw_setup,   vpm_set


    .macro makedata, d0l, d1l, d2l, d0r, d1r, d2r, fetch
    .local
    mov         data0,      d0l;                    mov         r0,         d0l
    mov         data3,      d1l;                    mov         r1,         d1l
    mov         data6,      d2l;                    mov         r2,         d2l
    sub.setf    -,          elem_num,   15
    mov.ifn     data1,      r0 << 1
    mov.ifn     data4,      r1 << 1
    mov.ifn     data7,      r2 << 1
    sub.setf    -,          elem_num,   14
    mov.ifn     data2,      r0 << 2;                mov         r0,         d0r
    mov.ifn     data5,      r1 << 2;                mov         r1,         d1r
    mov.ifn     data8,      r2 << 2;                mov         r2,         d2r
    
    sub.setf    -,          elem_num,   15
    mov.ifnn    data1,      r0 << 1;        
    mov.ifnn    data4,      r1 << 1;        
    mov.ifnn    data7,      r2 << 1;       
    sub.setf    -,          elem_num,   14
    mov.ifnn    data2,      r0 << 2
    mov.ifnn    data5,      r1 << 2
    mov.ifnn    data8,      r2 << 2


    .if 1
    # Component a   gradient magnitude
    #           b   descretised angle of gradent
    #           c   gx
    #           d   gy

    # // We look at the pixels either side of the centre pixel (p4) and suppress
    # // if either are higher
    # //  p0  p1  p2
    # //  p3  p4  p5
    # //  p6  p7  p8
    # float angle = floor(p4.y * 4.0 + 0.5);
    # float m = p4.x;
    # float ml =      float(angle == 0.0) * p3.x
    #             +   float(angle == 1.0) * p6.x
    #             +   float(angle == 2.0) * p7.x
    #             +   float(angle == 3.0) * p8.x;
    # float mr =      float(angle == 0.0) * p5.x
    #             +   float(angle == 1.0) * p2.x
    #             +   float(angle == 2.0) * p1.x
    #             +   float(angle == 3.0) * p0.x;
    fmul        r0,         data4.8bf,  4.0
    fadd        r0,         r0,         0.5
    ftoi        r0,         r0


    sub.setf    -,          r0,         0
    mov.ifz     r1,         data3.8af
    mov.ifz     r2,         data5.8af
    sub.setf    -,          r0,         1
    mov.ifz     r1,         data6.8af
    mov.ifz     r2,         data2.8af
    sub.setf    -,          r0,         2
    mov.ifz     r1,         data7.8af
    mov.ifz     r2,         data1.8af
    sub.setf    -,          r0,         3
    mov.ifz     r1,         data8.8af
    mov.ifz     r2,         data0.8af


    # // We don't care about having exactly thin edges, but we do want edges to be
    # // continuous, adding a little constant here so that suppression is not so
    # // aggressive
    # m = float(max(ml, mr) <= m + relax_suppress) * m;
    # r1 = ml, r2 = mr, data4.8af = p4.x = m
    fmax        r0,         r1,         r2
    fmax        r0,         r0,         data4.8af
    fsub.setf   -,          r0,         data4.8af;      mov         r0,         0
    mov.ifz     r0,         data4.8af;                  mov         tb7,        0

    fsub.setf   -,          r0,         edge_minval;    mov         r0,         0
    # mov.ifc     tb7.8asf,   1.0

    # mov         tb7,        0
    # mov         tb7.8asf,   r0

    # We can skip the corner calculation if all edges were less than minval
    brr.allcc   -,          :1f
    mov.ifc     tb7.8asf,   1.0
    mov         r0,         0
    mov         r3,         tb7

    # // We get the structure matrix for Shi-Tomasi by doing a box filter on
    # // the 3x3 pixels gradients.
    # // Convert gradients back to proper values
    # float gx0 = (p0.z - 0.5) * 5.0;
    # float gx1 = (p1.z - 0.5) * 5.0;
    # float gx2 = (p2.z - 0.5) * 5.0;
    # float gx3 = (p3.z - 0.5) * 5.0;
    # float gx4 = (p4.z - 0.5) * 5.0;
    # float gx5 = (p5.z - 0.5) * 5.0;
    # float gx6 = (p6.z - 0.5) * 5.0;
    # float gx7 = (p7.z - 0.5) * 5.0;
    # float gx8 = (p8.z - 0.5) * 5.0;
    # float gy0 = (p0.a - 0.5) * 5.0;
    # float gy1 = (p1.a - 0.5) * 5.0;
    # float gy2 = (p2.a - 0.5) * 5.0;
    # float gy3 = (p3.a - 0.5) * 5.0;
    # float gy4 = (p4.a - 0.5) * 5.0;
    # float gy5 = (p5.a - 0.5) * 5.0;
    # float gy6 = (p6.a - 0.5) * 5.0;
    # float gy7 = (p7.a - 0.5) * 5.0;
    # float gy8 = (p8.a - 0.5) * 5.0;

    # // Get structure matrix Ix^2
    # float a = 0.5 * (gx0 * gx0 + gx1 * gx1 + gx2 * gx2 + gx3 * gx3 + gx4 * gx4 + gx5 * gx5 + gx6 * gx6 + gx7 * gx7 + gx8 * gx8) * 0.11111;

    # // IxIy
    # float b = (gx0 * gy0 + gx1 * gy1 + gx2 * gy2 + gx3 * gy3 + gx4 * gy4 + gx5 * gy5 + gx6 * gy6 + gx7 * gy7 + gx8 * gy8) * 0.11111;

    # // Iy^2
    # float c = 0.5 * (gy0 * gy0 + gy1 * gy1 + gy2 * gy2 + gy3 * gy3 + gy4 * gy4 + gy5 * gy5 + gy6 * gy6 + gy7 * gy7 + gy8 * gy8) * 0.11111;


    # Calculate the offset and products for the first column, we can move this into the 
    # fetch latency
    mov         ta0,        0
    mov         ta1,        0
    mov         ta2,        0
    fsub        r0,         data0.8cf,  0.5
    fsub        r1,         data0.8df,  0.5
    fsub        r2,         data3.8cf,  0.5;    fmul    tb3,    r0,     r0
    fsub        r3,         data3.8df,  0.5;    fmul    tb4,    r0,     r1
    fsub        r0,         data6.8cf,  0.5;    fmul    tb5,    r1,     r1
    fsub        r1,         data6.8df,  0.5;    fmul    tb6,    r2,     r2
    fadd        ta0,        ta0,        tb3;    fmul    tb3,    r2,     r3
    fadd        ta1,        ta1,        tb4;    fmul    tb4,    r3,     r3
    fadd        ta2,        ta2,        tb5;    fmul    tb5,    r0,     r0
    fadd        ta0,        ta0,        tb6;    fmul    tb6,    r0,     r1
    fadd        ta1,        ta1,        tb3;    fmul    tb3,    r1,     r1
    fadd        ta2,        ta2,        tb4;
    fadd        ta0,        ta0,        tb5
    fadd        ta1,        ta1,        tb6
    fadd        ta2,        ta2,        tb3

    fsub        r0,         data1.8cf,  0.5
    fsub        r1,         data1.8df,  0.5
    fsub        r2,         data4.8cf,  0.5;    fmul    tb3,    r0,     r0
    fsub        r3,         data4.8df,  0.5;    fmul    tb4,    r0,     r1
    fsub        r0,         data7.8cf,  0.5;    fmul    tb5,    r1,     r1
    fsub        r1,         data7.8df,  0.5;    fmul    tb6,    r2,     r2
    fadd        ta0,        ta0,        tb3;    fmul    tb3,    r2,     r3
    fadd        ta1,        ta1,        tb4;    fmul    tb4,    r3,     r3
    fadd        ta2,        ta2,        tb5;    fmul    tb5,    r0,     r0
    fadd        ta0,        ta0,        tb6;    fmul    tb6,    r0,     r1
    fadd        ta1,        ta1,        tb3;    fmul    tb3,    r1,     r1
    fadd        ta2,        ta2,        tb4;
    fadd        ta0,        ta0,        tb5
    fadd        ta1,        ta1,        tb6
    fadd        ta2,        ta2,        tb3

    fsub        r0,         data2.8cf,  0.5
    fsub        r1,         data2.8df,  0.5
    fsub        r2,         data5.8cf,  0.5;    fmul    tb3,    r0,     r0
    fsub        r3,         data5.8df,  0.5;    fmul    tb4,    r0,     r1
    fsub        r0,         data8.8cf,  0.5;    fmul    tb5,    r1,     r1
    fsub        r1,         data8.8df,  0.5;    fmul    tb6,    r2,     r2
    fadd        ta0,        ta0,        tb3;    fmul    tb3,    r2,     r3
    fadd        ta1,        ta1,        tb4;    fmul    tb4,    r3,     r3
    fadd        ta2,        ta2,        tb5;    fmul    tb5,    r0,     r0
    fadd        r0,         ta0,        tb6;    fmul    tb6,    r0,     r1
    fadd        r1,         ta1,        tb3;    fmul    tb3,    r1,     r1
    fadd        r2,         ta2,        tb4;    
    fadd        r0,         r0,         tb5
    fadd        r1,         r1,         tb6;    fmul    r0,     r0,     scale1_3888888
    fadd        r2,         r2,         tb3;    fmul    r1,     r1,     scale2_7777777

    # fmul        tb0,        ta0,        scale1_3888888b 
    # fmul        tb1,        ta1,        scale2_7777777b
    fmul        r2,         r2,        scale1_3888888b ;    mov     tb0,    r0

    # Moving the factors to the end reduces the number of multiplies:
    # p0 - 0.5



    # // Extract maximum eigenvalue. This falls out as solution to quadratic
    # float l1 = a + c - sqrt((a - c) * (a - c) + b * b);
    fsub        r0,         r0,         r2;     fmul        r1,         r1,         r1
    fmul        r0,         r0,         r0;     mov         r3,         tb7
    fadd        r0,         r0,         r1
    mov         recipsqrt,  r0
    # r1 = a + c
    fadd        r1,         tb0,        r2
    nop
    # r0 = sqrt(...)
    fmul        r0,         r0,         r4  # mult to get sqrt


    # gl_FragColor = vec4(m, l1, 0.0, 0.0);
    # r0 = l1
    fsub        r0,         r1,         r0
    fsub.setf   -,          r0,         corner_thr;     mov     r0,     0
    mov.ifc     r0,         1.0

    :1
    mov         r3.8csf,    r0

    .endif


    .if fetch
    # Issue the fetches for the next line

    mov         r0,         src_ptr
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 1     rnc0
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 2     rnc1
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 3     rnc2
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 4     rnc3
    ldtmu0                                                                                          # 3

    mov         t0s,        r0;                     add     src_ptr,    src_ptr,    stride          # 4     rnc4
    .endif



    mov         vpm,        r3

    .endloc
    .endm 


    makedata    r0c0, r1c0, r2c0, r0c1, r1c1, r2c1, 0

    makedata    r0c1, r1c1, r2c1, r0c2, r1c2, r2c2, 0

    makedata    r0c2, r1c2, r2c2, r0c3, r1c3, r2c3, 0

    makedata    r0c3, r1c3, r2c3, r0c4, r1c4, r2c4, 1





    # Write VPM to memory, this writes 64 pixels, one row of the tile
    mov         vw_setup,   vdw_set
    mov         vw_setup,   vdw_stride
    mov         vw_addr,    tgt_addr
    add         tgt_addr,   tgt_addr,   stride

    # Shuffle the rows up
    mov         r0c0,       r1c0
    mov         r0c1,       r1c1
    mov         r0c2,       r1c2
    mov         r0c3,       r1c3
    mov         r0c4,       r1c4
    mov         r1c0,       r2c0
    mov         r1c1,       r2c1
    mov         r1c2,       r2c2
    mov         r1c3,       r2c3
    mov         r1c4,       r2c4


    sub.setf    line_count, line_count, 1
    brr.anynz   -,          :y
    nop
    nop
    nop




    # End of loop
    # -----------------------

# Eat the remaining inflight requests
ldtmu0
ldtmu0
ldtmu0
ldtmu0






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
