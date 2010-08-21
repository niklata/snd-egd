#ifndef NJK_INCLUDE_SOUND_H_
#define NJK_INCLUDE_SOUND_H_ 1

void sound_open(void);
int sound_bytes_per_frame(void);
int sound_read(void *buf, size_t size);
void sound_start(void);
void sound_stop(void);
void sound_close(void);
int sound_is_le(void);
int sound_is_be(void);
void sound_set_device(char *str);
void sound_set_port(char *str);
void sound_set_sample_rate(int rate);
void sound_set_skip_bytes(int sb);

#endif

