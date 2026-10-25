#include <aurora/wpad.h>
#include <revolution/kpad.h>
#include <stddef.h>

_Static_assert(sizeof(WPADStatus) == 42, "WPAD core layout");
_Static_assert(sizeof(WPADFSStatus) == 50, "WPAD Nunchuk layout");
_Static_assert(sizeof(WPADCLStatus) == 54, "WPAD Classic layout");
_Static_assert(sizeof(KPADStatus) == 132, "KPAD layout");
_Static_assert(offsetof(KPADStatus, ex_status) == 96, "KPAD extension offset");
