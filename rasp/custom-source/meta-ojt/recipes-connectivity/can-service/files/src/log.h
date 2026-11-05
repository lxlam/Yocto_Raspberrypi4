/*
 * log.h - log macros (same style as media-manager/common.h)
 * Each .c file defines LOG_TAG before including this file.
 * Output goes to stdout -> journalctl -u can-service
 */
#ifndef CAN_SERVICE_LOG_H
#define CAN_SERVICE_LOG_H

#include <stdio.h>

#ifndef LOG_TAG
#define LOG_TAG "APP"
#endif

#define LOG_I(fmt, ...) printf("[I][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_W(fmt, ...) printf("[W][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_E(fmt, ...) printf("[E][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)

#endif /* CAN_SERVICE_LOG_H */
