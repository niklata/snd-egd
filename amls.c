#include <alloca.h>
#include "rb.h"
#include "sound.h"
#include "amls.h"
#include "amls-round.h"
#include "util.h"

extern ring_buffer_t rb;
extern unsigned int stats[2][256];
extern unsigned char max_bit;

size_t amls_renorm_buf(uint16_t buf[], size_t bufsize, int channel)
{
    char *in, *out, *outp;
    size_t i, j;
    size_t insize = 0, amls_out = 0, total_out = 0;
    int topbit, bits_out = 0;
    unsigned char byte_out = 0;

    if (!buf || !bufsize)
        return 0;

    topbit = MIN(max_bit, 16);

    in = alloca(bufsize * topbit);

#ifdef HOST_ENDIAN_BE
    if (sound_is_le()) {
#else
    if (sound_is_be()) {
#endif
        for (i = 0; i < bufsize; ++i)
            buf[i] = endian_swap16(buf[i]);
    }

    for (i = 0; i < topbit; ++i) {
        for (j = 0; j < bufsize; ++j) {
            in[insize++] = (buf[j] >> i) & 1;
        }
    }

    out = alloca(insize);
    outp = out;
    memset(out, '\0', insize);
    amls_round(in, in + insize, &outp);
    amls_out = outp - out;

    for (i = 0; i < amls_out; ++i) {
        byte_out |= out[i] << bits_out;
        bits_out++;

        /* See if we've collected an entire byte.  If so, then copy
         * it into the output buffer. */
        if (bits_out == 8) {
            stats[channel][byte_out] += 1;
            total_out += rb_store_byte_xor(&rb, byte_out);
            bits_out = 0;
            byte_out = 0;

            if (rb_is_full(&rb))
                return total_out;
        }
    }
    return total_out;
}

