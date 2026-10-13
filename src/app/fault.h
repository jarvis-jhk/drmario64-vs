/* Crash and fault handling. A crash writes a CRASH: line and exits with 128+signal; a
 * real non-crash fault is logged with FAULT: and posted as one anonymous line. */
#pragma once
void dm_fault_install(void);
void dm_fault(const char *what, const char *detail);
