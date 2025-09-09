#ifndef CAT_H
#define CAT_H
#include "SDT.h"

//#include <usb_seremu.h>
extern int my_ptt;
extern bool catTX;

extern int CATOptions();
extern char *processCATCommand( char *buffer ); //nobody else calls this, don't pollute the namespace.
extern void CATSerialEvent();
void CheckForCATSerialEvents(void);
extern int ChangeBand( long f, bool updateRelays );
//extern usb_seremu_class Seremu;
#endif // CAT_H

