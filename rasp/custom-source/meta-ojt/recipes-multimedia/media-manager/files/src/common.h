/*
 * common.h - Simple log macros shared by all .c files
 *
 * Each .c file defines LOG_TAG before including this file, e.g.:
 *     #define LOG_TAG "USB"
 *     #include "common.h"
 * Output example:  [I][USB] USB inserted: /dev/sda1
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

#ifndef LOG_TAG
#define LOG_TAG "APP"
#endif

#define LOG_I(fmt, ...) printf("[I][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_W(fmt, ...) printf("[W][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_E(fmt, ...) printf("[E][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)

#endif /* COMMON_H */
