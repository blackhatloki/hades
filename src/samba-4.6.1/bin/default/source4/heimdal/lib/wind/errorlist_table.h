/* /home/teague/src/samba-4.6.1/bin/default/source4/heimdal/lib/wind/errorlist_table.h */
/* Automatically generated at 2017-03-28T17:44:11.966474 */

#ifndef ERRORLIST_TABLE_H
#define ERRORLIST_TABLE_H 1

#include "windlocl.h"

struct error_entry {
  uint32_t start;
  unsigned len;
  wind_profile_flags flags;
};

extern const struct error_entry _wind_errorlist_table[];

extern const size_t _wind_errorlist_table_size;

#endif /* ERRORLIST_TABLE_H */
