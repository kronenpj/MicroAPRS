#include <avr/wdt.h>

void wdt_disarm(void) __attribute__((naked)) __attribute__((section(".init3")));

#define soft_reset()        \
do                          \
{                           \
    wdt_enable(WDTO_1S);    \
    for(;;)                 \
    {                       \
    }                       \
} while(0)


