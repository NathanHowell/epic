/*
 * hash.h: hash table structures and prototypes
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYRIGHT file for more information on licensing and use.
 * 
 * $Id: hash.h,v 1.1.2.2 2003/02/27 18:15:57 wd Exp $
 */

#ifndef HASH_H
#define HASH_H

#include "bsdqueue.h"

typedef struct hashtable hashtable_t;

SLIST_HEAD(hashbucket, hashent);
struct hashent {
    void    *ent;

    SLIST_ENTRY(hashent) lp;
};

typedef int (*hash_cmpfunc)(void *, void *, size_t);

struct hashtable {
    int	    size;	    /* thte size of the hash table */
#define hashtable_size(x) ((x)->size)
    int	    entries;	    /* number of entries in the hash table */
#define hashtable_count(x) ((x)->entries)
    struct hashbucket *table;	/* our table */
    size_t  keyoffset;	    /* this stores the offset of the key from the
			       given structure */
    size_t  keylen;	    /* the length of the key.  this CANNOT be 0.  If
			       you want to use variable length strings use the
			       flag below. */

    /* these are useful for debugging the state of the hash system */
#ifdef DEBUG_CODE
    int	    max_per_bucket; /* most entries in a single bucket */
    int	    empty_buckets;  /* number of empty buckets */
#endif
#define HASH_FL_NOCASE 0x1	/* ignore case (tolower before hash) */
#define HASH_FL_STRING 0x2	/* key is a nul-terminated string, treat len
				   as a maximum length to hash */
#define HASH_FL_INSERTHEAD 0x4	/* insert values at the head of their
				   respective buckets (default) */
#define HASH_FL_INSERTTAIL 0x8	/* insert values at the tail of their bucket */
    int	    flags;
    /* the symbol for our comparison function, used in hash_find_ent().  This
     * behaves much like the compare function used in qsort().  This means that
     * a return of 0 (ZERO) means success!  (this lets you use stuff like
     * strncmp easily).  We expect a symbol with a type of:
     * int (*)(void *, void *, size_t).  If the value is NULL then the memcmp
     * function is used instead (with a little kludge-work ;) */
    hash_cmpfunc cmpsym;
};

extern struct heap *hashent_heap; /* allocated for us elsewhere */

/* table management functions */
hashtable_t *create_hash_table(int, size_t, size_t, int, hash_cmpfunc);
void destroy_hash_table(hashtable_t *);
void resize_hash_table(hashtable_t *table, int elems);
unsigned int hash_get_key_hash(hashtable_t *, void *, size_t);
int hash_insert(hashtable_t *, void *);
int hash_delete(hashtable_t *, void *);
#define hash_find_bucket(tbl, key) \
(&tbl->table[hash_get_key_hash(tbl, key, 0)])
void *hash_find(hashtable_t *, void *);
struct hashent *hash_find_ent(hashtable_t *, void *);
struct hashent *hash_find_ent_next(hashtable_t *, void *, struct hashent *);
#endif
/* vi:set ts=8 sts=4 sw=4 tw=79: */
