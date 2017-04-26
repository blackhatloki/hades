/* /home/teague/src/samba-4.6.1/bin/default/source4/heimdal/lib/wind/combining_table.h */
/* Automatically generated at 2017-03-28T17:44:13.828209 */

#ifndef COMBINING_TABLE_H
#define COMBINING_TABLE_H 1

#include <krb5-types.h>

struct translation {
  uint32_t key;
  unsigned combining_class;	
};

extern const struct translation _wind_combining_table[];

extern const size_t _wind_combining_table_size;
#endif /* COMBINING_TABLE_H */
