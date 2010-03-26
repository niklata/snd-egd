#ifndef _NJK_INCLUDE_SOUND_H_
#define _NJK_INCLUDE_SOUND_H_ 1

void sound_open(void);
void sound_read(char *buf, size_t size);
void sound_close(void);
int sound_is_le(void);
int sound_is_be(void);
void sound_set_device(char *str);
void sound_set_port(char *str);
void sound_set_sample_rate(int rate);

#endif

