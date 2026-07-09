#ifndef __AHT20_H__
#define __AHT20_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    float temperature;
    float humidity;
    uint8_t valid;
} aht20_data_t;

int aht20_init(void);
int aht20_read(aht20_data_t *out);
int aht20_soft_reset(void);
bool aht20_start_measurement(void);
bool aht20_wait_for_measurement(void);
bool aht20_read_measurement(float *temperature, float *humidity);

#endif /* __AHT20_H__ */
