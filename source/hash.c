/*
 * hash.c: hash table support functions
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYRIGHT file for more information on licensing and use.
 * 
 * This system implements a generic hash-table systeem.  The workings of
 * hash-tables are discussed in almost any simple C algorithm book, so I won't
 * go into deteail here.
 */

#include "irc.h"
#include "hash.h"

/* this function creates a hashtable with 'elems' buckets (elems can be any
 * size and remain efficient).  'offset' is the offset of the key from
 * structures being added (this should be obtained with the 'offsetof()'
 * function).  len is the length of the key, and flags are any flags for the
 * table (see hash.h).  cmpfunc is the function which should be used for
 * comparison when calling 'hash_find' */
hashtable_t *create_hash_table(int elems, size_t offset, size_t len,
	int flags, hash_cmpfunc cmpsym) {
    hashtable_t *htp = malloc(sizeof(hashtable_t));

    htp->size = elems;
    htp->entries = 0;
    htp->keyoffset = offset;
    htp->keylen = len;
#ifdef DEBUG_CODE
    htp->max_per_bucket = 1;
    htp->empty_buckets = elems;
#endif
    htp->flags = flags;
    if (cmpsym != NULL)
	htp->cmpsym = cmpsym;
    else
	htp->cmpsym = (hash_cmpfunc)memcmp;

    htp->table = malloc(sizeof(struct hashbucket) * htp->size);
    memset(htp->table, 0, sizeof(struct hashbucket) * htp->size);

    return htp;
}

/* hash_table destroyer.  sweep through the given table and kill off every
 * hashent */
void destroy_hash_table(hashtable_t *table) {
    struct hashent *hep;
    int i;

    for (i = 0;i < table->size;i++) {
	while (!SLIST_EMPTY(&table->table[i])) {
	    hep = SLIST_FIRST(&table->table[i]);
	    SLIST_REMOVE_HEAD(&table->table[i], lp);
	    free(hep);
	}
    }
    free(table->table);
    free(table);
}

/* this function is used to resize a hash table.  of course, to do this, it has
 * to create a new table and re-hash everything in it, so it's not very
 * efficient, however if used judiciously it should enhance performance, not
 * hinder it.  'elems' is the new size of the table. */
void resize_hash_table(hashtable_t *table, int elems) {
    struct hashbucket *oldtable;
    int oldsize, i;
    struct hashent *hep;

    /* preserve the old table, then create a new one.  */
    oldtable = table->table;
    oldsize = table->size;
    table->size = elems;
    table->entries = 0;
#ifdef DEBUG_CODE
    table->max_per_bucket = 1;
    table->empty_buckets = elems;
#endif
    table->table = malloc(sizeof(struct hashbucket) * table->size);
    memset(table->table, 0, sizeof(struct hashbucket) * table->size);

    /* now walk each bucket in the old table, pulling off individual entries
     * and re-adding them to the table as we go */
    for (i = 0;i < oldsize;i++) {
	while (!SLIST_EMPTY(&oldtable[i])) {
	    hep = SLIST_FIRST(&oldtable[i]);
	    hash_insert(table, hep->ent);
	    SLIST_REMOVE_HEAD(&oldtable[i], lp);
	    free(hep);
	}
    }
    free(oldtable);
}

/* this function allows you to get the hash of a given key.  it must be used in
 * the context of the table, of course.  it is mostly useful for insert/delete
 * below, and also for searching, but the included function should do that for
 * you adequately. */
unsigned int hash_get_key_hash(hashtable_t *table, void *key,
	size_t offset) {
    char *rkey = (char *)key + offset;
    int len = table->keylen;
    unsigned int hash = 0;

    if (table->flags & HASH_FL_STRING) {
	len = strlen(rkey);
	if (len > table->keylen)
	    len = table->keylen;
    }
    /* I borrowed this algorithm from perl5.  Kudos to Larry Wall & co. */
    if (table->flags & HASH_FL_NOCASE)
	while (len--)
	    hash = hash * 33 + tolower(*rkey++);
    else
	while (len--)
	    hash = hash * 33 + *rkey++;

    return hash % table->size;
}

/* add the entry into the table */
int hash_insert(hashtable_t *table, void *ent) {
    int hash = hash_get_key_hash(table, ent, table->keyoffset);
    struct hashent *hep = malloc(sizeof(struct hashent));

#ifdef DEBUG_CODE
    if (SLIST_EMPTY(&table->table[hash]))
	table->empty_buckets--; /* this bucket isn't empty now */
    else {
	/* count the items in the bucket.  of course this is expensive, that's
	 * why you don't debug code in production. :) */
	struct hashent *hep2;
	int cnt = 0;
	SLIST_FOREACH(hep2, &table->table[hash], lp) {
	    cnt++;
	}
	if (cnt > table->max_per_bucket)
	    table->max_per_bucket = cnt;
    }
#endif

    table->entries++;
    hep->ent = ent;
    if (table->flags & HASH_FL_INSERTTAIL) {
	/* find the end of the list and insert data there.  this is costly,
	 * and probably not something you want to do unless you're really sure
	 * that it's what you're after. */
	struct hashent *hep2;

	hep2 = SLIST_FIRST(&table->table[hash]);
	while (hep2 != NULL)
	    if (SLIST_NEXT(hep2, lp) == NULL)
		break;
	    else
		hep2 = SLIST_NEXT(hep2, lp);

	if (hep2 == NULL)
	    SLIST_INSERT_HEAD(&table->table[hash], hep, lp);
	else
	    SLIST_INSERT_AFTER(hep2, hep, lp);
    } else
	SLIST_INSERT_HEAD(&table->table[hash], hep, lp);

    /* if the table has twice as many entries as there are buckets, resize it
     * so that there are twice as many buckets as entries. :) */
    if (table->size * 2 <= table->entries)
	resize_hash_table(table, table->size * 4);

    return 1;
}

/* remove the entry from the table. */
int hash_delete(hashtable_t *table, void *ent) {
    int hash = hash_get_key_hash(table, ent, table->keyoffset);
    struct hashent *hep;

    SLIST_FOREACH(hep, &table->table[hash], lp) {
	if (hep->ent == ent)
	    break;
    }
    if (hep == NULL)
	return 0;

    table->entries--;
    SLIST_REMOVE(&table->table[hash], hep, hashent, lp);
    free(hep);

#ifdef DEBUG_CODE
    if (SLIST_EMPTY(&table->table[hash]))
	table->empty_buckets++; /* this bucket is empty again. */
#endif

    return 1;
}

/* last, but not least, the find functions.  given the table and the key to
 * look for, it hashes the key, and then calls the compare function in the
 * given table slice until it finds the item, or reaches the end of the
 * list. */
void *hash_find(hashtable_t *table, void *key) {
    struct hashbucket *bucket = hash_find_bucket(table, key);
    struct hashent *hep;

    SLIST_FOREACH(hep, bucket, lp) {
	if (!table->cmpsym(&((char *)hep->ent)[table->keyoffset], key,
		    table->keylen))
	    return hep->ent;
    }

    return NULL; /* not found */
}

/* this function finds the first 'hashent' of the given key, it returns a
 * hashent so that calls to the _next variant can be a bit faster. */
struct hashent *hash_find_ent(hashtable_t *table, void *key) {
    struct hashbucket *bucket = hash_find_bucket(table, key);
    struct hashent *hep;

    SLIST_FOREACH(hep, bucket, lp) {
	if (!table->cmpsym(&((char *)hep->ent)[table->keyoffset], key,
		    table->keylen))
	    return hep;
    }

    return NULL; /* not found */
}

/* this function finds the next entry matching the key after 'ent' */
struct hashent *hash_find_ent_next(hashtable_t *table, void *key,
	struct hashent *ent) {
    struct hashent *hep = ent;

    /* we know where we are .. */
    while ((hep = SLIST_NEXT(hep, lp)) != NULL) {
	if (!table->cmpsym(&((char *)hep->ent)[table->keyoffset], key,
		    table->keylen))
	    return hep;
    }

    return NULL;
}
    
/* vi:set ts=8 sts=4 sw=4 tw=79: */
