#ifndef VN_H_
#define VN_H_

#include <stdint.h>

typedef struct {
    int bits_out, topbit, total_out;
    int stats;
    char prev_bits[16];
    unsigned char byte_out;
} vn_renorm_state_t;

void vn_renorm_init(vn_renorm_state_t *state);
int vn_renorm_buf(uint16_t buf[], size_t bufsize, vn_renorm_state_t *state);

#endif /* VN_H_ */
