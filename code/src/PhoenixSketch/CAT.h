#ifndef CAT_H
#define CAT_H
#include "SDT.h"

extern int my_ptt;
extern bool catTX;
extern int CATOptions();
extern char *processCATCommand( char *buffer ); //nobody else calls this, don't pollute the namespace.
extern void CATSerialEvent();
void CheckForCATSerialEvents(void);
#endif // CAT_H

