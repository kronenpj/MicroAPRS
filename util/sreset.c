#include "sreset.h"

void wdt_disarm(void)
{
    MCUSR = 0;
    wdt_disable();

    return;
}
