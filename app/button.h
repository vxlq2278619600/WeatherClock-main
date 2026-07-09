#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdbool.h>

void button_init(void);
bool button_poll_short_press(void);

#endif /* __BUTTON_H__ */
