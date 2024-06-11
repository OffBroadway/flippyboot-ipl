#include "print.h"
#include <ogc/lwp_watchdog.h>

#ifdef DOLPHIN_PRINT_ENABLE
int iprintf(const char *fmt, ...) {
    va_list args;
    unsigned long length;
    static char buf[256];
    uint32_t level;

    level = IRQ_Disable();
    va_start(args, fmt);
    length = vsprintf((char *)buf, (char *)fmt, args);

    write(2, buf, length);

	int index = 0;
    for (char *s = &buf[0]; *s != '\x00'; s++) {
        if (*s == '\n')
            *s = '\r';
        int err = WriteUARTN(s, 1);
        if (err != 0) {
            printf("UART ERR: %d\n", err);
        }
        index++;
    }

    va_end(args);
    IRQ_Restore(level);

	return length;
}
#elif defined(CONSOLE_ENABLE) || defined(GECKO_PRINT_ENABLE)
u64 first_print = 0;
int iprintf(const char *fmt, ...) {
    if (first_print == 0) {
        first_print = gettime();
    }

    va_list args;
    unsigned long length;
    static char line[240];
    static char buf[256];
    uint32_t level;

    level = IRQ_Disable();
    va_start(args, fmt);
    // length = vsprintf(line, fmt, args);
    // length = sprintf(buf, "(%f): %s", (f32)diff_usec(first_print, gettime()) / 1000.0, line);
    length = vsprintf(buf, fmt, args);

    write(2, buf, length);

    va_end(args);
    IRQ_Restore(level);

	return length;
}
#else
#define iprintf(...)
#endif
