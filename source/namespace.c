/* $EPIC: namespace.c,v 1.1.2.1 2003/02/27 12:17:24 wd Exp $ */
/*
 * namespace.c: Namespace tracking system.
 *
 * Copyright 2003 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "irc.h"
#include "alias.h"
#include "config.h"
#include "commands.h"
#include "ircaux.h"
#include "namespace.h"

struct namespace_list namespaces;
static namespace_t *namespace_find_in(char *, namespace_t *);

/* This function fills in the 'namespaces' global.  It sets up the root
 * namespace and... well.. that's it. */
void namespace_init(void) {

    namespaces.root = namespace_create("");
    namespaces.current = namespaces.root;
}

/* This function creates a new namespace.  It links it, by default, to the
 * current namespace.  If the current namespace is NULL (this should only
 * ever happen once) then it is not linked anywhere.  It also sets up the
 * necessary tables, and so on. */
namespace_t *namespace_create(char *name) {
    namespace_t *nsp = new_malloc(sizeof(namespace_t));

    memset(nsp, 0, sizeof(namespace_t));
    strncpy(nsp->name, name, NAMESPACE_MAX_LEN);
    if ((nsp->parent = namespaces.current) != NULL)
	LIST_INSERT_HEAD(&nsp->parent->children, nsp, lp);
    nsp->ftable = create_hash_table(16, sizeof(Alias),
	    NAMESPACE_HASH_SANITYLEN, HASH_FL_NOCASE | HASH_FL_STRING,
	    strcasecmp);
    nsp->vtable = create_hash_table(16, sizeof(Alias),
	    NAMESPACE_HASH_SANITYLEN, HASH_FL_NOCASE | HASH_FL_STRING,
	    strcasecmp);

    return nsp;
}

/* This function does a search for the given namespace.  If the space begins
 * with :: it will start from the root, otherwise it will search from within
 * the current namespace.  The buffer given will not be mangled on function
 * exit, however it is mangled within the function, so const-ness is not
 * viable. */
namespace_t *namespace_find(char *name) {
    namespace_t *nsp = namespaces.current;
    char *s;

    if (strncmp(name, "::", 2)) {
	nsp = namespaces.root;
	name += 2;
    }

    while (name != '\0') {
	if ((s = strchr(name, ':')) != NULL) {
	    if (!strncmp(s, "::", 2)) {
		yell("malformed namespace component: %s", name);
		return NULL; /* this is bogus! */
	    }
	    *s = '\0'; /* terminate it for now */
	} else
	    /* otherwise, this is the last component, so whatever
	     * namespace_find_in says is final. */
	    return namespace_find_in(name, nsp);

	/* Okay, this is the normal case, see if nsp contains what we want.
	 * If it does, clean up the string and continue. */
	if ((nsp = namespace_find_in(name, nsp)) == NULL) {
	    *s = ':'; /* fix this.. */
	    return NULL;
	}

	*s = ':'; /* fix this, and continue with name being set after the :: */
	name = s + 2;
    }

    yell("namespace_find() reached end of function!");
    return NULL;
}

/* Find 'name' within the given space.  Name is expected to be a single
 * distinguished word. */
static namespace_t *namespace_find_in(char *name, namespace_t *space) {
    namespace_t *nsp;

    LIST_FOREACH(nsp, &space->children, lp) {
	if (!strncasecmp(name, nsp->name, NAMESPACE_MAX_LEN))
	    /* found a match. */
	    return nsp;
    }

    return NULL;
}

/* This function destroys a namespace, and all of its branches. */
void namespace_destroy(namespace_t *space) {
    namespace_t *nsp;

    while ((nsp = LIST_FIRST(&space->children)) != NULL)
	namespace_destroy(nsp);

    /* XXX: Alias cleanup! */
    destroy_hash_table(nsp->ftable);
    destroy_hash_table(nsp->vtable);

    new_free(&nsp);
}

