#ifndef __WIFI_H__
#define __WIFI_H__

#include "wifi_config.h"

#define APP_VERSION "v1.0"

void wifi_init(void);
void wifi_wait_connect(void);

#endif /* __WIFI_H__ */
