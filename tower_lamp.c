#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "tower_lamp.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct tower_lamp {
    int fd;
    int timeout_ms;
    struct termios original;
    bool known;
    tower_lamp_state_t state;
};

static int fail(int error) { errno = error; return -1; }

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static speed_t baud_speed(unsigned baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return 0;
    }
}

tower_lamp_t *tower_lamp_open(const char *port, unsigned baud, int timeout_ms)
{
    speed_t speed = baud_speed(baud);
    if (!port || !speed || timeout_ms <= 0) { errno = EINVAL; return NULL; }
    tower_lamp_t *lamp = calloc(1, sizeof(*lamp));
    if (!lamp) return NULL;
    lamp->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (lamp->fd < 0) { free(lamp); return NULL; }
    bool have_original = false;
    if (flock(lamp->fd, LOCK_EX | LOCK_NB) < 0) goto error;
    if (tcgetattr(lamp->fd, &lamp->original) < 0) goto error;
    have_original = true;
    struct termios settings = lamp->original;
    cfmakeraw(&settings);
    settings.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    settings.c_cflag |= CS8 | CREAD | CLOCAL;
    if (cfsetispeed(&settings, speed) < 0 || cfsetospeed(&settings, speed) < 0)
        goto error;
    if (tcsetattr(lamp->fd, TCSANOW, &settings) < 0) goto error;
    lamp->timeout_ms = timeout_ms;
    return lamp;
error: {
    int saved = errno;
    if (have_original) (void)tcsetattr(lamp->fd, TCSANOW, &lamp->original);
    close(lamp->fd);
    free(lamp);
    errno = saved;
    return NULL;
    }
}

void tower_lamp_close(tower_lamp_t *lamp)
{
    if (!lamp) return;
    int saved = errno;
    (void)tcsetattr(lamp->fd, TCSANOW, &lamp->original);
    close(lamp->fd);
    free(lamp);
    errno = saved;
}

static int wait_io(int fd, short events, int64_t deadline)
{
    for (;;) {
        int64_t now = now_ms();
        if (now < 0) return -1;
        if (now >= deadline) return fail(ETIMEDOUT);
        struct pollfd p = { .fd = fd, .events = events };
        int result = poll(&p, 1, (int)(deadline - now));
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return -1;
        if (!result) return fail(ETIMEDOUT);
        if (p.revents & events) return 0;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) return fail(EIO);
    }
}

int tower_lamp_apply(tower_lamp_t *lamp, const tower_lamp_state_t *state)
{
    if (!lamp || !state || state->color < TOWER_LAMP_OFF ||
        state->color > TOWER_LAMP_RED || state->blink < TOWER_LAMP_STEADY ||
        state->blink > TOWER_LAMP_BLINK_4) return fail(EINVAL);
    tower_lamp_state_t desired = *state;
    const uint8_t packet[5] = {0xFF, (uint8_t)desired.color,
        desired.buzzer ? 0x02 : 0x01, (uint8_t)desired.blink, 0xAA};
    lamp->known = false;
    int64_t start = now_ms();
    if (start < 0) return -1;
    int64_t deadline = start + lamp->timeout_ms;
    if (tcflush(lamp->fd, TCIFLUSH) < 0) return -1;
    size_t sent = 0;
    while (sent < sizeof(packet)) {
        if (wait_io(lamp->fd, POLLOUT, deadline) < 0) return -1;
        ssize_t n = write(lamp->fd, packet + sent, sizeof(packet) - sent);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n < 0) return -1;
        if (!n) return fail(EIO);
        sent += (size_t)n;
    }
    /* Search a rolling window: tolerate leading noise and fragmented echoes. */
    uint8_t window[5];
    size_t filled = 0;
    for (;;) {
        if (wait_io(lamp->fd, POLLIN, deadline) < 0) return -1;
        uint8_t buf[128];
        ssize_t n = read(lamp->fd, buf, sizeof(buf));
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n < 0) return -1;
        if (!n) return fail(EIO);
        for (ssize_t i = 0; i < n; ++i) {
            if (filled == sizeof(window)) {
                memmove(window, window + 1, sizeof(window) - 1);
                --filled;
            }
            window[filled++] = buf[i];
            if (filled == sizeof(window) && !memcmp(window, packet, sizeof(packet))) {
                lamp->state = desired;
                lamp->known = true;
                return 0;
            }
        }
    }
}

int tower_lamp_get_state(const tower_lamp_t *lamp, tower_lamp_state_t *state)
{
    if (!lamp || !state) return fail(EINVAL);
    if (!lamp->known) return fail(ENODATA);
    *state = lamp->state;
    return 0;
}

int tower_lamp_set_color(tower_lamp_t *lamp, tower_lamp_color_t color)
{
    tower_lamp_state_t state;
    if (tower_lamp_get_state(lamp, &state) < 0) return -1;
    state.color = color;
    return tower_lamp_apply(lamp, &state);
}

int tower_lamp_set_buzzer(tower_lamp_t *lamp, bool enabled)
{
    tower_lamp_state_t state;
    if (tower_lamp_get_state(lamp, &state) < 0) return -1;
    state.buzzer = enabled;
    return tower_lamp_apply(lamp, &state);
}

int tower_lamp_set_blink(tower_lamp_t *lamp, tower_lamp_blink_t blink)
{
    tower_lamp_state_t state;
    if (tower_lamp_get_state(lamp, &state) < 0) return -1;
    state.blink = blink;
    return tower_lamp_apply(lamp, &state);
}

int tower_lamp_off(tower_lamp_t *lamp)
{
    const tower_lamp_state_t state = {TOWER_LAMP_OFF, false, TOWER_LAMP_STEADY};
    return tower_lamp_apply(lamp, &state);
}
