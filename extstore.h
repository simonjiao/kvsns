#ifndef _POSIX_STORE_H
#define _POSIX_STORE_H

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include "kvsns.h"

int extstore_init(char *rootpath);
int extstore_read(kvsns_ino_t *ino,
		  off_t offset,
		  size_t buffer_size,
		  void *buffer,
		  bool *end_of_file,
		  struct stat *stat);
int extstore_write(kvsns_ino_t *ino,
		   off_t offset,
		   size_t buffer_size,
		   void *buffer,
		   bool *fsal_stable,
		   struct stat *stat);
int extstore_consolidate_attrs(kvsns_ino_t *ino,
			       struct stat *stat);
int extstore_del(kvsns_ino_t *ino);

int extstore_truncate(kvsns_ino_t *ino, off_t filesize);

#endif
