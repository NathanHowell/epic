/* $EPIC: namespace.c,v 1.1.2.7 2003/03/26 12:38:50 wd Exp $ */
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
#include "config.h"
#include "commands.h"
#include "ircaux.h"
#include "namespace.h"

struct namespace_list namespaces;
static int namespace_valid_name(char *);
static namespace_t *namespace_create(char *);
static namespace_t *namespace_find_in(char *, namespace_t *);
static void namespace_destroy(namespace_t *);

/* This function fills in the 'namespaces' global.  It sets up the root
 * namespace and... well.. that's it. */
void namespace_init(void) {

    namespaces.root = namespace_create("");
    namespaces.current = namespaces.root;
}

/*
 * This function performs 'cleanup' for namespaces deferred for destruction.
 * In other words, at the appropriate time, we destroy the namespaces
 * scheduled for destruction.  This should probably only occur in one place
 * (parse_line).
 */
void namespace_cleanup(void) {
    
    while (!LIST_EMPTY(&namespaces.deferred))
	namespace_destroy(LIST_FIRST(&namespaces.deferred));
}

/*
 * This function checks 'name' to see if it contains valid characters for
 * namespace names.  It is assumed that 'name' is a pre-separated component
 * (i.e. there are no :: separators).  Valid characters are:
 * a-zA-Z_ for the first character and a-zA-Z0-9_ for subsequent characters.
 */
static int namespace_valid_name(char *name) {
    char *s = name + 1;

    if (('a' > tolower(*name) || 'z' < tolower(*name)) && *name != '_')
	return 0; /* bogus first char */

    while (*s != '\0') {
	if ((('a' > tolower(*s) || 'z' < tolower(*s)) &&
		    ('0' > *s || '9' < *s)) && *s != '_')
	    return 0;
	s++;
    }

    return 1; /* s'alright. */
}

/* This function creates a new namespace.  It links it, by default, to the
 * current namespace.  If the current namespace is NULL (this should only
 * ever happen once) then it is not linked anywhere.  It also sets up the
 * necessary tables, and so on. */
static namespace_t *namespace_create(char *name) {
    namespace_t *nsp = new_malloc(sizeof(namespace_t));

    memset(nsp, 0, sizeof(namespace_t));
    strncpy(nsp->name, name, NAMESPACE_MAX_LEN);
    if ((nsp->parent = namespaces.current) != NULL)
	LIST_INSERT_HEAD(&nsp->parent->children, nsp, lp);
    nsp->ftable = create_hash_table(16, sizeof(Alias),
	    NAMESPACE_HASH_SANITYLEN, HASH_FL_NOCASE | HASH_FL_STRING,
	    (hash_cmpfunc)strcasecmp);
    nsp->vtable = create_hash_table(16, sizeof(Alias),
	    NAMESPACE_HASH_SANITYLEN, HASH_FL_NOCASE | HASH_FL_STRING,
	    (hash_cmpfunc)strcasecmp);

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

    if (!strncmp(name, "::", 2)) {
	nsp = namespaces.root;
	name += 2;
    }
    if (*name == '\0')
	return namespaces.root;

    while (name != '\0') {
	if ((s = strchr(name, ':')) != NULL) {
	    if (strncmp(s, "::", 2)) {
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

	*s = ':'; /* fix this and continue on */
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
static void namespace_destroy(namespace_t *space) {
    namespace_t *nsp;

    while ((nsp = LIST_FIRST(&space->children)) != NULL)
	namespace_destroy(nsp);

    /* XXX: Alias cleanup! */
    destroy_hash_table(space->ftable);
    destroy_hash_table(space->vtable);

    /* Make sure the current namespace remains valid! */
    if (namespaces.current == space && space != namespaces.root)
	namespaces.current = space->parent;

    /* Make sure not to free the root namespace. :) */
    if (space != namespaces.root) {
	LIST_REMOVE(space, lp);
	new_free(&space);
    }
}

/* This gets the full name of the namespace.  If explicit is non-zero, the
 * '::' is added at the root.  The memory returned must be freed by the
 * caller! */
char *namespace_get_full_name(namespace_t *space, int explicit) {
    namespace_t *nsp = space;
    char *buf = m_strdup(space->name);
    char *tmp;

    while ((nsp = nsp->parent) != NULL) {
	/* Don't prepend the root name if they don't want it. */
	if (nsp == namespaces.root && !explicit)
	    return buf;
	/* now *prepend* this one's name onto the current space.. */
	tmp = buf;
	buf = m_3dup(nsp->name, "::", tmp);
	new_free(&tmp);
    }
    if (*buf == '\0' && explicit) {
	new_free(&buf);
	buf = m_strdup("::");
    }

    return buf;
}

/*
 * The namespace command.  This controls the current notion of the namespace
 * we are in.  No restrictions are placed on where it can be used (meaning
 * it can be used from the input line so watch out ;).  Syntax is as
 * follows:
 * NAMESPACE [space] [verb]
 * If no arguments are given, a tree listing of the current namespaces is
 * provided.  If one argument is given the current namespace is changed to
 * that space.  If two arguments are given then the specified action is
 * taken on the specified namespace.  Currently there are two verbs
 * available: UNLOAD and INFO.  UNLOAD causes the namespace (and its
 * children!) to be obliterated.  INFO shows information on the namespace
 * and its children.
 *
 * It is important to note that, when setting the current namespace, if you
 * are creating a new namespace you cannot recursively create new spaces in
 * a single command.  i.e. "namespace foo::bar::baz" is not valid if either
 * of foo or foo::bar do not exist.
 */
static void show_namespace_tree(namespace_t *);
BUILT_IN_COMMAND(namespace_command) {
    char *space, *verb;
    namespace_t *nsp;

    if ((space = new_next_arg(args, &args)) == NULL) {
	show_namespace_tree(namespaces.root);
	return;
    }

    if ((verb = new_next_arg(args, &args)) == NULL) {
	if ((nsp = namespace_find(space)) == NULL) {
	    /* namespace doesn't exist.. create it for them.  We check to
	     * see if this is a multi-component name.  If it is, and the
	     * parts prior to the last represent a valid space, we switch
	     * over to that space and create in it. */
	    if ((verb = strrchr(space, ':')) != NULL) {
		if (verb == space || *--verb != ':') {
		    yell("Malformed namespace name %s", space);
		    return;
		} else
		    *verb = '\0';
		if ((nsp = namespace_find(space)) == NULL) {
		    yell("Cannot recursively create namespaces");
		    return;
		}
		namespaces.current = nsp;
		space = verb + 2;
	    }
	    if (!namespace_valid_name(space)) {
		yell("Invalid namespace name %s", space);
		return;
	    }
	    nsp = namespace_create(space);
	    say("Created namespace %s",
		    (space = namespace_get_full_name(nsp, 0)));
	} else
	    say("Switched to namespace %s",
		    (space = namespace_get_full_name(nsp, 0)));

	/* nsp is valid now.. */
	namespaces.current = nsp;
	new_free(&space);

	return;
    }

    /* Okay, we have a space and a verb.  Go through the motions */
    if ((nsp = namespace_find(space)) == NULL) {
	yell("No such namespace %s", space);
	return;
    }

    if (!strncasecmp(verb, "INFO", 1))
	show_namespace_tree(nsp);
    else if (!strncasecmp(verb, "UNLOAD", 1)) {
	say("Unloading namespace %s and its children",
		(space = namespace_get_full_name(nsp, 0)));
	/* Hrm, let's just defer this instead.. */
	LIST_REMOVE(nsp, lp);
	LIST_INSERT_HEAD(&namespaces.deferred, nsp, lp);
	new_free(&space);
    }
}

static void show_namespace_tree(namespace_t *space) {
    namespace_t *nsp;
    char *s;

    s = namespace_get_full_name(space, 1);
    say("%s%s %d functions, %d aliases", s,
	    (space == namespaces.current ? "*" : ""),
	    hashtable_count(space->ftable), hashtable_count(space->vtable));

    LIST_FOREACH(nsp, &space->children, lp)
	show_namespace_tree(nsp);

    new_free(&s);
}
