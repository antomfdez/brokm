/* natives.h - built-in functions and the shared print formatter. */
#ifndef BROKM_NATIVES_H
#define BROKM_NATIVES_H

#include "object.h"
#include "value.h"

/* printf-style formatter shared by the OP_PRINT statement and Print(). The
 * first argument is the format string; remaining args fill its specifiers. */
Value bk_format(int argc, Value *args);

/* Register the standard natives into the VM's global table. */
void natives_register(void);

#endif /* BROKM_NATIVES_H */
