#include "sreset.h"

void wdt_disarm(void)
{
    MCUSR &= ~(_BV(WDRF));
    wdt_disable();

    return;
}
