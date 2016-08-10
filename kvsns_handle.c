#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kvsns.h"
#include "kvsns_internal.h"
#include "extstore.h"

static int kvsns_set_store_url()
{
	int rc;
	char k[KLEN];
	char v[VLEN];
	char *store = NULL;

	snprintf(k, KLEN, "store_url");
	store = getenv(KVSNS_STORE_ENV);
	if (store == NULL) {
		rc = kvsal_get_char(k,v);
		if (rc == 0)
			store = v;
		else
			store = kvsns_store_default;
	}
	RC_WRAP( rc, kvsal_set_char, k, store);

	snprintf(kvsns_store_base, MAXPATHLEN, "%s", store);	
	printf( "====> kvsns_store_base=%s\n", kvsns_store_base);

	return 0;
}

int kvsns_start(void)
{
	int rc;
	char k[KLEN];
	char v[VLEN];

	RC_WRAP(rc, kvsal_init);

	RC_WRAP(rc, kvsns_set_store_url);

	snprintf(k, KLEN, "store_url");
	RC_WRAP(rc, kvsal_get_char, k, v);

	RC_WRAP(rc, extstore_init, v);

	/** @todo : remove all existing opened FD (crash recovery) */
	return 0;
}

int kvsns_init_root(int openbar)
{
	int rc;
	char k[KLEN];
	char v[KLEN];
	struct stat bufstat;
	kvsns_cred_t cred;
	kvsns_ino_t ino;

	cred.uid= 0;
	cred.gid = 0;
	ino = KVSNS_ROOT_INODE;

	kvsns_set_store_url();

	snprintf(k, KLEN, "%llu.parentdir", ino);
	snprintf(v, VLEN, "%llu|", ino);
	RC_WRAP( rc, kvsal_set_char, k, v);

	snprintf(k, KLEN, "ino_counter");
	snprintf(v, VLEN, "3");
	RC_WRAP( rc, kvsal_set_char, k, v);

	/* Set stat */
	memset(&bufstat, 0, sizeof(struct stat));
	if (openbar != 0)
		bufstat.st_mode = S_IFDIR|0777;
	else
		bufstat.st_mode = S_IFDIR|0755;
	bufstat.st_ino = KVSNS_ROOT_INODE;
	bufstat.st_nlink = 2;
	bufstat.st_uid = 0;
	bufstat.st_gid = 0;
	bufstat.st_atim.tv_sec = 0;
	bufstat.st_mtim.tv_sec = 0;
	bufstat.st_ctim.tv_sec = 0;

	snprintf(k, KLEN, "%llu.stat", ino);
	RC_WRAP(rc, kvsal_set_stat, k, &bufstat);

	return 0;
}

int kvsns_fsstat(kvsns_fsstat_t *stat)
{
	char k[KLEN];
	int rc;

	if (!stat)
		return -EINVAL;

	snprintf(k, KLEN, "*.stat");
	rc = kvsal_get_list_size(k);
	if (rc < 0 )
		return rc;

	stat->nb_inodes = rc;
	return 0;
}

int kvsns_get_root(kvsns_ino_t *ino)
{
	if (!ino)
		return -EINVAL;

	*ino = KVSNS_ROOT_INODE;
	return 0;
}

int kvsns_mkdir(kvsns_cred_t *cred, kvsns_ino_t *parent, char *name,
		mode_t mode, kvsns_ino_t *newdir)
{
	int rc;

	RC_WRAP(rc, kvsns_access, cred, parent, KVSNS_ACCESS_WRITE);

	return kvsns_create_entry(cred, parent, name, NULL,
				  mode, newdir, KVSNS_DIR);
}

int kvsns_symlink(kvsns_cred_t *cred, kvsns_ino_t *parent, char *name,
		  char *content, kvsns_ino_t *newlnk)
{
	int rc;
	char k[KLEN];
	struct stat bufstat;
	struct stat parent_stat;

	if (!cred || !parent || ! name || !content || !newlnk)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, parent, KVSNS_ACCESS_WRITE);
	RC_WRAP(rc, kvsns_get_stat, parent, &parent_stat);

	RC_WRAP(rc, kvsns_create_entry, cred, parent, name, content,
			0, newlnk, KVSNS_SYMLINK);

	RC_WRAP(rc, kvsns_update_stat, parent, STAT_MTIME_SET|STAT_CTIME_SET);

	return 0;
}

int kvsns_readlink(kvsns_cred_t *cred, kvsns_ino_t *lnk, 
		  char *content, size_t *size) 
{
	int rc;
	char k[KLEN];
	char v[KLEN];

	/* No access check, a symlink's content is always readable */

	if (!cred || !lnk || !content || !size)
		return -EINVAL;

	snprintf(k, KLEN, "%llu.link", *lnk);
	RC_WRAP(rc, kvsal_get_char, k, v);

	strncpy(content, v, *size);
	*size = strnlen(v, VLEN); 
	
	RC_WRAP(rc, kvsns_update_stat, lnk, STAT_ATIME_SET);

	return 0;
}

int kvsns_rmdir(kvsns_cred_t *cred, kvsns_ino_t *parent, char *name)
{
	int rc;
	char k[KLEN];
	kvsns_ino_t ino;
	struct stat parent_stat;

	if (!cred || !parent || !name)
		return -EINVAL; 
	
	RC_WRAP(rc, kvsns_access, cred, parent, KVSNS_ACCESS_WRITE);

	RC_WRAP(rc, kvsns_lookup, cred, parent, name, &ino);

	RC_WRAP(rc, kvsns_get_stat, parent, &parent_stat);

	snprintf(k, KLEN, "%llu.dentries.*", ino);
	rc = kvsal_get_list_size(k);
	if (rc > 0)
		return -ENOTEMPTY;

	RC_WRAP(rc, kvsal_begin_transaction);

	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *parent, name);
	RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

	snprintf(k, KLEN, "%llu.parentdir", ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

	snprintf(k, KLEN, "%llu.stat", ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

	RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &parent_stat,
		      STAT_CTIME_SET|STAT_MTIME_SET);
	RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, parent, &parent_stat);

	RC_WRAP(rc, kvsal_end_transaction);

	/* Remove all associated xattr */
	RC_WRAP(rc, kvsns_remove_all_xattr, cred, &ino);

	return 0;

aborted:
	kvsal_discard_transaction();
	return rc;
}

int kvsns_readdir(kvsns_cred_t *cred, kvsns_ino_t *dir, off_t offset, 
		  kvsns_dentry_t *dirent, int *size)
{
	int rc;
	char pattern[KLEN];
	char v[VLEN];
	kvsal_item_t *items;
	int i; 
	kvsns_ino_t ino;

	if (!cred || !dir || !dirent || !size)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, dir, KVSNS_ACCESS_READ);

	items = (kvsal_item_t *)malloc(*size*sizeof(kvsal_item_t));
	if (items == NULL)
		return -ENOMEM;


	snprintf(pattern, KLEN, "%llu.dentries.*", *dir);
	rc = kvsal_get_list(pattern, (int)offset, size, items);
	if (rc < 0)
		return rc;

	for (i=0; i < *size ; i++) {
		sscanf(items[i].str, "%llu.dentries.%s\n", 
		       &ino, dirent[i].name);

		RC_WRAP(rc, kvsal_get_char, items[i].str, v);

		sscanf(v, "%llu", &dirent[i].inode);		

		RC_WRAP(rc, kvsns_getattr, cred, &dirent[i].inode,
			 &dirent[i].stats);
	} 

	RC_WRAP(rc, kvsns_update_stat, dir, STAT_ATIME_SET);

	return 0;
}

int kvsns_lookup(kvsns_cred_t *cred, kvsns_ino_t *parent, char *name,
		kvsns_ino_t *ino)
{
	int rc;
	char k[KLEN];
	char v[VLEN];

	if (!cred || !parent || !name || !ino)
		return -EINVAL; 

	RC_WRAP(rc, kvsns_access, cred, parent, KVSNS_ACCESS_READ);

	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *parent, name);

	RC_WRAP(rc, kvsal_get_char, k, v);

	sscanf(v, "%llu", ino);

	return 0;
}

int kvsns_lookupp(kvsns_cred_t *cred, kvsns_ino_t *dir, kvsns_ino_t *parent)
{
	int rc;
	char k[KLEN];
	char v[VLEN];

	if (!cred || !dir || !parent)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, dir, KVSNS_ACCESS_READ);

	snprintf(k, KLEN, "%llu.parentdir", 
		 *dir);

	RC_WRAP(rc, kvsal_get_char, k, v);

	sscanf(v, "%llu|", parent);

	return 0;
}

int kvsns_getattr(kvsns_cred_t *cred, kvsns_ino_t *ino, struct stat *bufstat) 
{
	char k[KLEN];
	int rc;

	if (!cred || !ino || !bufstat)
		return -EINVAL;

	snprintf(k, KLEN, "%llu.stat", *ino);
	return kvsal_get_stat(k, bufstat);
}

int kvsns_setattr(kvsns_cred_t *cred, kvsns_ino_t *ino, struct stat *setstat, int statflag) 
{
	char k[KLEN];
	struct stat bufstat;
	int rc;
	struct timeval t;

	if (!cred || !ino || !setstat)
		return -EINVAL;

	if (statflag == 0)
		return 0; /* Nothing to do */

	if (gettimeofday(&t, NULL) != 0)
		return -errno;

	RC_WRAP(rc, kvsns_access, cred, ino, KVSNS_ACCESS_WRITE);

	snprintf(k, KLEN, "%llu.stat", *ino);
	RC_WRAP(rc, kvsal_get_stat, k, &bufstat);

	/* ctime is to be updated if md are changed */
	bufstat.st_ctim.tv_sec = t.tv_sec;
	bufstat.st_ctim.tv_nsec = 1000 * t.tv_usec;
	
	if (statflag & STAT_MODE_SET)
		bufstat.st_mode = setstat->st_mode;

	if (statflag & STAT_UID_SET)
		bufstat.st_uid = setstat->st_uid;
		
	if (statflag & STAT_GID_SET)
		bufstat.st_gid = setstat->st_gid;
		
	if (statflag & STAT_SIZE_SET) {
		bufstat.st_size = setstat->st_size;
		bufstat.st_mtim.tv_sec = t.tv_sec;
		bufstat.st_mtim.tv_nsec = 1000 * t.tv_usec;
	}
		
	if (statflag & STAT_ATIME_SET) {
		bufstat.st_atim.tv_sec = setstat->st_atim.tv_sec;
		bufstat.st_atim.tv_nsec = setstat->st_atim.tv_nsec;
	}		

	if (statflag & STAT_MTIME_SET) {
		bufstat.st_mtim.tv_sec = setstat->st_mtim.tv_sec;
		bufstat.st_mtim.tv_nsec = setstat->st_mtim.tv_nsec;
	}		

	if (statflag & STAT_CTIME_SET) {
		bufstat.st_ctim.tv_sec = setstat->st_ctim.tv_sec;
		bufstat.st_ctim.tv_nsec = setstat->st_ctim.tv_nsec;
	}		

	return kvsal_set_stat(k, &bufstat);
}

int kvsns_link(kvsns_cred_t *cred, kvsns_ino_t *ino,
	       kvsns_ino_t *dino, char *dname)
{
	int rc;
	char k[KLEN];
	char v[VLEN];
	kvsns_ino_t tmpino;
	kvsns_ino_t parent[KVSNS_ARRAY_SIZE];
	struct stat dino_stat;
	struct stat ino_stat;
	int size ;

	if (!cred || !ino || !dino || !dname)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, dino, KVSNS_ACCESS_WRITE);

	rc = kvsns_lookup(cred, dino, dname, &tmpino);
	if (rc == 0)
		return -EEXIST;

	RC_WRAP(rc, kvsns_get_stat, parent, &dino_stat);
	RC_WRAP(rc, kvsns_get_stat, parent, &ino_stat);

	RC_WRAP(rc, kvsal_begin_transaction);

	snprintf(k, KLEN, "%llu.parentdir", *ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_get_char, k, v);

	snprintf(k, KLEN, "%llu|", *dino);
	strcat(v,k);
 
	snprintf(k, KLEN, "%llu.parentdir", *ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);

	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *dino, dname);
	snprintf(v, VLEN, "%llu", *ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);

	RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &ino_stat, STAT_CTIME_SET|STAT_INCR_LINK);
	RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, ino, &ino_stat);

	RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &dino_stat,
		      STAT_CTIME_SET|STAT_MTIME_SET);
	RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, dino, &dino_stat);

	RC_WRAP(rc, kvsal_end_transaction);

	return 0;

aborted:
	kvsal_discard_transaction();
	return rc;
}

int kvsns_unlink(kvsns_cred_t *cred, kvsns_ino_t *dir, char *name)
{
	int rc;
	char k[KLEN];
	char v[VLEN];
	kvsns_ino_t ino;
	kvsns_ino_t parent[KVSNS_ARRAY_SIZE];
	struct stat ino_stat;
	struct stat dir_stat;
	int size;
	int i;
	bool opened;
	bool deleted;

	opened = false;
	deleted = false;

	if (!cred || !dir || !name)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, dir, KVSNS_ACCESS_WRITE);

	RC_WRAP(rc, kvsns_lookup, cred, dir, name, &ino);

	RC_WRAP(rc, kvsns_get_stat, dir, &dir_stat);
	RC_WRAP(rc, kvsns_get_stat, &ino, &ino_stat);
	
	snprintf(k, KLEN, "%llu.parentdir", ino);
	RC_WRAP(rc, kvsal_get_char, k, v);

	size = KVSNS_ARRAY_SIZE;
	RC_WRAP(rc, kvsns_str2parentlist, parent, &size, v);

	/* Check if file is opened */
	snprintf(k, KLEN, "%llu.openowner", ino);
	rc = kvsal_exists(k);
	if ((rc != 0) && (rc != -ENOENT))
		return rc;

	opened = (rc == -ENOENT) ? false : true ;

	RC_WRAP(rc, kvsal_begin_transaction);

	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *dir, name);
	RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

	if (size == 1) {
		/* Last link, try to perform deletion */
		RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

		snprintf(k, KLEN, "%llu.stat", ino);
		RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

		if (!opened) {
			RC_WRAP_LABEL(rc, aborted, extstore_del, &ino);
		} else {
			/* File is opened, deleted it at last close */
			snprintf(k, KLEN, "%llu.opened_and_deleted", ino);
			snprintf(v, VLEN, "1");
			RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);
		}

		/* Remove all associated xattr */
		deleted = true;
	} else {
		for(i=0; i < size ; i++)
			if (parent[i] == *dir) {
				/* In this list mgmt, setting value 0
				 * will make it ignored as str is rebuilt */ 
				parent[i] = 0;
				break;
			}
		RC_WRAP_LABEL(rc, aborted, kvsns_parentlist2str, parent, size, v);
		RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);

		RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &ino_stat,
			 STAT_CTIME_SET|STAT_DECR_LINK);
		RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, &ino, &ino_stat);
	}

	RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &dir_stat,
		      STAT_MTIME_SET|STAT_CTIME_SET);
	RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, dir, &dir_stat);

	RC_WRAP(rc, kvsal_end_transaction);
	
	if (deleted)
		RC_WRAP(rc, kvsns_remove_all_xattr, cred, &ino);
	return 0;

aborted:
	kvsal_discard_transaction();
	return rc;
}

int kvsns_rename(kvsns_cred_t *cred,  kvsns_ino_t *sino,
		 char *sname, kvsns_ino_t *dino, char *dname)
{
	int rc;
	char k[KLEN];
	char v[VLEN];
	kvsns_ino_t ino;
	kvsns_ino_t parent[KVSNS_ARRAY_SIZE];
	struct stat sino_stat;
	struct stat dino_stat;
	int size;
	int i;

	if (!cred || !sino || !sname || !dino || !dname)
		return -EINVAL;

	RC_WRAP(rc, kvsns_access, cred, sino, KVSNS_ACCESS_WRITE);

	RC_WRAP(rc, kvsns_access, cred, dino, KVSNS_ACCESS_WRITE);

	rc = kvsns_lookup(cred, dino, dname, &ino);
	if (rc ==0)
		return -EEXIST;

	RC_WRAP(rc, kvsns_get_stat, sino, &sino_stat);
	if (*sino != *dino)
		RC_WRAP(rc, kvsns_get_stat, dino, &dino_stat);

	RC_WRAP(rc, kvsns_lookup, cred, sino, sname, &ino);

	snprintf(k, KLEN, "%llu.parentdir", ino);
	RC_WRAP(rc, kvsal_get_char, k, v);

	size = KVSNS_ARRAY_SIZE;
	RC_WRAP(rc, kvsns_str2parentlist, parent, &size, v);
	for(i=0; i < size ; i++)
		if (parent[i] == *sino) {
			parent[i] = *dino;
			break;
		}

	RC_WRAP(rc, kvsal_begin_transaction);
	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *sino, sname);
	RC_WRAP_LABEL(rc, aborted, kvsal_del, k);

	snprintf(k, KLEN, "%llu.dentries.%s", 
		 *dino, dname);
	snprintf(v, VLEN, "%llu", ino);
	RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);

	snprintf(k, KLEN, "%llu.parentdir", ino);
	RC_WRAP_LABEL(rc, aborted, kvsns_parentlist2str, parent, size, v);
	RC_WRAP_LABEL(rc, aborted, kvsal_set_char, k, v);

	RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat,
	        &sino_stat, STAT_CTIME_SET|STAT_MTIME_SET);
	RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, sino, &sino_stat);
	if (*sino != *dino) {
		RC_WRAP_LABEL(rc, aborted, kvsns_amend_stat, &dino_stat,
			 STAT_CTIME_SET|STAT_MTIME_SET);
		RC_WRAP_LABEL(rc, aborted, kvsns_set_stat, dino, &dino_stat);
	}
	RC_WRAP(rc, kvsal_end_transaction);
	return 0;

aborted:
	kvsal_discard_transaction();
	return rc;
}


int kvsns_mr_proper(void)
{
	int rc;
	char pattern[KLEN];
	char v[VLEN];
	kvsal_item_t items[KVSNS_ARRAY_SIZE];
	int i; 
	int size;

	snprintf(pattern, KLEN, "*");

	do {
		size = KVSNS_ARRAY_SIZE;
		rc = kvsal_get_list(pattern, 0, &size, items);
		if (rc < 0)
			return rc;
	
		for (i=0; i < size ; i++) {
			RC_WRAP(rc, kvsal_del, items[i].str);
		}
	} while(size > 0);

	return 0;
}

