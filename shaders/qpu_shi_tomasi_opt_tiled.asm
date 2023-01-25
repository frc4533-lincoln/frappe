

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


.set temp4a,        ra12
.set temp6a,        ra13


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


.set sqrt1_2,       ra30
.set msqrt1_2,      ra31

# These registers hold the current three rows of the tile. The are five colums
# because we need to access outside the 64 pixel width of the tile
# .set r0c0,          rb0
# .set r0c1,          rb1
# .set r0c2,          rb2
# .set r0c3,          rb3
# .set r0c4,          rb4
# .set r1c0,          rb5
# .set r1c1,          rb6
# .set r1c2,          rb7
# .set r1c3,          rb8
# .set r1c4,          rb9
# .set r2c0,          rb10
# .set r2c1,          rb11
# .set r2c2,          rb12
# .set r2c3,          rb13
# .set r2c4,          rb14


.set vpm_set,       rb15
.set vdw_set,       rb16
.set vdw_stride,    rb17
.set istride,       rb18
.set ostride,       rb25

.set temp0,         rb19
.set temp1,         rb20
.set temp2,         rb21
.set temp3,         rb22
.set temp5,         rb23
.set temp7,         rb24

.set float2,        rb29
.set num256,        rb30
.set num64,         rb31

mov     src_ptr,        unif;
mov     tgt_addr,       unif;
mov	    istride,        unif;
# Read and throw away the uniforms we don't need
mov	    ostride,        unif;
mov	    temp0,          unif;
mov	    line_count,     unif;

mov     num256,         256
mov     num64,          64
mov     sqrt1_2,        M_SQRT1_2
mov     msqrt1_2,       -M_SQRT1_2
mov     float2,         2.0

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
sub     src_ptr,    src_ptr,        istride


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
add     src_ptr,    src_ptr,    istride;                                            ldtmu0  # 3
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
add     src_ptr,    src_ptr,    istride;                                            ldtmu0  # 3
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
add     src_ptr,    src_ptr,    istride;                                            ldtmu0  # 3
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


    # float gx =   (p2 + 2.0 * p5 + p8 - p0 - 2.0 * p3 - p6);
    fsub        r0,         0,          data0.8af
    fsub        r0,         r0,         data6.8af
    fadd        r0,         r0,         data2.8af
    fadd        r0,         r0,         data8.8af
    fmul        r1,         data3.8af,  float2; 
    fsub        r0,         r0,         r1;             fmul        r1,     data5.8af,  float2
    fadd        r0,         r0,         r1

    # float gy =   (p0 + 2.0 * p1 + p2 - p6 - 2.0 * p7 - p8);
    fsub        r2,         0,          data6.8af
    fsub        r2,         r2,         data8.8af
    fadd        r2,         r2,         data0.8af
    fadd        r2,         r2,         data2.8af
    fmul        r1,         data7.8af,  float2; 
    fsub        r2,         r2,         r1;             fmul        r1,     data1.8af,  float2
    fadd        r2,         r2,         r1


    # float g = 0.5 * length(vec2(gx, gy));
    fmul        r1,         r0,         r0
    fmul        r3,         r2,         r2
    fadd        temp0,      r1,         r3;             mov         r1,         0.25

    # r0 = gx
    # r2 = gy
    # r1 is gx^2 + gy^2

    # Find the reciprocal square root 1/sqrt(a), needs no division, using
    # the special function, then multiply to get sqrt.
    # The special function is not accurate, though
    # https://github.com/doe300/VC4CL/wiki/Hardware states 11-13 bits, which 
    # since the result is going to be squashed to 8 bits is probably fine..
    fmul        r1,         r0,         r1
    mov         recipsqrt,  temp0;                     
    # These instrs moved here to fill delay slots
    # gl_FragColor = vec4(g, dth, 0.25 * gx + 0.5, 0.25 * gy + 0.5);
    fadd        r1,         r1,         0.5
    mov         r3.8csf,    r1;                         mov         r1,         0.25
    fmul        r1,         r2,         r1
    fadd        r1,         r1,         0.5
    mov         r3.8dsf,    r1


    fmul        r1,         r4,         temp0
    # r1 now has length
    # get recip of length for normalise
    mov         recip,      r1;                         fmul        r3.8asf,    r1,         0.5
    
    # vec2 gv = normalize(vec2(gx, gy));
    mov         r1,         r4

    # temp0, temp1 are the normalised x and y components of gv
    fmul        temp0,      r0,         r1
    fmul        temp1,      r2,         r1


    .if fetch
    # Issue the fetches for the next line

    mov         r0,         src_ptr
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 1     rnc0
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 2     rnc1
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 3     rnc2
    mov         t0s,        r0;                     add     r0,         r0,         num64           # 4     rnc3
    ldtmu0                                                                                          # 3

    mov         t0s,        r0;                     add     src_ptr,    src_ptr,    istride         # 4     rnc4
    .endif



    # temp0, temp1 now contain normlised gx, gy

    # // The vector with the highest dot product to the gradient vector
    # // is the angle approximation
    # const vec2 d0 = vec2(1.0, 0.0);
    # const vec2 d1 = vec2(0.707, 0.707);
    # const vec2 d2 = vec2(0.0, 1.0);
    # const vec2 d3 = vec2(-0.707, 0.707);

    # float d0d = abs(dot(gv, d0));
    fminabs     temp4a,     temp0,      temp0
    # float d1d = abs(dot(gv, d1));
    fmul        r0,         temp0,      sqrt1_2
    fmul        r1,         temp1,      sqrt1_2
    fmul        r2,         temp0,      msqrt1_2;   fadd        r0,         r0,         r1
    fminabs     temp5,      r0,         r0
    # float d2d = abs(dot(gv, d2));
    fminabs     temp6a,     temp1,      temp1;
    # float d3d = abs(dot(gv, d3));
    fadd        r0,         r2,         r1
    fminabs     temp7,      r0,         r0    
    # mov         r3.8dsf,    r2

    # dth = ((d0d >= d1d) && (d0d >= d2d) && (d0d >= d3d)) ? 0.0
    #     : ((d1d >= d0d) && (d1d >= d2d) && (d1d >= d3d)) ? 0.25
    #     : ((d2d >= d0d) && (d2d >= d1d) && (d2d >= d3d)) ? 0.5
    #     : ((d3d >= d0d) && (d3d >= d1d) && (d3d >= d2d)) ? 0.75 : 0.1;
    fmax        r0,         temp4a,     temp5
    fmax        r1,         temp6a,     temp7
    fmax        r0,         r0,         r1;         mov         r1,         0.125
    fsub.setf   -,          r0,         temp4a      # z set when temp4a==max
    mov.ifz     r1,         0.0
    fsub.setf   -,          r0,         temp5       # z set when temp5==max
    mov.ifz     r1,         0.25
    fsub.setf   -,          r0,         temp6a      # z set when temp6a==max
    mov.ifz     r1,         0.5
    fsub.setf   -,          r0,         temp7       # z set when temp7==max
    mov.ifz     r1,         0.75

    mov         r3.8bsf,    r1;


    mov         vpm,        r3

    .endm 


    makedata    r0c0, r1c0, r2c0, r0c1, r1c1, r2c1, 0

    makedata    r0c1, r1c1, r2c1, r0c2, r1c2, r2c2, 0

    makedata    r0c2, r1c2, r2c2, r0c3, r1c3, r2c3, 0

    makedata    r0c3, r1c3, r2c3, r0c4, r1c4, r2c4, 1





    # Write VPM to memory, this writes 64 pixels, one row of the tile
    mov         vw_setup,   vdw_set
    mov         vw_setup,   vdw_stride
    mov         vw_addr,    tgt_addr
    add         tgt_addr,   tgt_addr,   ostride

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
