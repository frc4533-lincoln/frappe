
; Notes on the vector ISA.
;-------------------------
;
; Descriptions on https://github.com/hermanhermitage/videocoreiv/wiki/VideoCore-IV-Programmers-Manual
;
; The patent diagram shows the path to the PPUs as 256 bits wide, implying that each of
; the 16 PPUs is a 16 bit wide ALU. But actually, the ALU seems to be 32 bits wide, with 512 bit 
; paths. It is not clear how narrower bit widths are expanded

; The 16/32 term in the instruction mnemonic seems to be to enable sign extension of 16 bit operands,
; 
; I would assume that e.g. v32testmag would work fine with 32 bit operands, but it doesnt seem to.
; Likewise only v16bitplanes works
;
; 


.text
    
.global do_block_test

do_block_test:
        ; r0    src
        ; r1    dest

        stm         r6,         (--sp)
        mov         r4, 0x008040
        mov         r6, 0x40
        mov         r2, 0x40
        v32ld       HY(0++,0),  (r0+=r2) REP64           ; load 64 rows of 16 32 bit values
        v32and      HY(0++,0),  HY(0++,0), #0xff REP64   ; mask all except bottom byte
        mov         r5,         0x3f
        mov         r3,         0                       ; r3 is vrf index
l1:
        v16testmag  -,          HY(0,0)+r3, r6 SETF     ; compare all in row
        v32mov      HY(0,0)+r3, r4                       ; set row with contents of r4
        v32mov      HY(0,0)+r3, #0 IFNZ                  ; if z flag set, set elts of row to zero
        ;v32add      HY(0,0)+r3, HY(0,0)+r3, r6
        add         r3,         #0x40                    ; move to the next row
        addcmpbge   r5,-1,0,    l1                       ; loop until block done

        v32st       HY(0++,0),  (r1+=r2) REP64 
        ldm         r6,         (sp++)
        rts


.global flush_cache
flush_cache:
        ; We need to read 128k from region 0 to force the cache to write 
        ; out all data. 
        mov         r0, 0x0
        mov         r1, 0x2000
0:
        ld          r3,     (r0)
        add         r0,     r0,     0x20
        addcmpbgt   r1,-1,0,        0b
        rts

        ; This doesn't quite work.. still some unstable parts of buffer..
        ; Does vector load work via L2 or direct DMA?
        ;
        mov         r2, 0x40
        v32ld       HY(0++,0),  (r0+=r2)    REP64
        add         r0,         r0,         0x1000
        addcmpbgt   r1,-1,0,    0b
        rts

.global do_bandwidth_test
do_bandwidth_test:
        ; Read a large amount of memory as fast as possible 
        ; r0    src
        ; r1    stride
        ; r2    count
        ;
        mov         r3, 0x40
0:
        v32ld       HY(0++,0),  (r0+=r3)    REP64
        add         r0,         r0,         r1
        addcmpbgt   r2,-1,0,    0b
        rts
        


.global do_y_copy
do_y_copy:
        ; Copy Y bytes from camera buffer with 640x480x8 resolution, 
        ; to 1024x512x32 RGBA buffer
        ; r0    src
        ; r1    dst
        ;
        ; Work on 64x32 pixel tiles, with 10x15 tiles over whole frame
        stm         r6-r8,         (--sp)
        v32mov      HY(32++,0), 0           REP32

        mov         r8,         15
        add         r2,         r0,     640*479
2:
        mov         r4,         10
        mov         r3,         -640
        mov         r5,         4096
    0:
            ; load 64x32 pixels
            v32ld       HY(0++,0),  (r2+=r3)    REP32

            mov         r6,         0
            mov         r7,         0
        1:
                v32mov      VY(32,0)+r6,   V(0,0)+r7
                v32mov      VY(32,1)+r6,   V(0,16)+r7
                v32mov      VY(32,2)+r6,   V(0,32)+r7 
                v32mov      VY(32,3)+r6,   V(0,48)+r7
                v32mov      VY(48,0)+r6,   V(16,0)+r7
                v32mov      VY(48,1)+r6,   V(16,16)+r7
                v32mov      VY(48,2)+r6,   V(16,32)+r7 
                v32mov      VY(48,3)+r6,   V(16,48)+r7
                add         r7,         1
                addcmpblt   r6, 4,  16, 1b
                v32st       HY(32++,0), (r1+=r5)    REP32
                add         r1,         64
                mov         r6,         0
                addcmpblt   r7, 0,  16, 1b

            add         r2,         64
            addcmpbgt   r4, -1, 0,  0b        
        
        ; move the dest pointer forward by 32 lines, minus one row of tiles
        add         r1,     32*4096-2560
        ; move src pointer forward by 31 lines
        add         r2,     -33*640 
        addcmpbgt   r8, -1, 0,  2b




        ldm         r6-r8,      (sp++)
        rts

// .global do_y_copy
// do_y_copy:
//         ; Copy Y bytes from camera buffer with 640x480x8 resolution, 
//         ; to 1024x512x32 RGBA buffer
//         ; r0    src
//         ; r1    dst
//         ;
//         ; Work on 64x32 pixel tiles, with 10x15 tiles over whole frame

//         mov         r2,         64
//         v32ld       HY(0++,0),  (r0+=r2)    REP32
//         v32mov      HY(32++,0), 0           REP32


//         rts

.global do_block_empty_test
do_block_empty_test:
        ; Iterate over buffer, marking all 16x16 blocks that have any pixels
        ; in them. This is used to accelerate the contour tracing by skipping
        ; empty blocks in the raster-scan
        ;
        ; This take approx 1.2ms for 640x480
        ;
        ; r0    src
        ; r1    stride
        ; r2    dest
        ; r3    width
        ; r4    height

        stm         r6-r10,     (--sp)


        mov         r8,         r0
        lsr         r3,         r3,         4
        lsr         r4,         r4,         4

        mov         r3,         40
        mov         r4,         30

        mov         r5,         0
1:
        mov         r6,         0
0:
        ; addr = src + (y << 16) + (x << 6)
        shl         r9,         r5,         16                          ; 4096*16 rows
        shl         r10,        r6,         6                           ; 16 pixels * 4 bytes
        add         r0,         r8,         r9
        add         r0,         r0,         r10
        v32mov      H(16,0),    0
        v32ld       HY(0++,0),  (r0+=r1)                REP16           ; load 16 rows of 16 32 bit values
        v32or       H(16,0),    H(16,0),    H(0++,0)    REP16 MAX r7    ; or together low bytes in block
                                                                        ; with r7 getting the max value
        ; addr = dest + (y << 6) + x
        shl         r9,         r5,         6
        add         r9,         r9,         r6
        add         r0,         r2,         r9

        stb         r7,         (r0)

        addcmpblt   r6,         1,          r3,         0b
        addcmpblt   r5,         1,          r4,         1b

        ldm         r6-r10,     (sp++)
        rts


.global do_extract_channel
do_extract_channel:
        ; Extract a single channel of the input image to create an output image
        ; of the same dimensions.
        ;
        ; r0    src
        ; r1    dest
        ; r2    width - must be multiple of 16
        ; r3    height
        ; r4    stride

        stm         r6-r7,     (--sp)

        ; Get width as 16 pixel wide blocks. r0 is the only register that can be
        ; used for variable REP, so save src in r5
        mov         r5,         r0
        lsr         r0,         r2,        4
        shl         r4,         2

        ; Address increment for each load/store
        mov         r6,         64
        mov         r7,         16
0:
        ; Load one line (up to 1024 pixels) of 4 byte pixels
        v32ld       HY(0++, 0), (r5+=r6)                REP r0

        ; Store channel 0 of line as bytes
        v8st        H(0++, 0),  (r1+=r7)                REP r0

        ; Do appropriate incrments, and loop until all lines done
        add         r5,         r4
        add         r1,         r2
        addcmpbgt   r3,         -1,         0,          0b


        ldm         r6-r7,     (sp++)
        rts

.global do_scale
do_scale:
        ; Scale input image down
        ;
        ; r0    src
        ; r1    dest
        ; r2    width - must be multiple of 16
        ; r3    height
        ; r4    stride

        stm             r6-r9,      (--sp)

        v32mov          HY(0++,0),  0                           REP64


        mov             r5,         r0              ; save src
        mov             r7,         r0
        shl             r4,         2               ; get stride in bytes
        lsr             r2,         4               ; get width in chunks of 16 pix 

        ; Create the fetch pattern for 0.75 scale
        mov             r0,         15
        v32mov          VY(0,15),   1
        v32mov          VY(0,0),    0
        v32add          VY(0,1++),  V(0,15),    V(0,0++)        REP r0
        ; now ascending 
        mov             r0,         0xfff8
        v16bitplanes    -,          r0                          SETF
        v32add          HY(0,0),    HY(0,0),    1               IFNZ
        shl             r0,         3
        v16bitplanes    -,          r0                          SETF
        v32add          HY(0,0),    HY(0,0),    1               IFNZ
        shl             r0,         3
        v16bitplanes    -,          r0                          SETF
        v32add          HY(0,0),    HY(0,0),    1               IFNZ
        shl             r0,         3
        v16bitplanes    -,          r0                          SETF
        v32add          HY(0,0),    HY(0,0),    1               IFNZ
        shl             r0,         3
        v16bitplanes    -,          r0                          SETF
        v32add          HY(0,0),    HY(0,0),    1               IFNZ
        ; now has pattern
        ;   0   1   2   4   5   6   8   9   a   c   d   e   10  11  12  14


        v32mov          -,          HY(0,0)                     CLRA UACC
        ; we should now have address offsets in ACC

        ; Now fetch complete decimated row. Max length is 640 = 40 rows to output
        ; Decimation at 0.75 is 30 rows

        ;--------------------------
        ; r1 is dest
        ; r3 contains the number of rows in the output
        1:
        mov             r0,         0
        mov             r5,         r7
            ;-----------------------------
            mov             r6,         r2
            0:
            v32lookupml     HY(0,0)+r0, (r5)
            add             r0,         0x40
            add             r5,         0x54
            addcmpbgt       r6,         -1,         0,      0b

        ; Loaded whole line
        mov             r0,         r2
        mov             r8,         0x40

        ; some fixed data for testing
        // mov             r9,         0xff804020
        // v32mov          HY(0,0),    r9
        // v32mov          HY(0++,0),  HY(0,0)                         REP64


        ; Store the line out
        v32st           HY(0++,0),  (r1+=r8)            REP r0

        ; Get the next line address
        add             r7,         r4
        ; if linecount % 4 == 0, skip a line from the input
        and             r9,         r3,         3
        cmp             r9,         0
        add.eq          r7,         r7,         r4

        add             r1,         r4
        addcmpbgt       r3,         -1,         0,      1b




        ldm             r6-r9,      (sp++)
        rts



.global do_load_code
do_load_code:
        ; r0    location of codes
        
        ; Load the entire set of codes into the VRF
        mov             r3,         0x40
        v32ld           HY(0++,0),  (r0+=r3)                REP32
        rts


.global do_hamming_dist
do_hamming_dist:
        ; r0    location of codes
        ; r1    output buffer
        ; r2    worst case Hamming distance to accept
        ; 
        ; The four possible bit patterns are in HY(57), HY(58), HY(59), HY(60)

        stm         r6-r12,     (--sp)


        ; Take an item from bits and compute the hamming distance for
        ; each entry in codes, keeping note of the lowest distance and the 
        ; code index.
        ;
        ; 

        ; Load the entire set of codes into the VRF
        mov             r3,         0x40
        
        ; clear output buffer !!FIXME!! for debug
        ; v32mov      HY(0,0),    0
        ; v32st       HY(0,0),    (r1+=r3)    REP8

        v32ld           HY(0++,0),  (r0+=r3)                REP32

        ; Get element of bits
        ; Replicate over row 32
        ; Check if best, repeat for each element of bits
        ;
        ; r9    best index
        ; r10   best hamming
        ; r11   best row
        mov             r10,        36      ; Highest possible
        mov             r0,         0
        1:
            ; Get an orientation
            shl             r5,         r0,         6
            v32mov          HY(32,0),   HY(57,0)+r5

            ; Loop over all the codes, saving ones with lowest Hamming distance.
            ; Each loop checks eight codes
            mov             r5,         0           ; Loop counter
            mov             r6,         0           ; VRF row pointer
            mov             r7,         0x40        ; VRF row increment
            mov             r8,         0x20        ; Loop limit
            0:
                v32eor          HY(33,0),   HY(32,0),   HY(0,0)+r6
                ; only v16count seems to work
                v16count        HY(34,0),   HX(33,0),   HX(33,32)
                v32even         HX(35,0),   HX(34,0),   HX(34,32)
                v32odd          HX(35,32),  HX(34,0),   HX(34,32)
                v16bitplanes    -,          0xff                      SETF
                ; This sums the counts from the low and high 32 bits, and take the 
                ; index of the lowest for r3
                v16add          HX(36,0),   HX(35,0),   HX(35,32)   IFNZ IMIN r3

                ; r3 has the index of the minimum value, extract the value
                mov             r4,         1
                shl             r4,         r4,         r3
                v16bitplanes    -,          r4                      SETF
                v16add          -,          HX(36,0),   0           IFNZ SUMU r4
                ; r4 has the minimum value
                v32mov          HX(37,0),   r4

                ; If the hamming distance is lower than the current, grab the new
                ; distance, index, and row
                and             r4,         0xff
                and             r10,        0xff
                cmp             r4,         r10
                mov.le          r9,         r3
                mov.le          r10,        r4
                mov.le          r11,        r5
                mov.le          r12,        r0


                ; HX(36,0) now has the Hamming distances for the row
                ; stb r11, (r1++)     ; best row
                ; stb r9, (r1++)      ; best index in row
                ; stb r10, (r1++)     ; best hamming
                ; stb r5, (r1++)      ; loop counter


                add             r6,         r6,         r7
                addcmpblt       r5,         1,          r8,         0b
            

            ; move on to the next entry in bits
            addcmpblt       r0,         1,          4,          1b

        shl             r12,        r12,        8   ; ID =  (orient << 8) |
        shl             r3,         r11,        3   ;       (row << 3) |
        or              r3,         r9              ;       index
        or              r3,         r12
        cmp             r10,        r2              ; return hamming <= lim ? ID : -1
        mov.le          r0,         r3
        mov.gt          r0,         -1

        ldm             r6-r12,     (sp++)
        rts


.global do_hamming_dist_no_load
do_hamming_dist_no_load:
        ; r0    location of codes
        ; r1    output buffer
        ; r2    worst case Hamming distance to accept
        ; 
        ; The four possible bit patterns are in HY(57), HY(58), HY(59), HY(60)

        stm         r6-r12,     (--sp)


        ; Take an item from bits and compute the hamming distance for
        ; each entry in codes, keeping note of the lowest distance and the 
        ; code index.
        ;
        ; 


        ; Get element of bits
        ; Replicate over row 32
        ; Check if best, repeat for each element of bits
        ;
        ; r9    best index
        ; r10   best hamming
        ; r11   best row
        mov             r10,        36      ; Highest possible
        mov             r0,         0
        1:
            ; Get an orientation
            shl             r5,         r0,         6
            v32mov          HY(32,0),   HY(57,0)+r5

            ; Loop over all the codes, saving ones with lowest Hamming distance.
            ; Each loop checks eight codes
            mov             r5,         0           ; Loop counter
            mov             r6,         0           ; VRF row pointer
            mov             r7,         0x40        ; VRF row increment
            mov             r8,         0x20        ; Loop limit
            0:
                v32eor          HY(33,0),   HY(32,0),   HY(0,0)+r6
                ; only v16count seems to work
                v16count        HY(34,0),   HX(33,0),   HX(33,32)
                v32even         HX(35,0),   HX(34,0),   HX(34,32)
                v32odd          HX(35,32),  HX(34,0),   HX(34,32)
                v16bitplanes    -,          0xff                      SETF
                ; This sums the counts from the low and high 32 bits, and take the 
                ; index of the lowest for r3
                v16add          HX(36,0),   HX(35,0),   HX(35,32)   IFNZ IMIN r3

                ; r3 has the index of the minimum value, extract the value
                mov             r4,         1
                shl             r4,         r4,         r3
                v16bitplanes    -,          r4                      SETF
                v16add          -,          HX(36,0),   0           IFNZ SUMU r4
                ; r4 has the minimum value
                v32mov          HX(37,0),   r4

                ; If the hamming distance is lower than the current, grab the new
                ; distance, index, and row
                and             r4,         0xff
                and             r10,        0xff
                cmp             r4,         r10
                mov.le          r9,         r3
                mov.le          r10,        r4
                mov.le          r11,        r5
                mov.le          r12,        r0


                ; HX(36,0) now has the Hamming distances for the row
                // stb r11, (r1++)     ; best row
                // stb r9, (r1++)      ; best index in row
                // stb r10, (r1++)     ; best hamming
                // stb r12, (r1++)     ; orientation


                add             r6,         r6,         r7
                addcmpblt       r5,         1,          r8,         0b
            

            ; move on to the next entry in bits
            addcmpblt       r0,         1,          4,          1b

        shl             r12,        r12,        8   ; ID =  (orient << 8) |
        shl             r3,         r11,        3   ;       (row << 3) |
        or              r3,         r9              ;       index
        or              r3,         r12
        cmp             r10,        r2              ; return hamming <= lim ? ID : -1
        mov.le          r0,         r3
        mov.gt          r0,         -1
        st              r0,         (r1)

        ldm             r6-r12,     (sp++)
        rts



.global do_make_bits
do_make_bits:
        ; Convert a fiducial pattern into the four possible bit patterns.
        ;
        ; Each fiducial is represented as a 16x16 pixel array of 8 bit data.
        ; Threshold the data by finding the minimum and maximum pixel values,
        ; and setting the threshold to be half way between them. Add the four
        ; pixels representing a square of the fiducial and thresholding that.
        ;
        ; r0    location of fiducial data
        ; r1    index into fid data

        ; Zero array !!FIXME!!
        ;v32mov          HY(0++,0),  0                       REP64

        ;mov             r3,         0x40

        ; Calculate starting position of fiducial data
        ; int offset = ((idx & 7) << 4) | ((idx & 0x18) << 8)
        ; Words are 32 bit, so extra x4 needed
        and         r3,     r1,     7
        shl         r3,     6
        add         r0,     r3
        and         r3,     r1,     0x18
        shl         r3,     10
        add         r0,     r3

        ; Load red bytes
        mov         r2,         0x200
        v32ld       H(48++,0),   (r0+=r2)        REP16

        ; Get maximum
        v32mov      H(48,16),    0
        v32max      H(48,16),    H(48,16),    H(48++,0)    REP16 MAX r3
        v32mov      H(49,16),    r3                                      ; for debug

        ; Get minimum. There is no scalar register update MIN function, so
        ; flip the bits before and after comparison
        v32mov      H(48,16),    0xff
        v32min      H(48,16),    H(48,16),    H(48++,0)    REP16
        v32eor      H(48,16),    H(48,16),    0xff        MAX r4
        eor         r4,         0xff
        ; v32mov      H(18,0),    r4  ; for debug

        ; get threshold
        sub         r3,         r4
        lsr         r3,         1
        add         r3,         r4
        shl         r3,         2
        ; v32mov      HY(19,0),   r3

        ; Add groups of 4 pixels to single 32 bit words.
        ; First add adjacent rows..
        v32add      HY(32,0),       H(48,0),        H(49,0)
        v32add      HY(33,0),       H(50,0),        H(51,0)
        v32add      HY(34,0),       H(52,0),        H(53,0)
        v32add      HY(35,0),       H(54,0),        H(55,0)
        v32add      HY(36,0),       H(56,0),        H(57,0)
        v32add      HY(37,0),       H(58,0),        H(59,0)
        v32add      HY(38,0),       H(60,0),        H(61,0)
        v32add      HY(39,0),       H(62,0),        H(63,0)
        ; ..then odd and even lanes
        v32even     HY(40++,0),     HY(32++,0),     0               REP8
        v32odd      HY(48++,0),     HY(32++,0),     0               REP8
        v32add      HY(32++,0),     HY(40++,0),     HY(48++,0)      REP8

        ; Apply threshold
        v32mov      HY(63,0),       r3
        v32mov      HY(48++,0),     0                           REP8
        v32sub      -,              HY(32,0),       HY(63,0)    SETF
        v32mov      HY(48,0),       1                           IFNC
        v32sub      -,              HY(33,0),       HY(63,0)    SETF
        v32mov      HY(49,0),       1                           IFNC
        v32sub      -,              HY(34,0),       HY(63,0)    SETF
        v32mov      HY(50,0),       1                           IFNC
        v32sub      -,              HY(35,0),       HY(63,0)    SETF
        v32mov      HY(51,0),       1                           IFNC
        v32sub      -,              HY(36,0),       HY(63,0)    SETF
        v32mov      HY(52,0),       1                           IFNC
        v32sub      -,              HY(37,0),       HY(63,0)    SETF
        v32mov      HY(53,0),       1                           IFNC
        v32sub      -,              HY(38,0),       HY(63,0)    SETF
        v32mov      HY(54,0),       1                           IFNC
        v32sub      -,              HY(39,0),       HY(63,0)    SETF
        v32mov      HY(55,0),       1                           IFNC

        ; Convert to bit pattern
        ;
        ; Get power-of-2 descending in cols 1-6, ascending in cols
        v32mov          V(32,6),    1
        v32add          V(32,5),    V(32,6),    V(32,6)
        v32add          V(32,4),    V(32,5),    V(32,5)
        v32add          V(32,3),    V(32,4),    V(32,4)
        v32add          V(32,2),    V(32,3),    V(32,3)
        v32add          V(32,1),    V(32,2),    V(32,2)

        v32mov          V(32,17),   1
        v32add          V(32,18),   V(32,17),   V(32,17)
        v32add          V(32,19),   V(32,18),   V(32,18)
        v32add          V(32,20),   V(32,19),   V(32,19)
        v32add          V(32,21),   V(32,20),   V(32,20)
        v32add          V(32,22),   V(32,21),   V(32,21)

        ; Get each chunk of six bits and shift into scalar registers,
        ; Top 4 bits of the 36 bit vector go into r5, rest in r4
        ;
        ; Correct patterns for fid0:
        ;// d2b63a09d 45685db74 b905c6d4b 2edba16a2
        mov             r4,         0
        vmul32.uu       -,          H(32,0),    H(49,0)         SUMU r3
        mov             r5,         r3
        lsr             r5,         2
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    H(50,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    H(51,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    H(52,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    H(53,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    H(54,0)         SUMU r3
        or              r4,         r3

        ;
        v16bitplanes    -,          0x5555                  SETF
        v32mov          HY(63,0),   r5
        v32mov          HY(57,0),   HY(63,0)                IFZ
        v32mov          HY(63,0),   r4         
        v32mov          HY(57,0),   HY(63,0)                IFNZ

        mov             r4,         0
        vmul32.uu       -,          H(32,0),    V(48,6)         SUMU r3
        mov             r5,         r3
        lsr             r5,         2
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    V(48,5)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    V(48,4)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    V(48,3)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    V(48,2)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,0),    V(48,1)         SUMU r3
        or              r4,         r3

        v16bitplanes    -,          0x5555                      SETF
        v32mov          HY(63,0),   r5
        v32mov          HY(58,0),   HY(63,0)                    IFZ
        v32mov          HY(63,0),   r4         
        v32mov          HY(58,0),   HY(63,0)                    IFNZ

        mov             r4,         0
        vmul32.uu       -,          H(32,16),    H(54,0)         SUMU r3
        mov             r5,         r3
        lsr             r5,         2
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    H(53,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    H(52,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    H(51,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    H(50,0)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    H(49,0)         SUMU r3
        or              r4,         r3

        ;
        v16bitplanes    -,          0x5555                      SETF
        v32mov          HY(63,0),   r5
        v32mov          HY(59,0),   HY(63,0)                    IFZ
        v32mov          HY(63,0),   r4         
        v32mov          HY(59,0),   HY(63,0)                    IFNZ

        mov             r4,         0
        vmul32.uu       -,          H(32,16),    V(48,1)         SUMU r3
        mov             r5,         r3
        lsr             r5,         2
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    V(48,2)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    V(48,3)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    V(48,4)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    V(48,5)         SUMU r3
        or              r4,         r3
        shl             r4,         6
        vmul32.uu       -,          H(32,16),    V(48,6)         SUMU r3
        or              r4,         r3

        v16bitplanes    -,          0x5555                      SETF
        v32mov          HY(63,0),   r5
        v32mov          HY(60,0),   HY(63,0)                    IFZ
        v32mov          HY(63,0),   r4         
        v32mov          HY(60,0),   HY(63,0)                    IFNZ


        rts

.global dump_vrf
dump_vrf:
        ; r0    location of vrf array
        ; 

        mov         r1,         0x40
        v32st       HY(0++,0),  (r0+=r1) REP64

        ;st sp, (r0)
        rts



.macro makeidxblock
        ; Fill 16x16 byte block with ascending byte values
        ; 34+5*16=114
        ;
        ; Make each column have ascending low halfbyte
        mov             r0,         15
        v32mov          V(0,15),    1
        v32mov          V(0,0),     0
        v32add          V(0,1++),   V(0,15), V(0,0++)       REP r0

        ; Make each row have ascending high halfbyte
        v32mov          H(15,0),    0x10
        v32add          H(1++,0),   H(15,0), H(0++,0)       REP r0
        v32mov          H(0++,16),  1                       REP16

        ; We now have a 16x16 block labelled with ascending values
        ; corrsponding to coordinate
        ; Duplicate the low byte to byte 1 so that pixels, 
        ; to become labels, are 16 bits
        v32mov          H(16++,16),  H(16++,0)              REP16
        ; Replace pixels with labels, inverting bits so that background
        ; pixels become highest value
        v32eor          HX(16++,0), HX(16++,0), -1          REP16
        v32and          HX(16++,0), HX(16++,0), 0x7fff      REP16
        v32or           HX(16++,0), HX(16++,0), HX(0++,0)   REP16
.endm



.macro mergedown
        ; Sweep downwards.
        ; 4+15*12=184 cycles
        ;
        ; Compare pixel with three pixels above, if non-background, 
        ; make this pixel the minimum of the ones above and this one.
        ; This works because background are now 0x7fff, rather than 0
        mov             r3,         0
        mov             r0,         0
        mov             r1,         1
        mov             r2,         31
0:
        ; Make offset versions to left and right, fill the appropriate edge with
        ; background. Lower halfword has right-shifted, upper has left-shifted
        v32mov          HX(32,0),       HX(16,0)+r1
        v32mov          HX(32,32),      HX(16,0)+r2
        v32mov          VX(32,32),      0x7eeb
        v32mov          VX(32,15),      0x7eea
        # If bit 12 is set, this is a background pixel
        v32and          -,              HX(17,0)+r0,    0x1000          SETF
        v32min          HX(17,0)+r0,    HX(17,0)+r0,    HX(16,0)+r0     IFZ
        v32min          HX(17,0)+r0,    HX(17,0)+r0,    HX(32,0)        IFZ
        v32min          HX(17,0)+r0,    HX(17,0)+r0,    HX(32,32)       IFZ

        add             r0,         r0,         0x40
        add             r1,         r1,         0x40
        add             r2,         r2,         0x40
        addcmpblt       r3,         1,      15, 0b
.endm

.macro mergeup
        ; Sweep upwards.
        ; 4+15*12=184 cycles
        ;
        ; Compare pixel with three pixels above, if non-background, 
        ; make this pixel the minimum of the ones above and this one.
        ; This works because background are now 0x7fff, rather than 0
        mov             r3,         0
        mov             r0,         15<<6
        mov             r1,         1+(15<<6)
        mov             r2,         31+(15<<6)
0:
        ; Make offset versions to left and right, fill the appropriate edge with
        ; background. Lower halfword has right-shifted, upper has left-shifted
        v32mov          HX(32,0),       HX(16,0)+r1
        v32mov          HX(32,32),      HX(16,0)+r2
        v32mov          VX(32,32),      0x7eeb
        v32mov          VX(32,15),      0x7eea
        # If bit 12 is set, this is a background pixel
        v32and          -,              HX(15,0)+r0,    0x1000          SETF
        v32min          HX(15,0)+r0,    HX(15,0)+r0,    HX(16,0)+r0     IFZ
        v32min          HX(15,0)+r0,    HX(15,0)+r0,    HX(32,0)        IFZ
        v32min          HX(15,0)+r0,    HX(15,0)+r0,    HX(32,32)       IFZ

        sub             r0,         r0,         0x40
        sub             r1,         r1,         0x40
        sub             r2,         r2,         0x40
        addcmpblt       r3,         1,      15, 0b
.endm


.macro mergeright
        ; Sweep rightwards
        ; 3+15*11=168 cycles
        ;
        ; Compare pixel with three pixels to left, if non-background, 
        ; make this pixel the minimum of the ones to the left and this one.
        ; This works because background are now 0x7fff, rather than 0
        mov             r0,         0
        mov             r1,         15<<6
        mov             r2,         17<<6
0:
        v32mov          VX(32,0),       VX(0,0)+r1
        v32mov          VX(32,32),      VX(0,0)+r2
        v32mov          HX(32,0),       0x7eed
        v32mov          HX(47,32),      0x7eec
        # If bit 12 is set, this is a background pixel
        v32and          -,              VX(16,1)+r0,    0x1000          SETF
        # Check pixel to left
        v32min          VX(16,1)+r0,    VX(16,1)+r0,    VX(16,0)+r0     IFZ
        # Check pixels to left above and left below
        v32min          VX(16,1)+r0,    VX(16,1)+r0,    VX(32,0)        IFZ
        v32min          VX(16,1)+r0,    VX(16,1)+r0,    VX(32,32)       IFZ

        add             r1,         r1,         1
        add             r2,         r2,         1
        addcmpblt       r0,         1,      15, 0b
.endm

.macro mergeleft
        ; Sweep leftward
        ; 3+15*11=168 cycles
        ;
        ; Compare pixel with three pixels to right, if non-background, 
        ; make this pixel the minimum of the ones to the right and this one.
        ; This works because background are now 0x7fff, rather than 0
        mov             r0,         14
        mov             r1,         (15<<6)|15
        mov             r2,         (17<<6)|15
0:
        v32mov          VX(32,0),       VX(0,0)+r1
        v32mov          VX(32,32),      VX(0,0)+r2
        v32mov          HX(32,0),       0x7eed
        v32mov          HX(47,32),      0x7eec
        # If bit 12 is set, this is a background pixel
        v32and          -,              VX(16,0)+r0,    0x1000          SETF
        # Check pixel to left
        v32min          VX(16,0)+r0,    VX(16,0)+r0,    VX(16,1)+r0     IFZ
        # Check pixels to left above and left below
        v32min          VX(16,0)+r0,    VX(16,0)+r0,    VX(32,0)        IFZ
        v32min          VX(16,0)+r0,    VX(16,0)+r0,    VX(32,32)       IFZ

        sub             r1,         r1,         1
        sub             r2,         r2,         1
        addcmpbge       r0,         -1,      0, 0b
.endm



.global connect_block
connect_block:
        ; r0 points to the buffer of 32 bit pixels
        ; r1 is the buffer stride
        ;
        ; Provisionally label a 16x16 block.
        ;
        ; In a 16x16 block of pixels, we can give each pixel a unique label
        ; in a byte using its coordinate. Perform a local equiv label merge
        ; in the block, then to global merge by looking at the boundaries between
        ; blocks
        ;
        ; By sweeping in four directions, using the current and trailing three pixels
        ; as the mask, we can guarantee that all stright lines and corners will
        ; be completely labelled. It is possible to construct patterns that
        ; will not be labelled, be we assume these will be rare, due to the 
        ; nature of the fiducials we are trying to detect.
        ;
        ; e.g. downwards using mask below: if d is foreground, choose min of
        ; a,b,c,d
        ;
        ; a b c
        ;   d
        ;
        ; Failing pattern example:
        ;
        ; 1 2 3 4    
        ; 5     8
        ;      
        ; 1 1 1 1
        ; 1     2

        


        stm         r0-r1,  (--sp)          
        ; load 16x16 block of pixels
        v32ld       HY(16++,0), (r0+=r1)    REP16

        ; Main code
        makeidxblock

        mergedown
        mergeleft
        mergeup
        mergeright
        mergedown

        ; change background pix values back
        ; 2+16*4=66 cycles
        mov             r0,             0
        mov             r1,             15
0:
        v32and          -,              HX(16,0)+r0,    0x1000      SETF
        v32mov          HX(16,0)+r0,    0                           IFNZ
        add             r0,             r0,     0x40
        addcmpbge       r1,             -1,     0,      0b

        ;--------------------
        ; Total cycle count excluding memory
        ; 114+184+184+168+168+66=884 cycles

        ldm         r0-r1,      (sp++)          
        v32st       HY(16++,0), (r0+=r1) REP16
        rts




.global get_set
get_set:
        v32ld       HY(32++,0), (r0+=r1)    REP32
        rts

.global union_init_asm
union_init_asm:
        ; mov         r1,         0x40
        ; v32ld       HY(0++,0),  (r0+=r1)    REP64
        ; rts

        ; Fill 16x16 byte block with ascending byte values
        ; 34+5*16=114
        ;
        ; Make each column have ascending low halfbyte
        mov             r1,         r0  ;save dest
        mov             r0,         15
        v32mov          V(0,15),    1
        v32mov          V(0,0),     0
        v32add          V(0,1++),   V(0,15), V(0,0++)       REP r0

        ; Make each row have ascending high halfbyte
        v32mov          H(15,0),    0x10
        v32add          H(1++,0),   H(15,0), H(0++,0)       REP r0
        v32mov          H(0++,16),  1                       REP16

        ; Loop over high byte
        mov             r2,         0
        mov             r3,         0x40    ;stride
        mov             r4,         0x100

0:
        v32mov          H(16,0),    r2
        v32mov          H(0++,16),  H(16,0)     REP16

        v32even         HX(16,0),     HX(0,0), HX(1,0)  
        v32odd          HX(16,32),    HX(0,0), HX(1,0)  
        v32even         HX(17,0),     HX(2,0), HX(3,0)  
        v32odd          HX(17,32),    HX(2,0), HX(3,0)  
        v32even         HX(18,0),     HX(4,0), HX(5,0)  
        v32odd          HX(18,32),    HX(4,0), HX(5,0)  
        v32even         HX(19,0),     HX(6,0), HX(7,0)  
        v32odd          HX(19,32),    HX(6,0), HX(7,0)  
        v32even         HX(20,0),     HX(8,0), HX(9,0)  
        v32odd          HX(20,32),    HX(8,0), HX(9,0)  
        v32even         HX(21,0),     HX(10,0), HX(11,0)  
        v32odd          HX(21,32),    HX(10,0), HX(11,0)  
        v32even         HX(22,0),     HX(12,0), HX(13,0)  
        v32odd          HX(22,32),    HX(12,0), HX(13,0)  
        v32even         HX(23,0),     HX(14,0), HX(15,0)  
        v32odd          HX(23,32),    HX(14,0), HX(15,0)  

        ;v32mov          HX(0++,32), HX(0++,0)   REP16

        v32st           HY(16++,0),  (r1+=r3)    REP8
        add             r1,         r1,         0x400
        addcmpblt       r2,         1,          r4,      0b

        rts



;---------------------------------------------------------------------
; Interesting code fragments
;---------------------------------------------------------------------

        ; This code snippet differentially increments each 32 bit word
        ; of a vector by expanding a mask with bitplanes and using the
        ; result to set lane predication flags
        v32mov          HY(0,0),    0x0
        mov             r2,         0xfffe
        mov             r3,         14
0:
        v16bitplanes    -,          r2      SETF
        v32add          HY(0,0),    HY(0,0),    2     IFNZ
        shl             r2,         r2,         1
        addcmpbge       r3,-1,0,    0b 
        
        ; Use the incremented values in row0, accumulate,
        ; then fetch 32x 16 bit values at 32 bit stride.
        ; The accumulators are used to index the mem fetch
        v32mov          -,          HY(0,0)   CLRA UACC
        v16lookupml     HX(16,0),   (r2) 


        ; Use LUT to transpose. Given an index block of ascending 
        ; byte numbers, we can read a whole block of any width,
        ; (8, 16, 32) into the LUT then write out again
        ; memwrite LUT[B[i]] = A[i]
        ; memread D[i] = LUT[B[i]]
        ;
        ; Make each column have ascending low halfbyte
        mov             r0,         15
        v32mov          V(0,15),    1
        v32mov          V(0,0),     0
        v32add          V(0,1++),   V(0,15), V(0,0++) REP r0

        ; Make each row have ascending high halfbyte
        v32mov          H(15,0),    0x10
        v32add          H(1++,0),   H(15,0), H(0++,0) REP r0

        ; We now have a 16x16 block labelled with ascending values
        ; corrsponding to coordinate 
        ; By fetching vertical vectors into the LUT at ascending
        ; indices and writing out as horizontal, we transpose
        v32memwrite     -,          VY(16,0++), H(0++,0) REP16
        v32memread      HY(16++,0), -,          H(0++,0) REP16

        ;v32memread      HY(16,0)+r0,-,             r15+1
        
        ;----------------------------------------------
        ; Various experiments with memwrite and memread LUT.
        ; This does not seem to work as expected, and I can't find 
        ; any uses of the LUT in the Broadcom code, or in disassembled start.elf.
        ;
        ; Unless the indices are contiguous and start on a 16 entry boundary, the
        ; read data is garbage. No obvious pattern
        ;
        ;

        ;
        ; test memwrite with conflicting values

        ; Clear first 16 entries of LUT
        v32mov          HY(32,0),   0
        v32memwrite     -,          HY(32,0),   H(0,0)

        ; Make indices with duplicates
        v32mov          H(32,0),    H(0,0)      
        v16bitplanes    -,          0x80        SETF

        mov     r0,1
        ;v32mov          H(32,0)+r0,     H(0,0)      IFNZ 
        v32mov          H(32,0)+r0,     0      IFNZ 
        v16bitplanes    -,          0x1        SETF
        v32mov          H(32,0),     15      IFNZ 

        ; Write using these indices, what happens with the multiple
        ; writes targetting entry 0?
        v32mov          HY(32,0),   1
        v32mov          HY(33,0),   0
        v32mov          HY(34,0),   0
        ;v32memwrite     -,          HY(0++,0),    H(0++,0) REP16
        mov             r0,         8
        ;v32memread      HY(33,0),   -,          H(32,0)
        ;v32memread      HY(34,0),   -,          H(32,16)

        ; Seems like the lowest lane wins, higher lanes ignored?
        ;

        v32mov          HY(32,0), HY(4,0)
        v16bitplanes    -,          0x1     SETF
        v32mov          H(32,0),    0x0     IFZ 

        nop
        nop
        nop
        ;v32memwrite     -,          HY(16++,0), H(0++,0) REP16
        mov r0, 0xdeadbeef
        v32mov          HY(16,0),   r0
        v32memwrite     -,          HY(16++,0), HX(0++,0) REP16
        v32memread      HY(33++,0), -,          VX(0,0++) REP16
        
        v32mov      HY(16++,0),0 REP16
        v32mov      HY(32++,0),0 REP16
        v32mov      HY(48++,0),0 REP16

        v32memwrite     -,      HX(0++,0), H(0++,0) REP16

        mov r0,1
        v32mov          HY(32,0), HY(4,0)+r0

        v16bitplanes    -,          0x1     SETF
        ;v32mov          H(32,0),    0x0     IFZ 


        ;v32memwrite     -,          HY(9,0), H(0,0)
        v32memread      HY(33,0),        H(32,0), -
