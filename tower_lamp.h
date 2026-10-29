#ifndef TOWER_LAMP_H
#define TOWER_LAMP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tower_lamp tower_lamp_t;

typedef enum {
    TOWER_LAMP_OFF = 0x01,
    TOWER_LAMP_GREEN = 0x02,
    TOWER_LAMP_YELLOW = 0x03,
    TOWER_LAMP_RED = 0x04
} tower_lamp_color_t;

typedef enum {
    TOWER_LAMP_STEADY = 0x01,
    TOWER_LAMP_BLINK_2 = 0x02,
    TOWER_LAMP_BLINK_3 = 0x03,
    TOWER_LAMP_BLINK_4 = 0x04
} tower_lamp_blink_t;

typedef struct {
    tower_lamp_color_t color;
    bool buzzer;
    /* Shared by LED and buzzer; hardware has no independent blink fields. */
    tower_lamp_blink_t blink;
} tower_lamp_state_t;

/* Linux/POSIX, 8N1. Does not send a command. NULL on error, with errno set.
 * timeout_ms is a positive deadline for each command, including its echo.
 * One owner per port; serialize all calls to the same handle. */
tower_lamp_t *tower_lamp_open(const char *port, unsigned baud, int timeout_ms);

/* Does not change outputs. Call tower_lamp_off() first if required. */
void tower_lamp_close(tower_lamp_t *lamp);

/* All int functions return 0 on success, -1 with errno on failure.
 * Success means a matching protocol echo, not optical/acoustic verification.
 * A failed transaction invalidates cached state: the device may have acted. */
int tower_lamp_apply(tower_lamp_t *lamp, const tower_lamp_state_t *state);

/* Changes only the named field, preserving the other confirmed fields.
 * Before first apply(), or after a failed transaction: -1, errno=ENODATA.
 * Another process changing the device makes this cache stale. */
int tower_lamp_set_color(tower_lamp_t *lamp, tower_lamp_color_t color);
int tower_lamp_set_buzzer(tower_lamp_t *lamp, bool enabled);
int tower_lamp_set_blink(tower_lamp_t *lamp, tower_lamp_blink_t blink);

/* Cached confirmed state only; no device query is available. */
int tower_lamp_get_state(const tower_lamp_t *lamp, tower_lamp_state_t *state);

/* Explicitly turns off both light and buzzer and disables blinking. */
int tower_lamp_off(tower_lamp_t *lamp);

#ifdef __cplusplus
}
#endif
#endif
