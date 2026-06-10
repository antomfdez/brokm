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

/* Stash the script's command-line arguments for Args(). Called once by the
 * driver (main.c, or the AOT-emitted main) before any code runs; the pointers
 * must stay valid for the life of the process (argv does). */
void natives_set_args(int argc, char **argv);

#endif /* BROKM_NATIVES_H */
