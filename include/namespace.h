/*
 * namespace.h: header containing namespace handling routines
 *
 * Copyright 2003 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef EPIC_NAMESPACE_H
#define EPIC_NAMESPACE_H

#include "alias.h"
#include "hash.h"

/* The namespace structure.  The core of a namespace is its name, and its
 * children and parent namespaces. */
typedef struct namespace {
#define NAMESPACE_MAX_LEN 32
    char    name[NAMESPACE_MAX_LEN + 1];	/* name of this namespace */
    struct namespace *parent;			/* parent.  NULL only if
						   this is the root (::) */
    LIST_HEAD(, namespace) children;		/* list of our children */

    hashtable_t *ftable;			/* function (alias) table */
    struct alias_list flist;			/* and list */
    hashtable_t *vtable;			/* variable (assign) table */
    struct alias_list vlist;			/* and list */

    LIST_ENTRY(namespace) lp;
} namespace_t;

extern struct namespace_list {
    namespace_t *current;
    namespace_t *root;
} namespaces;

void namespace_init(void);
namespace_t *namespace_create(char *);
namespace_t *namespace_find(char *);
void namespace_destroy(namespace_t *);
char *namespace_get_full_name(namespace_t *, int);
BUILT_IN_COMMAND(namespace_command);

/* This is the 'sanity' length for hashing functions/variables.  While
 * longer names will remain 'unique' in the system, the names will only be
 * hashed up to this point. */
#define NAMESPACE_HASH_SANITYLEN 32

#endif
