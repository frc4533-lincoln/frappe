

#include <stdio.h>
#include <stdint.h>




uint32_t do_make_bits(uint32_t, uint32_t);
uint32_t do_bandwidth_test(uint32_t, uint32_t, uint32_t);
uint32_t do_load_code(uint32_t);
uint32_t do_hamming_dist(uint32_t, uint32_t, uint32_t);
uint32_t do_hamming_dist_no_load(uint32_t, uint32_t, uint32_t);
uint32_t do_block_empty_test(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t do_extract_channel(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t do_scale(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t dump_vrf(uint32_t);

#define CACHE_SETTING(x, y) ((((x)&~0xc0000000))|((y)<<30))
uint32_t main(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5)
{


    // Set the correct memory region
    //  0 - L1 + L2 cache allocating and coherent
    //  1 - L1 non-allocating and coherent, L2 allocating and coherent
    //  2 - L1 + L2 non-allocating and coherent
    //  3 - No cache
    //
    // Cache mode 3 always works
    //
    // decode hamming, 22 fiducials
    //  0   1.6     working
    //  1   1.8     not working
    //  2   1.8     working
    //  3   1.8     working
    //
    // scale output only
    //  0   1.9     working
    //  1   0.5     not working
    //  2   2.0     working
    //  3   1.3     working

    const int cache = 3;

    uint32_t ret = 0;
    switch (r0)
    {
        case 0:
        {
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t dest    = (r3 & ~0xc0000000) | (cache << 30);

            do_block_empty_test(src, r2, dest, r4, r5);

            break;
        }
        case 1:
        {
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t dest    = (r2 & ~0xc0000000) | (cache << 30);

            ret = do_make_bits(src, r3);
            dump_vrf(dest + 0x200);
            break;
        }
        case 2:
        {
            // Arguments are:
            //  r1  fid buffer
            //  r2  codes buffer
            //  r3  decode output buffer
            //  r4  hamming minimum distance
            //  r5  fid index
            // Decode bits by checking codebook at each location looking for
            // lowest Hamming distance that meets minimum distance.
            uint32_t fid     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t codes   = (r2 & ~0xc0000000) | (cache << 30);
            uint32_t buffer  = (r3 & ~0xc0000000) | (cache << 30);

            // Codes contains the set of 250 possible tags. We need to compare the 
            // bit pattern in the four entries in bits (one for each orientation) to
            // find the code with the lowest Hamming distance


            // For the particular fid index, extract the bit patterns for the
            // four orientations, leaving them in:
            // HY(57,0) HY(58,0), HY(59,0), HY(60,0)
            do_make_bits(fid, r5);

            ret = do_hamming_dist(codes, buffer, r4);

            //dump_vrf(buffer + 0x200);
            break;
        }
        case 3:
        {
            // Extract a single channel
            //  r1  input buffer
            //  r2  output buffer
            //  r3  width
            //  r4  height
            //  r5  input stride (in pixels)
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t dest    = (r2 & ~0xc0000000) | (cache << 30);
            do_extract_channel(src, dest, r3, r4, r5);
            break;
        }
        case 4:
        {
            // Load codebook
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);
            do_load_code(src);
            break;
        }
        case 5:
        {
            // scale by nearest neighbour decimation
            //  r1  source buffer
            //  r2  dest buffer
            //  r3  width
            //  r4  height
            //  r5  input stride (in pixels)
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t dest    = (r2 & ~0xc0000000) | (cache << 30);
            do_scale(src, dest, r3, r4, r5);
            dump_vrf(dest + 0x200);
            break;
        }
        case 6:
        {
            // Arguments are:
            //  r1  fid buffer
            //  r2  codes buffer
            //  r3  decode output buffer
            //  r4  hamming minimum distance
            //  r5  number of fids
            // Decode bits by checking codebook at each location looking for
            // lowest Hamming distance that meets minimum distance.
            uint32_t fid     = (r1 & ~0xc0000000) | (cache << 30);
            uint32_t codes   = (r2 & ~0xc0000000) | (cache << 30);
            uint32_t buffer  = (r3 & ~0xc0000000) | (cache << 30);

            // Codes contains the set of 250 possible tags. We need to compare the 
            // bit pattern in the four entries in bits (one for each orientation) to
            // find the code with the lowest Hamming distance
            do_load_code(codes);

            // For the particular fid index, extract the bit patterns for the
            // four orientations, leaving them in:
            // HY(57,0) HY(58,0), HY(59,0), HY(60,0)
            for(int i = 0; i < r5; i++)
            {
                do_make_bits(fid, i);
                do_hamming_dist_no_load(codes, buffer + (i << 2), r4);
            }
            //dump_vrf(buffer + 0x200);
            break;
        }
        case 7:
        {
            // Read memory for bandwidth test
            //  r1  source buffer
            //  r2  stride
            //  r3  count
            uint32_t src     = (r1 & ~0xc0000000) | (cache << 30);

            do_bandwidth_test(src, r2, r3);

            break;
        }
    }


    return ret;

    

}
