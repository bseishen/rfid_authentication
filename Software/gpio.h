#ifndef _gpio_h
#define _gpio_h

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>


int gpio_export(unsigned int gpio);
int gpio_unexport(unsigned int gpio);
int gpio_set_dir(unsigned int gpio, unsigned int out_flag);
int gpio_set_value(unsigned int gpio, unsigned int value);
int gpio_get_value(unsigned int gpio, unsigned int *value);
int gpio_set_edge(unsigned int gpio, char *edge);
int gpio_fd_open(unsigned int gpio);
int gpio_fd_close(int fd);
int gpio_init(void);
void beep_on(void);
void beep_off(void);
void led_on(void);
void led_off(void);
void led_blink(int times);
void unlock_door(void);
void lock_door(void);
void toggle_garage(void);

#endif /* gpio.h */
