/* $EPIC: alias.c,v 1.11.2.11 2003/03/26 12:38:50 wd Exp $ */
/*
 * alias.c -- Handles the whole kit and caboodle for aliases.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1997, 2002 EPIC Software Labs
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
/* Almost totaly rewritten by Jeremy Nelson (01/97) */

#include "irc.h"
#include "commands.h"
#include "files.h"
#include "hash.h"
#include "history.h"
#include "hook.h"
#include "input.h"
#include "ircaux.h"
#include "namespace.h"
#include "output.h"
#include "screen.h"
#include "stack.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "keys.h"
#include "functions.h"

#define LEFT_BRACE '{'
#define RIGHT_BRACE '}'
#define LEFT_BRACKET '['
#define RIGHT_BRACKET ']'
#define LEFT_PAREN '('
#define RIGHT_PAREN ')'
#define DOUBLE_QUOTE '"'

/*
 * after_expando: This replaces some much more complicated logic strewn
 * here and there that attempted to figure out just how long an expando 
 * name was supposed to be.  Well, now this changes that.  This will slurp
 * up everything in 'start' that could possibly be put after a $ that could
 * result in a syntactically valid expando.  All you need to do is tell it
 * if the expando is an rvalue or an lvalue (it *does* make a difference)
 */
static 	char *lval[] = { "rvalue", "lvalue" };
static  char *after_expando (char *start, int lvalue, int *call)
{
	char	*rest;
	char	*str;

	if (!*start)
		return start;

	/*
	 * One or two leading colons are allowed
	 */
	str = start;
	if (*str == ':')
		if (*++str == ':')
			++str;

	/*
	 * This handles 99.99% of the cases
	 */
	while (*str && (isalpha(*str) || isdigit(*str) || 
				*str == '_' || *str == '.'))
		str++;

	/*
	 * This handles any places where a var[var] could sneak in on
	 * us.  Supposedly this should never happen, but who can tell?
	 */
	while (*str == '[')
	{
		if (!(rest = MatchingBracket(str + 1, '[', ']')))
		{
			if (!(rest = strchr(str, ']')))
			{
				yell("Unmatched bracket in %s (%s)", 
						lval[lvalue], start);
				return empty_string;
			}
		}
		str = rest + 1;
	}

	/*
	 * Rvalues may include a function call, slurp up the argument list.
	 */
	if (!lvalue && *str == '(')
	{
		if (!(rest = MatchingBracket(str + 1, '(', ')')))
		{
			if (!(rest = strchr(str, ')')))
			{
				yell("Unmatched paren in %s (%s)", 
						lval[lvalue], start);
				return empty_string;
			}
		}
		*call = 1;
		str = rest + 1;
	}

	/*
	 * If the entire thing looks to be invalid, perhaps its a 
	 * special built-in expando.  Check to see if it is, and if it
	 * is, then slurp up the first character as valid.
	 * Also note that $: by itself must be valid, which requires
	 * some shenanigans to handle correctly.  Ick.
	 */
	if (str == start || (str == start + 1 && *start == ':'))
	{
		int is_builtin = 0;

		built_in_alias(*start, &is_builtin);
		if (is_builtin && (str == start))
			str++;
	}


	/*
	 * All done!
	 */
	return str;
}

/* Used for statistics gathering */
static unsigned long 	alias_total_allocated = 0;
static unsigned long 	alias_total_bytes_allocated = 0;

enum ARG_TYPES {
	WORD,
	UWORD,
	DWORD
};

/* Ugh. XXX Bag on the side */
struct ArgListT {
	char *	vars[32];
	char *	defaults[32];
	int	words[32];
	enum	ARG_TYPES types[32];
	int	void_flag;
	int	dot_flag;
};
typedef struct ArgListT ArgList;
ArgList	*parse_arglist (char *arglist);
void	destroy_arglist (ArgList *);


/*
 * This is where we keep track of where the last pending function call.
 * This is used when you assign to the FUNCTION_RETURN value.  Since it
 * is neccesary to be able to access FUNCTION_RETURN in contexts where other
 * local variables would not be visible, we do this as a quasi-neccesary
 * hack.  When you reference FUNCTION_RETURN, it goes right to this stack.
 */
	int	last_function_call_level = -1;

/*
 * The following actions are supported:  add, delete, find, list
 * On the following types of data:	 variable, cmd_alias, local_var
 * Except you cannot list or delete local_variables.
 *
 * To fetch a variable, use ``get_variable''
 * To fetch an alias, use ``get_cmd_alias''
 * To fetch an ambiguous alias, use ``glob_cmd_alias''
 * To recurse an array structure, use ``get_subarray_elements''
 */

/*
 * These are general purpose interface functions.
 * However, the static ones should **always** be static!  If you are tempted
 * to use them outside of alias.c, please rethink what youre trying to do.
 * Using these functions violates the encapsulation of the interface.
 * Specifically, if you create a global variable and then want to delete it,
 * try using a local variable so it is reaped automatically.
 */
static  void    add_cmd_alias      (char *name, ArgList *arglist, char *stuff,
	int stub);
static	void	delete_variable   (char *name, int noisy);
static	void	delete_cmd_alias   (char *name, int noisy);
/*	void	delete_local_var (char *name); 		*/
static	void	unload_cmd_alias   (char *fn);
static	void	unload_variable   (char *fn);
static	void	list_cmd_alias     (char *name);
static	void	list_variable     (char *name);
static	void	list_local_var   (char *name);
static	void 	destroy_aliases    (hashtable_t *table,
	struct alias_list *list);

extern	char *  get_variable            (char *name);
extern	char ** glob_cmd_alias          (char *name, int *howmany);
extern	char ** glob_assign_alias	(char *name, int *howmany);
extern	char ** pmatch_cmd_alias        (char *name, int *howmany);
extern	char ** pmatch_assign_alias	(char *name, int *howmany);
extern	char ** get_subarray_elements   (char *root, int *howmany, int type);


static	char *	get_variable_with_args (const char *str, const char *args, int *args_flag);

static	void	prepare_alias_call (ArgList *, char **);

/* I'm afraid this needed to be moved here, because some commands need to
 * know about the call stack and wind_index at this point. :/ */

/*
 * This is the ``stack frame''.  Each frame has a ``name'' which is
 * the name of the alias or on of the frame, or is NULL if the frame
 * is not an ``enclosing'' frame.  Each frame also has a ``current command''
 * that is being executed, which is used to help us when the client crashes.
 * Each stack also contains a list of local variables.
 */
typedef struct RuntimeStackStru
{
	const char *name;	/* Name of the stack */
	char 	*current;	/* Current cmd being executed */
	hashtable_t *vtable;	/* variable table */
	struct alias_list *vlist;/* and list */
	int	locked;		/* Are we locked in a wait? */
	int	parent;		/* Our parent stack frame */
}	RuntimeStack;

/*
 * This is the master stack frame.  Its size is saved in ``max_wind''
 * and the current frame being used is stored in ``wind_index''.
 */
static 	RuntimeStack *call_stack = NULL;
	int 	max_wind = -1;
	int 	wind_index = -1;

/************************** HIGH LEVEL INTERFACE ***********************/

/*
 * User front end to the ALIAS command
 * Syntax to debug alias:  ALIAS /S
 * Syntax to add alias:    ALIAS name [{] irc command(s) [}]
 * Syntax to delete alias: ALIAS -name
 */
BUILT_IN_COMMAND(aliascmd)
{
	char *name;
	char *real_name;
	char *ptr;

	/*
	 * If no name present, list all aliases
	 */
	if (!(name = next_arg(args, &args)))
	{
		list_cmd_alias(NULL);
		return;
	}

	/*
	 * Alias can take an /s arg, which shows some data we've collected
	 */
	if (!my_strnicmp(name, "/S", 2))
	{
	    say("Total aliases handled: %lu", 
			alias_total_allocated);
	    say("Total bytes allocated to aliases: %lu", 
			alias_total_bytes_allocated);
	    return;
	}

	/*
	 * Canonicalize the alias name
	 */
	real_name = remove_brackets(name, NULL, NULL);

	/*
	 * Find the argument body
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * If there is no argument body, we probably want to delete alias
	 */
	if (!args || !*args)
	{
		/*
		 * If the alias name starts with a hyphen, then we are
		 * going to delete it.
		 */
		if (real_name[0] == '-')
		{
			if (real_name[1])
				delete_cmd_alias(real_name + 1, 1);
			else
				say("You must specify an alias to be removed.");
		}

		/*
		 * Otherwise, the user wants us to list that alias
		 */
		else
			list_cmd_alias(real_name);
	}

	/*
	 * If there is an argument body, then we have to register it
	 */
	else
	{
		ArgList *arglist = NULL;

		/*
		 * Aliases may contain a parameter list, which is parsed
		 * at registration time.
		 */
		if (*args == LEFT_PAREN)
		{
			ptr = MatchingBracket(++args, LEFT_PAREN, RIGHT_PAREN);
			if (!ptr)
				say("Unmatched lparen in %s %s", 
					command, real_name);
			else
			{
				*ptr++ = 0;
				while (*ptr && my_isspace(*ptr))
					ptr++;
				if (!*ptr)
					say("Missing alias body in %s %s", 
						command, real_name);

				while (*args && my_isspace(*args))
					args++;

				arglist = parse_arglist(args);
				args = ptr;
			}
		}

		/*
		 * Aliases' bodies can be surrounded by a set of braces,
		 * which are stripped off.
		 */
		if (*args == LEFT_BRACE)
		{
			ptr = MatchingBracket(++args, LEFT_BRACE, RIGHT_BRACE);

			if (!ptr)
				say("Unmatched brace in %s %s", 
						command, real_name);
			else 
			{
				*ptr++ = 0;
				while (*ptr && my_isspace(*ptr))
					ptr++;

				if (*ptr)
					say("Junk [%s] after closing brace in %s %s", ptr, command, real_name);

				while (*args && my_isspace(*args))
					args++;

			}
		}

		/*
		 * Register the alias
		 */
		add_cmd_alias(real_name, arglist, args, 0);
	}

	new_free(&real_name);
	return;
}

/*
 * User front end to the ASSIGN command
 * Syntax to add variable:    ASSIGN name text
 * Syntax to delete variable: ASSIGN -name
 */
BUILT_IN_COMMAND(assigncmd)
{
	char *real_name;
	char *name;

	/*
	 * If there are no arguments, list all the global variables
	 */
	if (!(name = next_arg(args, &args)))
	{
		list_variable(NULL);
		return;
	}

	/*
	 * Canonicalize the variable name
	 */
	real_name = remove_brackets(name, NULL, NULL);

	/*
	 * Find the stuff to assign to the variable
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * If there is no body, then the user probably wants to delete
	 * the variable
	 */
	if (!args || !*args)
	{
		/*
	 	 * If the variable name starts with a hyphen, then we remove
		 * the variable
		 */
		if (real_name[0] == '-')
		{
			if (real_name[1])
				delete_variable(real_name + 1, 1);
			else
				say("You must specify an alias to be removed.");
		}

		/*
		 * Otherwise, the user wants us to list the variable
		 */
		else
			list_variable(real_name);
	}

	/*
	 * Register the variable
	 */
	else
		add_variable(real_name, args, 1, 0);

	new_free(&real_name);
	return;
}

/*
 * User front end to the STUB command
 * Syntax to stub an alias to a file:	STUB ALIAS name[,name] filename(s)
 * Syntax to stub a variable to a file:	STUB ASSIGN name[,name] filename(s)
 */
BUILT_IN_COMMAND(stubcmd)
{
	int 	type;
	char 	*cmd;
	char 	*name;
const 	char 	*usage = "Usage: %s (alias|assign) <name> <file> [<file> ...]";

	/*
	 * The first argument is the type of stub to make
	 * (alias or assign)
	 */
	if (!(cmd = upper(next_arg(args, &args))))
	{
		error("Missing stub type");
		say(usage, command);
		return;
	}

	if (!strncmp(cmd, "ALIAS", strlen(cmd)))
		type = COMMAND_ALIAS;
	else if (!strncmp(cmd, "ASSIGN", strlen(cmd)))
		type = VAR_ALIAS;
	else
	{
		error("[%s] is an Unrecognized stub type", cmd);
		say(usage, command);
		return;
	}

	/*
	 * The next argument is the name of the item to be stubbed.
	 * This is not optional.
	 */
	if (!(name = next_arg(args, &args)))
	{
		error("Missing alias name");
		say(usage, command);
		return;
	}

	/*
	 * Find the filename argument
	 */
	while (my_isspace(*args))
		args++;

	/*
	 * The rest of the argument(s) are the files to load when the
	 * item is referenced.  This is not optional.
	 */
	if (!args || !*args)
	{
		error("Missing file name");
		say(usage, command);
		return;
	}

	/*
	 * Now we iterate over the item names we were given.  For each
	 * item name, seperated from the next by a comma, stub that item
	 * to the given filename(s) specified as the arguments.
	 */
	while (name && *name)
	{
		char 	*next_name;
		char	*real_name;

		if ((next_name = strchr(name, ',')))
			*next_name++ = 0;

		real_name = remove_brackets(name, NULL, NULL);
		if (type == COMMAND_ALIAS)
			add_cmd_alias(real_name, NULL, args, 1);
		else
			add_variable(real_name, args, 0, 1);

		new_free(&real_name);
		name = next_name;
	}
}

BUILT_IN_COMMAND(localcmd)
{
	char *name;

	if (!(name = next_arg(args, &args)))
	{
		list_local_var(NULL);
		return;
	}

	while (args && *args && my_isspace(*args))
		args++;

	if (!args)
		args = empty_string;

	if (!my_strnicmp(name, "-dump", 2))	/* Illegal name anyways */
	{
		destroy_aliases(call_stack[wind_index].vtable,
			call_stack[wind_index].vlist);
		return;
	}

	while (name && *name)
	{
		char 	*next_name;
		char	*real_name;

		if ((next_name = strchr(name, ',')))
			*next_name++ = 0;

		real_name = remove_brackets(name, NULL, NULL);
		add_local_var(real_name, args, 1);
		new_free(&real_name);
		name = next_name;
	}
}

BUILT_IN_COMMAND(dumpcmd)
{
	char 	*blah = empty_string;
	int 	all = 0;
	int 	dumped = 0;

	upper(args);

	if (!args || !*args || !strncmp(args, "ALL", 3))
		all = 1;

	while (all || (blah = next_arg(args, &args)))
	{
		dumped = 0;

		if (!strncmp(blah, "VAR", strlen(blah)) || all)
		{
			say("Dumping your global variables");
			destroy_aliases(namespaces.root->vtable,
				&namespaces.root->vlist);
			dumped++;
		}
		if (!strncmp(blah, "ALIAS", strlen(blah)) || all)
		{
			say("Dumping your global aliases");
			destroy_aliases(namespaces.root->ftable,
				&namespaces.root->flist);
			dumped++;
		}
		if (!strncmp(blah, "ON", strlen(blah)) || all)
		{
			say("Dumping your ONs");
			flush_on_hooks();
			dumped++;
		}

		if (!dumped)
			say("Dump what? ('%s' is unrecognized)", blah);

		if (all)
			break;
	}
}

BUILT_IN_COMMAND(unloadcmd)
{
	char *filename;

	if (!(filename = next_arg(args, &args)))
		error("You must supply a filename for /UNLOAD.");
	else
	{
		do_hook(UNLOAD_LIST, "%s", filename);
		say("Removing aliases from %s ...", filename);
		unload_cmd_alias(filename);
		say("Removing assigns from %s ...", filename);
		unload_variable(filename);
		say("Removing hooks from %s ...", filename);
		unload_on_hooks(filename);
		say("Removing keybinds from %s ...", filename);
		unload_bindings(filename);
		say("Done.");
	}
}


/*
 * Argument lists look like this:
 *
 * LIST   := LPAREN TERM [COMMA TERM] RPAREN)
 * LPAREN := '('
 * RPAREN := ')'
 * COMMA  := ','
 * TERM   := LVAL QUAL | "..." | "void"
 * LVAL   := <An alias name>
 * QUAL   := NUM "words"
 *
 * In English:
 *   An argument list is a comma seperated list of variable descriptors (TERM)
 *   enclosed in a parenthesis set.  Each variable descriptor may contain any
 *   valid alias name followed by an action qualifier, or may be the "..."
 *   literal string, or may be the "void" literal string.  If a variable
 *   descriptor is an alias name, then that positional argument will be removed
 *   from the argument list and assigned to that variable.  If the qualifier
 *   specifies how many words are to be taken, then that many are taken.
 *   If the variable descriptor is the literal string "...", then all argument
 *   list parsing stops there and all remaining alias arguments are placed 
 *   into the $* special variable.  If the variable descriptor is the literal
 *   string "void", then the balance of the remaining alias arguments are 
 *   discarded and $* will expand to the false value.  If neither "..." nor
 *   "void" are provided in the argument list, then that last variable in the
 *   list will recieve all of the remaining arguments left at its position.
 * 
 * Examples:
 *
 *   # This example puts $0 in $channel, $1 in $mag, and $2- in $nicklist.
 *   /alias opalot (channel, mag, nicklist) {
 *	fe ($nicklist) xx yy zz {
 *	    mode $channel ${mag}ooo $xx $yy $zz
 *	}
 *   }
 *
 *   # This example puts $0 in $channel, $1 in $mag, and the new $* is old $2-
 *   /alias opalot (channel, mag, ...) {
 *	fe ($*) xx yy zz {
 *	    mode $channel ${mag}ooo $xx $yy $zz
 *	}
 *   }
 *
 *   # This example throws away any arguments passed to this alias
 *   /alias booya (void) { echo Booya! }
 */
ArgList	*parse_arglist (char *arglist)
{
	char *	this_term;
	char *	next_term;
	char *	varname;
	char *	modifier, *value;
	int	arg_count = 0;
	ArgList *args = new_malloc(sizeof(ArgList));

	args->void_flag = args->dot_flag = 0;
	for (this_term = arglist; *this_term; this_term = next_term,arg_count++)
	{
		while (isspace(*this_term))
			this_term++;
		next_in_comma_list(this_term, &next_term);
		if (!(varname = next_arg(this_term, &this_term)))
			continue;
		args->types[arg_count] = WORD;
		if (!my_stricmp(varname, "void")) {
			args->void_flag = 1;
			break;
		} else if (!my_stricmp(varname, "...")) {
			args->dot_flag = 1;
			break;
		} else {
			args->vars[arg_count] = m_strdup(varname);
			args->defaults[arg_count] = NULL;
			args->words[arg_count] = 1;

			while ((modifier = next_arg(this_term, &this_term)))
			{
				if (!(value = new_next_arg(this_term, &this_term)))
						break;
				if (!my_stricmp(modifier, "default"))
				{
					args->defaults[arg_count] = m_strdup(value);
				}
				else if (!my_stricmp(modifier, "words"))
				{
					args->types[arg_count] = WORD;
					args->words[arg_count] = atol(value);
				}
				else if (!my_stricmp(modifier, "uwords"))
				{
					args->types[arg_count] = UWORD;
					args->words[arg_count] = atol(value);
				}
				else if (!my_stricmp(modifier, "dwords"))
				{
					args->types[arg_count] = DWORD;
					args->words[arg_count] = atol(value);
				}
				else
				{
					yell("Bad modifier %s", modifier);
				}
			}
		}
	}
	args->vars[arg_count] = NULL;
	return args;
}

void	destroy_arglist (ArgList *arglist)
{
	int	i = 0;

	if (!arglist)
		return;

	for (i = 0; ; i++)
	{
		if (!arglist->vars[i])
			break;
		new_free(&arglist->vars[i]);
		new_free(&arglist->defaults[i]);
	}
	new_free((char **)&arglist);
}

#define ew_next_arg(a,b,c,d) ((d) ? new_next_arg_count((a),(b),(c)) : next_arg_count((a),(b),(c)))

/*
 * This function is used to prepare the $* string before calling a user
 * alias or function.  You should pass in the arglist from the Alias
 * to this function, and also the $* value.  The second value may very
 * well be modified.
 */
static void prepare_alias_call (ArgList *args, char **stuff) {
	int	i;

	if (!args)
		return;

	for (i = 0; args->vars[i]; i++)
	{
		char	*next_val;
		char	*expanded = NULL;
		int	af, type = 0;

		switch (args->types[i])
		{
			case WORD:
				type = (x_debug & DEBUG_EXTRACTW);
				break;
			case UWORD:
				type = 0;
				break;
			case DWORD:
				type = 1;
				break;
		}

		/* Last argument on the list and no ... argument? */
		if (!args->vars[i + 1] && !args->dot_flag && !args->void_flag)
		{
			next_val = *stuff;
			*stuff = empty_string;
		}

		/* Yank the next word from the arglist */
		else
		{
			next_val = ew_next_arg(*stuff, stuff, args->words[i], type);
		}

		if (!next_val || !*next_val)
		{
			if ((next_val = args->defaults[i]))
				next_val = expanded = expand_alias(next_val, *stuff, &af, NULL);
			else
				next_val = empty_string;
		}

		/* Add the local variable */
		add_local_var(args->vars[i], next_val, 0);
		if (expanded)
			new_free(&expanded);
	}

	/* Throw away rest of args if wanted */
	if (args->void_flag)
		*stuff = empty_string;
}

#undef ew_next_arg

/**************************** INTERMEDIATE INTERFACE *********************/
static	namespace_t *extract_namespace(char **name);
static	Alias *	find_variable     (char *name, int local);
static	Alias *	find_local_var   (char *name, RuntimeStack **frame);

/* this function extracts the namespace portion from a string.  it returns
 * the namespace (if it exists!) and stores the remaining portion in name.
 * If the description is for a local space (:var) it will return a special
 * 'magic' value as the address.  This should be used by all functions below
 * to figure out what a name is supposed to be. */
#define LOCAL_NAMESPACE (namespace_t *)0xf0010ca1 /* heh! */
static namespace_t *extract_namespace(char **name) {
    char *s;
    char *start = *name;
    namespace_t *nsp;

    if ((s = strrchr(*name, ':')) == NULL)
	return namespaces.current; /* a global object in the current space */

    /* Otherwise, we need to set 'name' correctly, then decide if we have a
     * namespace or just a 'local' specifier. */
    *name = s + 1;
    if (s == start)
	/* this means the string is of the form ':name'.  It's local */
	return LOCAL_NAMESPACE;

    /* Now we know the string is at least of the form '...:word', make sure
     * the form is really proper, then pass the find work to
     * namespace_find(). */
    if (*--s != ':')
	/* This is a bogus name.  XXX: print a warning? */
	return NULL;
    else if (s == start)
	return namespaces.root; /* it's actually the root namespace. */

    *s = '\0';
    nsp = namespace_find(start);
    *s = ':';
    return nsp;
}

Alias *make_new_Alias (char *name)
{
	size_t len = strlen(name);

	/* We do some evil voodoo here to locate the name at the end of the
	 * alias structure for hashing it.  Ulch. :) */
	Alias *tmp = (Alias *) new_malloc(sizeof(Alias) + len + 1);
	memset(tmp, 0, sizeof(Alias));
	tmp->name = (char *)tmp + sizeof(Alias);
	strcpy(tmp->name, name);
	tmp->nspace = namespaces.current;
	tmp->stuff = tmp->stub = NULL;
	tmp->line = current_line();
	tmp->filename = m_strdup(current_package());
	alias_total_bytes_allocated += sizeof(Alias) + strlen(name) +
				strlen(tmp->filename);
	return tmp;
}


/*
 * add_variable: Add a global variable
 *
 * name -- name of the alias to create (must be canonicalized)
 * stuff -- what to have ``name'' expand to.
 *
 * If ``name'' is FUNCTION_RETURN, then it will create the local
 * return value (die die die)
 *
 * If ``name'' refers to an already created local variable, that
 * local variable is used (invisibly)
 */
void	add_variable	(char *name, char *stuff, int noisy, int stub)
{
	char 	*ptr;
	Alias 	*tmp = NULL;
	int 	af;
	int	local = 0;
	char	*save;
	namespace_t *nsp;

	save = name = remove_brackets(name, NULL, &af);
	if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE)
		local = 1;
	else if (nsp == NULL) {
		yell("Unknown/invalid namespace in %s", save);
		new_free(&save);
		return;
	}

	/* Weed out invalid variable names */
	ptr = after_expando(name, 1, NULL);
	if (*ptr)
		error("ASSIGN names may not contain '%c' (You asked for [%s])",
			*ptr, name);

	/* Weed out FUNCTION_RETURN (die die die) */
	else if (!strcmp(name, "FUNCTION_RETURN")) {
		if (stub)
			error("You may not stub the FUNCTION_RETURN variable.");
		else
			add_local_var(name, stuff, noisy);
	}

	/* Pass the buck on local variables.  Variables are local if nsp is
	 * 'the local namespace' or if save is the same as the 'modified'
	 * name (which means there was no explicit namespace specified) and
	 * the variable exists locally */
	else if (nsp == LOCAL_NAMESPACE || (save == name &&
		    find_local_var(name, NULL) != NULL))
		add_local_var(name, stuff, noisy);

	else if (stuff && *stuff) {
		/* Try and find the alias.  If it does not exist, we create
		 * it in the namespace given back to us */
		if ((tmp = hash_find(nsp->vtable, name)) == NULL) {
			tmp = make_new_Alias(name);
			hash_insert(nsp->vtable, tmp);
			LIST_INSERT_HEAD(&nsp->vlist, tmp, lp);
		}

		/*
		 * Then we fill in the interesting details
		 */
		if (strcmp(tmp->filename, current_package()))
			malloc_strcpy(&(tmp->filename), current_package());
		if (stub) {
			malloc_strcpy(&(tmp->stub), stuff);
			new_free(&tmp->stuff);
		} else {
			malloc_strcpy(&(tmp->stuff), stuff);
			new_free(&tmp->stub);
		}
		tmp->global = loading_global;

		alias_total_allocated++;
		if (stub)
			alias_total_bytes_allocated += strlen(tmp->stub);
		else
			alias_total_bytes_allocated += strlen(tmp->name) + strlen(tmp->stuff) + strlen(tmp->filename);

		/*
		 * And tell the user.
		 */
		if (noisy && !stub)
			say("Assign %s added [%s]", name, stuff);
		else if (stub)
			say("Assign %s stubbed to file %s", name, stuff);
	} else
		delete_variable(save, noisy);

	new_free(&save);
	return;
}

void	add_local_var	(char *name, char *stuff, int noisy)
{
	char 	*ptr;
	Alias 	*tmp = NULL;
	RuntimeStack *rtsp;
	int 	af;

	name = remove_brackets(name, NULL, &af);

	/* Weed out invalid variable names */
	ptr = after_expando(name, 1, NULL);
	if (*ptr) {
		error("LOCAL names may not contain '%c' (You asked for [%s])", 
						*ptr, name);
		new_free(&name);
		return;
	}
	
	/*
	 * Now we see if this local variable exists anywhere
	 * within our view.  If it is, we dont care where.
	 * If it doesnt, then we add it to the current frame,
	 * where it will be reaped later.
	 */
	if ((tmp = find_local_var(name, &rtsp)) == NULL) {
		tmp = make_new_Alias(name);
		hash_insert(rtsp->vtable, tmp);
		LIST_INSERT_HEAD(rtsp->vlist, tmp, lp);
	}

	/* Fill in the interesting stuff */
	if (strcmp(tmp->filename, current_package()))
		malloc_strcpy(&(tmp->filename), current_package());
	malloc_strcpy(&(tmp->stuff), stuff);
	alias_total_allocated++;
	if (tmp->stuff)		/* Oh blah. */
	{
		alias_total_bytes_allocated += strlen(tmp->stuff);
		if (x_debug & DEBUG_LOCAL_VARS && noisy)
		    yell("Assign %s (local) added [%s]", name, stuff);
		else if (noisy)
		    say("Assign %s (local) added [%s]", name, stuff);
	}

	new_free(&name);
	return;
}

void	add_cmd_alias	(char *name, ArgList *arglist, char *stuff, int stub)
{
	Alias *tmp = NULL;
	int af;
	char *save;
	namespace_t *nsp;

	save = name = remove_brackets(name, NULL, &af);

	if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE) {
		/* XXX: Self-serving hack.  I use aliases which begin with
		 * ':'.  I can make them work without too much trouble. :) */
		name = save;
		nsp = namespaces.root;
	} else if (nsp == NULL) {
		yell("Unknown/invalid namespace in %s", save);
		new_free(&save);
		return;
	}

	if ((tmp = hash_find(nsp->ftable, name)) == NULL) {
		tmp = make_new_Alias(name);
		hash_insert(nsp->ftable, tmp);
		LIST_INSERT_HEAD(&nsp->flist, tmp, lp);
	}

	if (strcmp(tmp->filename, current_package()))
		malloc_strcpy(&(tmp->filename), current_package());
	if (stub) {
		malloc_strcpy(&(tmp->stub), stuff);
		new_free(&tmp->stuff);
	} else {
		malloc_strcpy(&(tmp->stuff), stuff);
		new_free(&tmp->stub);
	}
	tmp->global = loading_global;
	destroy_arglist(tmp->arglist);
	tmp->arglist = arglist;

	alias_total_allocated++;
	if (stub) {
		alias_total_bytes_allocated += strlen(tmp->stub);
		say("Alias %s stubbed to file %s", name, stuff);
	} else {
		alias_total_bytes_allocated += strlen(tmp->stuff);
		say("Alias	%s added [%s]", name, stuff);
	}

	new_free(&save);
	return;
}

/************************ LOW LEVEL INTERFACE *************************/
static	int	unstub_in_progress = 0;


/* 'name' is expected to already be in canonical form (uppercase, dot
 * noation), local is true if local lookups should be performed, and false
 * (0) otherwise. */
static Alias *	find_variable (char *name, int local) {
	Alias *	item = NULL;
	char *save = name;
	namespace_t *nsp;

	if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE) {
		if (!local)
		    return NULL;
		else
		    return find_local_var(name, NULL);
	} else if (nsp == NULL)
		return NULL;

	/* try the local space first if we can.  that is, if local is set
	 * and 'name' was unqualified (name == save) */
	if (local && name == save &&
		(item = find_local_var(name, NULL)) != NULL)
	    return item;

	/* no luck?  try the regular method */
	if ((item = hash_find(nsp->vtable, name)) == NULL)
	    return NULL; /* no luck */

	/* otherwise, we have to see if it is stubbed.  if it is, we need to
	 * load the file it's stubbed in, and then try again. */
	if (item->stub != NULL) {
		char *file;

		file = LOCAL_COPY(item->stub);
		delete_variable(item->name, 0);

		if (!unstub_in_progress) {
			unstub_in_progress = 1;
			load("LOAD", file, empty_string);
			unstub_in_progress = 0;
		}

		/* At this point, we have to see if 'item' was
		 * redefined by the /load.  We call find_variable 
		 * recursively to pick up the new value */
		return find_variable(save, local);
	}

	return item;
}

Alias *find_cmd_alias (char *name) {
    Alias * item = NULL;
    char *save = name;
    namespace_t *nsp;

    if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE) {
	name = save;
	nsp = namespaces.root;
    } else if (nsp == NULL)
	return NULL;

    if ((item = hash_find(nsp->ftable, name)) == NULL)
	return NULL; /* no luck */

    /* otherwise, we have to see if it is stubbed.  if it is, we need to
     * load the file it's stubbed in, and then try again. */
    if (item->stub != NULL) {
	char *file;

	file = LOCAL_COPY(item->stub);
	delete_cmd_alias(item->name, 0);

	if (!unstub_in_progress) {
	    unstub_in_progress = 1;
	    load("LOAD", file, empty_string);
	    unstub_in_progress = 0;
	}

	/* At this point, we have to see if 'item' was
	 * redefined by the /load.  We call find_cmd_alias 
	 * recursively to pick up the new value */
	return find_cmd_alias(save);
    }

    if (item->stuff == NULL)
	panic("item->stuff is NULL and it shouldn't be.");
    return item;
}


/*
 * An example will best describe the semantics:
 *
 * A local variable will be returned if and only if there is already a
 * variable that is exactly ``name'', or if there is a variable that
 * is an exact leading subset of ``name'' and that variable ends in a
 * period (a dot).
 */
static Alias *	find_local_var (char *name, RuntimeStack **frame)
{
	Alias *alias = NULL;
	int c = wind_index;
	char *ptr;
	int function_return = 0;

	/* No name is an error */
	if (!name)
		return NULL;

	ptr = after_expando(name, 1, NULL);
	if (*ptr)
		return NULL;

	if (!my_stricmp(name, "FUNCTION_RETURN"))
		function_return = 1;

	/*
	 * Search our current local variable stack, and wind our way
	 * backwards until we find a NAMED stack -- that is the enclosing
	 * alias or ON call.  If we find a variable in one of those enclosing
	 * stacks, then we use it.  If we dont, we progress.
	 *
	 * This needs to be optimized for the degenerate case, when there
	 * is no local variable available...  It will be true 99.999% of
	 * the time.
	 */
	for (c = wind_index; c >= 0; c = call_stack[c].parent)
	{
		/* XXXXX */
		if (function_return && last_function_call_level != -1)
			c = last_function_call_level;

		if (x_debug & DEBUG_LOCAL_VARS)
			yell("Looking for [%s] in level [%d]", name, c);

		if (!LIST_EMPTY(call_stack[c].vlist)) {
			char *s, save;

			/* We can always hope that the variable exists */
			alias = hash_find(call_stack[c].vtable, name);
			/* XXXX - This is bletcherous */
			/* Okay, the basic concept here, is that we need to
			 * find out if there is any 'local scope'
			 * predeclared.  So what we do is see if the name
			 * we're looking for has a period (.) in it and, if
			 * it does, truncate at that first period
			 * temporarily and do a lookup in this scope.  If we
			 * find something, we know it's a local scope
			 * predeclaration, and implicitly create the
			 * variable.
			 *
			 * This is really braindead. */
			if (alias == NULL && (s = strchr(name, '.')) != NULL) {
			    save = *++s;
			    *s = '\0';
			    if (hash_find(call_stack[c].vtable, name ) !=
				    NULL) {
				/* implicit creation.. */
				*s = save;
				alias = make_new_Alias(name);
				hash_insert(call_stack[c].vtable, alias);
				LIST_INSERT_HEAD(call_stack[c].vlist,
					alias, lp);
			    }
			    *s = save;
			}
		}

		if (alias)
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I found [%s] in level [%d] (%s)", name, c, alias->stuff);
			break;
		}

		if (*call_stack[c].name || call_stack[c].parent == -1)
		{
			if (x_debug & DEBUG_LOCAL_VARS) 
				yell("I didnt find [%s], stopped at level [%d]", name, c);
			break;
		}
	}

	if (alias) {
		if (frame != NULL)
			*frame = &call_stack[c];
		return alias;
	} else if (frame != NULL)
		*frame = &call_stack[wind_index];

	return NULL;
}





static
void	delete_variable (char *name, int noisy)
{
	Alias *item;
	int i;
	namespace_t *nsp;
	char *save = name;

	if ((nsp = extract_namespace(&name))  == LOCAL_NAMESPACE) {
		yell("Trying to delete local variables in the wrong place!");
		return;
	} else if (nsp == NULL) {
		yell("Unknown/invalid namespace in %s", save);
		return;
	}

	if ((item = hash_find(nsp->vtable, name)) != NULL) {
		hash_delete(nsp->vtable, item);
		LIST_REMOVE(item, lp);
		new_free(&(item->stuff));
		new_free(&(item->stub));
		new_free(&(item->filename));
		new_free((char **)&item);
		if (noisy)
			say("Assign %s removed", save);
	} else if (noisy)
		say("No such assign: %s", save);
}

static
void	delete_cmd_alias (char *name, int noisy)
{
	Alias *item;
	int i;
	namespace_t *nsp;
	char *save = name;

	if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE) {
		name = save;
		nsp = namespaces.root;
	} else if (nsp == NULL) {
		yell("Unknown/invalid namespace in %s", save);
		return;
	}

	if ((item = hash_find(nsp->ftable, name)) != NULL) {
		hash_delete(nsp->ftable, item);
		LIST_REMOVE(item, lp);
		new_free(&(item->stuff));
		new_free(&(item->stub));
		new_free(&(item->filename));
		destroy_arglist(item->arglist);
		new_free((char **)&item);
		if (noisy)
			say("Alias	%s removed", name);
	}
	else if (noisy)
		say("No such alias: %s", name);
}



/* XXX: I have severely munged the stuff below  It still works, in spirit,
 * but it definitely has some issues.  Things are not as neat as they used
 * to be. :( */
static void	list_local_var (char *name) {
	int len, cnt;
	Alias *ap;

	say("Visible Local Assigns:");
	if (name)
		len = strlen(upper(name));

	for (cnt = wind_index; cnt >= 0; cnt = call_stack[cnt].parent)
	{
		if (LIST_EMPTY(call_stack[cnt].vlist))
		    continue;
		LIST_FOREACH(ap, call_stack[cnt].vlist, lp) {
		    if (!name || !strncmp(ap->name, name, len))
			put_it("\t%s\t%s", ap->name, ap->stuff);
		}
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static
void	list_variable (char *name) {
	Alias *ap;
	int len = 0;
	namespace_t *nsp;
	char *save = name;

	if (name != NULL) {
	    if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE ||
		    nsp == NULL) {
		say("Unknown/invalid namespace in %s", save);
		return;
	    }
	} else {
	    nsp = namespaces.root;
	    name = empty_string;
	}
	say("Assigns:");

	if (*name) {
		upper(name);
		len = strlen(name);
	}

	LIST_FOREACH(ap, &nsp->vlist, lp) {
	    if (!*name || !strncmp(ap->name, name, len)) {
		if (ap->stub)
		    say("\t%s STUBBED TO %s", ap->name, ap->stub);
		else
		    say("\t%s\t%s", ap->name, ap->stuff);
	    }
	}
}

/*
 * This function is strictly O(N).  Its possible to address this.
 */
static
void	list_cmd_alias (char *name)
{
	Alias *ap;
	int len = 0;
	namespace_t *nsp;
	char *save = name;

	if (name != NULL) {
	    if ((nsp = extract_namespace(&name)) == LOCAL_NAMESPACE) {
		name = save;
		nsp = namespaces.root;
	    } else if (nsp == NULL) {
		say("Unknown/invalid namespace in %s", save);
		return;
	    }
	} else {
	    nsp = namespaces.root;
	    name = empty_string;
	}
	say("Aliases:");

	if (*name) {
		upper(name);
		len = strlen(name);
	}

	LIST_FOREACH(ap, &nsp->flist, lp) {
	    if (!*name || !strncmp(ap->name, name, len)) {
		if (ap->stub)
		    say("\t%s STUBBED TO %s", ap->name, ap->stub);
		else
		    say("\t%s\t%s", ap->name, ap->stuff);
	    }
	}
}

/************************* UNLOADING SCRIPTS ************************/
static void	unload_cmd_alias (char *filename)
{
	/* XXX: broken */
#if 0
	int 	cnt;

	for (cnt = 0; cnt < cmd_alias.max; )
	{
		if (!strcmp(cmd_alias.list[cnt]->filename, filename))
			delete_cmd_alias(cmd_alias.list[cnt]->name, 0);
		else
			cnt++;
	}
#endif
}

static void	unload_variable (char *filename)
{
	/* XXX: broken */
#if 0
	int 	cnt;

	for (cnt = 0; cnt < variable.max;)
	{
		if (!strcmp(variable.list[cnt]->filename, filename))
			delete_variable(variable.list[cnt]->name, 0);
		else
			cnt++;
	}
#endif
}


/************************* DIRECT VARIABLE EXPANSION ************************/
/*
 * get_variable: This simply looks up the given str.  It first checks to see
 * if its a user variable and returns it if so.  If not, it checks to see if
 * it's an IRC variable and returns it if so.  If not, it checks to see if
 * its and environment variable and returns it if so.  If not, it returns
 * null.  It mallocs the returned string 
 */
char 	*get_variable 	(char *str)
{
	int af;
	return get_variable_with_args(str, NULL, &af);
}


static 
char	*get_variable_with_args (const char *str, const char *args, int *args_flag)
{
	Alias	*var = NULL;
	char	*ret = NULL;
	char	*name = NULL;
	char	*freep = NULL;
	int	copy = 1;
	int	local = 0;

	freep = name = remove_brackets(str, args, args_flag);

	/* ugh.  support '$:' expando first (otherwise the namespace decoder
	 * will get confused), then check variables, then builtins, then
	 * SETs, then environment variables. */
	if (name[0] == ':' && name[1] == '\0') {
	    copy = 0;
	    ret = built_in_alias(*name, NULL);
	} else if ((var = find_variable(name, 1)) != NULL)
	    ret = var->stuff;
	else if (str[1] == '\0' && (ret = built_in_alias(*str, NULL)))
	    copy = 0;
	else if ((ret = make_string_var(str)))
		copy = 0;
	else if ((ret = getenv(str)))
	    ;
	else if (x_debug & DEBUG_UNKNOWN && ret == NULL)
	    yell("Variable lookup to non-existant assign [%s]", name);

	new_free(&freep);
	return (copy ? m_strdup(ret) : ret);
}

/*
 * This function will try to 'complete' a command alias with the given name.
 * It looks for all the aliases in the current namespace (should normally be
 * the root space, but...) with 'name' as the beginning component of their
 * name.  If it finds an exact match, it stores the value -1 in howmany.  If
 * there were no matches then 0 is stored.  If there were non-exact matches
 * it stores the number available.  If one exact or non-exact match is found
 * then that name is returned.  Otherwise, NULL is returned.
 */
char *complete_cmd_alias(const char *name, int *howmany) {
    Alias *item;
    int len = strlen(name);
    char *ret = NULL;

    *howmany = 0;

    LIST_FOREACH(item, &namespaces.current->flist, lp) {
	if (!strncasecmp(item->name, name, len)) {
	    /* a match.. see what sort it is.. */
	    if (strlen(item->name) == len) {
		/* an exact match.  set howmany, set complete_name, then go
		 * home. */
		*howmany = -1;
		return item->name;
	    }
	    *howmany++;
	    ret = item->name;
	}
    }

    if (*howmany == 1)
	return ret;
    else
	return NULL;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 * XXX: Namespace support is missing (it assumes root space)
 */
char **	glob_cmd_alias (char *name, int *howmany)
{
	Alias *ap;
	int len;
	char **matches = NULL;
	int matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	LIST_FOREACH(ap, &namespaces.root->flist, lp) {
	    if (!strncasecmp(ap->name, name, len)) {
		if (strchr(ap->name + len, '.'))
			continue;

		if (*howmany >= matches_size) {
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(ap->name);
		*howmany += 1;
	    }
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 * XXX: Namespace support is missing (it assumes root space)
 */
char **	glob_assign_alias (char *name, int *howmany)
{
	Alias *ap;
	int len;
	char **matches = NULL;
	int matches_size = 0;

	len = strlen(name);
	*howmany = 0;
	LIST_FOREACH(ap, &namespaces.root->flist, lp) {
	    if (!strncasecmp(ap->name, name, len)) {
		if (strchr(ap->name + len, '.'))
			continue;

		if (*howmany >= matches_size) {
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(ap->name);
		*howmany += 1;
	    }
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 */
char **	pmatch_cmd_alias (char *name, int *howmany)
{
	Alias *ap;
	int len;
	char **matches = NULL;
	int matches_size = 0;

	len = strlen(name);
	*howmany = 0;

	LIST_FOREACH(ap, &namespaces.root->flist, lp) {
	    if (wild_match(name, ap->name)) {
		if (strchr(ap->name + len, '.'))
			continue;

		if (*howmany >= matches_size) {
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(ap->name);
		*howmany += 1;
	    }
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 */
char **	pmatch_assign_alias (char *name, int *howmany)
{
	Alias *ap;
	int len;
	char **matches = NULL;
	int matches_size = 0;

	len = strlen(name);
	*howmany = 0;

	LIST_FOREACH(ap, &namespaces.root->vlist, lp) {
	    if (wild_match(name, ap->name)) {
		if (strchr(ap->name + len, '.'))
			continue;

		if (*howmany >= matches_size) {
			matches_size += 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strdup(ap->name);
		*howmany += 1;
	    }
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * This function is strictly O(N).  This should probably be addressed.
 * XXX: No namespace support yet.
 */
char **	get_subarray_elements (char *root, int *howmany, int type)
{
	struct alias_list *list;
	Alias *ap;
	int pos, cnt, max;
	int cmp = 0;
	char **matches = NULL;
	int matches_size = 0;
	size_t end;
	char *last = NULL;

	if (type == COMMAND_ALIAS)
	    list = &namespaces.root->flist;
	else
	    list = &namespaces.root->vlist;

	root = m_2dup(root, ".");
	*howmany = 0;
	cmp = strlen(root);

	LIST_FOREACH(ap, list, lp) {
	    if (!strncasecmp(ap->name, root, cmp)) {
		end = strcspn(ap->name + cmp, ".");
		if (last != NULL && !strncasecmp(ap->name, last, cmp + end))
		    continue; /* same as the last match.  move on. */

		if (*howmany >= matches_size) {
			matches_size = *howmany + 5;
			RESIZE(matches, char *, matches_size + 1);
		}
		matches[*howmany] = m_strndup(ap->name, cmp + end);
		last = matches[*howmany];
		*howmany += 1;
	    }
	}

	if (*howmany)
		matches[*howmany] = NULL;
	else
		new_free((char **)&matches);

	return matches;
}

/*
 * parse_line_with_return: Creates a local stack and executes parse_line
 * inside it, then returns the result (as stored in FUNCTION_RETURN).
 */
char *parse_line_with_return (char *name, char *what, char *args) {
    int	old_last_function_call_level = last_function_call_level;
    char *result = NULL;

    make_local_stack(name);
    last_function_call_level = wind_index;
    add_local_var("FUNCTION_RETURN", empty_string, 0);

    will_catch_return_exceptions++;
    parse_line(NULL, what, args, 0);
    will_catch_return_exceptions--;
    return_exception = 0;

    result = get_variable("FUNCTION_RETURN");
    last_function_call_level = old_last_function_call_level;

    destroy_local_stack();
    return result;
}

/************************************************************************/
/*
 * Call a user alias as a function.  This is very similar to call_user_alias
 * below, except that we monitor our return value and send it back as well.
 */
char 	*call_user_function	(char *alias_name, char *args)
{
	Alias *alias;
	int old_last_function_call_level = last_function_call_level;
	char *result = NULL;
	namespace_t *old_ns = namespaces.current;

	if ((alias = find_cmd_alias(alias_name)) == NULL) {
	    if (x_debug & DEBUG_UNKNOWN)
		yell("Function call to non-existant alias [%s]", alias_name);
	    return m_strdup(empty_string);
	}

	make_local_stack(alias->name);
	namespaces.current = alias->nspace;

	prepare_alias_call(alias->arglist, &args);
	last_function_call_level = wind_index;
	add_local_var("FUNCTION_RETURN", empty_string, 0);

	will_catch_return_exceptions++;
	parse_line(NULL, alias->stuff, args, 1);
	will_catch_return_exceptions--;
	return_exception = 0;

	result = get_variable("FUNCTION_RETURN");
	last_function_call_level = old_last_function_call_level;
	destroy_local_stack();

	return (result == NULL ? m_strdup(empty_string) : result);
}

/*
 * Call a user alias.  We handle setting the namespace gunk and all of that
 * here.
 */
void	call_user_alias	(Alias *alias, char *args) {
	namespace_t *old_ns = namespaces.current;

	make_local_stack(alias->name);
	namespaces.current = alias->nspace;

	will_catch_return_exceptions++;
	prepare_alias_call(alias->arglist, &args);
	parse_line(NULL, alias->stuff, args, 1);
	will_catch_return_exceptions--;
	return_exception = 0;

	namespaces.current = old_ns;
	destroy_local_stack();
}


/*
 * save_aliases: This will write all of the aliases to the FILE pointer fp in
 * such a way that they can be read back in using LOAD or the -l switch 
 */
void 	save_assigns	(FILE *fp, int do_all)
{
#if 0
	int cnt = 0;

	for (cnt = 0; cnt < variable.max; cnt++)
	{
		if (!variable.list[cnt]->global || do_all)
		{
			if (variable.list[cnt]->stub)
				fprintf(fp, "STUB ");
			fprintf(fp, "ASSIGN %s %s\n", variable.list[cnt]->name, variable.list[cnt]->stuff);
		}
	}
#endif
}

void 	save_aliases (FILE *fp, int do_all)
{
#if 0
	int	cnt = 0;

	for (cnt = 0; cnt < cmd_alias.max; cnt++)
	{
		if (!cmd_alias.list[cnt]->global || do_all)
		{
			if (cmd_alias.list[cnt]->stub)
				fprintf(fp, "STUB ");
			fprintf(fp, "ALIAS %s {%s}\n", cmd_alias.list[cnt]->name, cmd_alias.list[cnt]->stuff);
		}
	}	
#endif
}

static
void 	destroy_aliases (hashtable_t *table, struct alias_list *list) {
	Alias *ap;

	while (!LIST_EMPTY(list)) {
	    ap =  LIST_FIRST(list);
	    LIST_REMOVE(ap, lp);
	    hash_delete(table, ap);
	    new_free(&ap->stuff);
	    new_free(&ap->stub);
	    new_free(&ap->filename);
	    new_free((char **)&ap);
	}
}

/******************* RUNTIME STACK SUPPORT **********************************/

void 	make_local_stack 	(const char *name)
{
	wind_index++;

	if (wind_index >= max_wind)
	{
		int i;

		if (max_wind == -1)
			max_wind = 8;
		else
			max_wind <<= 1;

		RESIZE(call_stack, RuntimeStack, max_wind);
		for (i = wind_index;i < max_wind;i++) {
		    memset(&call_stack[i], 0, sizeof(RuntimeStack));
		    LIST_ALLOC(call_stack[i].vlist);
		    call_stack[i].parent = -1;
		}
	}

	/* Just in case... */
	destroy_local_stack();
	wind_index++;		/* XXXX - chicanery */

	if (name) {
		call_stack[wind_index].name = name;
		call_stack[wind_index].parent = -1;
	} else {
		call_stack[wind_index].name = empty_string;
		call_stack[wind_index].parent = wind_index - 1;
	}
	if (call_stack[wind_index].vtable == NULL)
	    call_stack[wind_index].vtable = create_hash_table(4,
		    sizeof(Alias), NAMESPACE_HASH_SANITYLEN,
		    HASH_FL_NOCASE | HASH_FL_STRING, (hash_cmpfunc)strcasecmp);
	else
	    if (!LIST_EMPTY(call_stack[wind_index].vlist))
		yell("make_local_stack(): stack frame list is non-empty!");
	LIST_INIT(call_stack[wind_index].vlist);
	call_stack[wind_index].locked = 0;
}

int	find_locked_stack_frame	(void)
{
	int i;
	for (i = 0; i < wind_index; i++)
		if (call_stack[i].locked)
			return i;

	return -1;
}

void	bless_local_stack	(void)
{
	call_stack[wind_index].name = empty_string;
	call_stack[wind_index].parent = find_locked_stack_frame();
}

void 	destroy_local_stack 	(void)
{
	/*
	 * We clean up as best we can here...
	 */
	if (!LIST_EMPTY(call_stack[wind_index].vlist))
	    destroy_aliases(call_stack[wind_index].vtable,
		    call_stack[wind_index].vlist);
	if (call_stack[wind_index].current)
		call_stack[wind_index].current = 0;
	if (call_stack[wind_index].name)
		call_stack[wind_index].name = 0;

	wind_index--;
}

void 	set_current_command 	(char *line)
{
	call_stack[wind_index].current = line;
}

void 	unset_current_command 	(void)
{
	call_stack[wind_index].current = NULL;
}

void	lock_stack_frame 	(void)
{
	call_stack[wind_index].locked = 1;
}

void	unlock_stack_frame	(void)
{
	int lock = find_locked_stack_frame();
	if (lock >= 0)
		call_stack[lock].locked = 0;
}

void 	dump_call_stack 	(void)
{
	int my_wind_index = wind_index;
	if (wind_index >= 0)
	{
		yell("Call stack");
		while (my_wind_index--)
			yell("[%3d] %s", my_wind_index, call_stack[my_wind_index].current);
		yell("End of call stack");
	}
}

void 	panic_dump_call_stack 	(void)
{
	int my_wind_index = wind_index;
	fprintf(stderr, "Call stack\n");
	if (wind_index >= 0)
	{
		while (my_wind_index--)
			fprintf(stderr, "[%3d] %s\n", my_wind_index, call_stack[my_wind_index].current);
	}
	else
		fprintf(stderr, "Stack is corrupted [wind_index is %d], sorry.\n",
			wind_index);
	fprintf(stderr, "End of call stack\n");
}


/*
 * You may NOT call this unless youre about to exit.
 * If you do (call this when youre not about to exit), and you do it more 
 * than a few times, max_wind will get absurdly large.  So dont do it.
 *
 * XXXX - this doesnt clean up everything -- but do i care?
 */
void 	destroy_call_stack 	(void)
{
	wind_index = -1;
	new_free((char **)&call_stack);
}

/******************* expression and text parsers ***************************/
/* XXXX - bogus for now */
#include "expr2.c"
#include "expr.c"


/****************************** ALIASCTL ************************************/
#if 0
#define EMPTY empty_string
#define RETURN_EMPTY return m_strdup(EMPTY)
#define RETURN_IF_EMPTY(x) if (empty( x )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY(y); x = my_atol(safe_new_next_arg(y, &y));}
#define GET_FLOAT_ARG(x, y) {RETURN_IF_EMPTY(y); x = atof(safe_new_next_arg(y, &y));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY(y); x = new_next_arg(y, &y);RETURN_IF_EMPTY(x);}
#define RETURN_STR(x) return m_strdup((x) ? (x) : EMPTY)
#define RETURN_INT(x) return m_strdup(ltoa((x)))
#endif

/* Used by function_aliasctl */
/* MUST BE FIXED */
char 	*aliasctl 	(char *input)
{
	int list = -1;
	char *listc;
	enum { EXISTS, GET, SET, MATCH, PMATCH, GETPACKAGE, SETPACKAGE } op;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "AS", 2))
		list = VAR_ALIAS;
	else if (!my_strnicmp(listc, "AL", 2))
		list = COMMAND_ALIAS;
	else if (!my_strnicmp(listc, "LO", 2))
		list = VAR_ALIAS_LOCAL;
	else
		RETURN_EMPTY;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "GETP", 4))
		op = GETPACKAGE;
	else if (!my_strnicmp(listc, "G", 1))
		op = GET;
	else if (!my_strnicmp(listc, "SETP", 4))
		op = SETPACKAGE;
	else if (!my_strnicmp(listc, "S", 1))
		op = SET;
	else if (!my_strnicmp(listc, "M", 1))
		op = MATCH;
	else if (!my_strnicmp(listc, "P", 1))
		op = PMATCH;
	else if (!my_strnicmp(listc, "E", 1))
		op = EXISTS;
	else
		RETURN_EMPTY;

	GET_STR_ARG(listc, input);
	switch (op)
	{
		case (GET) :
		case (EXISTS) :
		case (GETPACKAGE) :
		case (SETPACKAGE) :
		{
			Alias *alias = NULL;

			upper(listc);
			if (list == VAR_ALIAS_LOCAL)
				alias = find_local_var(listc, NULL);
			else if (list == VAR_ALIAS)
				alias = find_variable(listc, 0);
			else
				alias = find_cmd_alias(listc);

			if (alias)
			{
				if (op == GET)
					RETURN_STR(alias->stuff);
				else if (op == EXISTS)
					RETURN_INT(1);
				else if (op == GETPACKAGE)
					RETURN_STR(alias->filename);
				else /* op == SETPACKAGE */
				{
					malloc_strcpy(&alias->filename, input);
					RETURN_INT(1);
				}
			}
			else
			{
				if (op == GET || op == GETPACKAGE)
					RETURN_EMPTY;
				else	/* EXISTS or SETPACKAGE */
					RETURN_INT(0);
			}
		}
		case (SET) :
		{
			upper(listc);
			if (list == VAR_ALIAS_LOCAL)
				add_local_var(listc, input, 0);
			else if (list == VAR_ALIAS)
				add_variable(listc, input, 0, 0);
			else
				add_cmd_alias(listc, NULL, input, 0);

			RETURN_INT(1);
		}
		case (MATCH) :
		{
			char **mlist = NULL;
			char *mylist = NULL;
			size_t	mylistclue = 0;
			int num = 0, ctr;

			if (!my_stricmp(listc, "*"))
				listc = empty_string;

			upper(listc);

			if (list == COMMAND_ALIAS)
				mlist = glob_cmd_alias(listc, &num);
			else
				mlist = glob_assign_alias(listc, &num);

			for (ctr = 0; ctr < num; ctr++)
			{
				m_sc3cat(&mylist, space, mlist[ctr], &mylistclue);
				new_free((char **)&mlist[ctr]);
			}
			new_free((char **)&mlist);
			if (mylist)
				return mylist;
			RETURN_EMPTY;
		}
		case (PMATCH) : 
		{
			char **	mlist = NULL;
			char *	mylist = NULL;
			size_t	mylistclue = 0;
			int	num = 0,
				ctr;

			if (list == COMMAND_ALIAS)
				mlist = pmatch_cmd_alias(listc, &num);
			else
				mlist = pmatch_assign_alias(listc, &num);

			for (ctr = 0; ctr < num; ctr++)
			{
				m_sc3cat(&mylist, space, mlist[ctr], &mylistclue);
				new_free((char **)&mlist[ctr]);
			}
			new_free((char **)&mlist);
			if (mylist)
				return mylist;
			RETURN_EMPTY;
		}
		default :
			error("aliasctl: Error");
			RETURN_EMPTY;
	}
	RETURN_EMPTY;
}

/*************************** stacks **************************************/
typedef	struct	aliasstacklist
{
	int	which;
	char	*name;
	char	*nspace;
	Alias	*list;
	struct aliasstacklist *next;
}	AliasStack;

static  AliasStack *	alias_stack = NULL;
static	AliasStack *	assign_stack = NULL;

void	do_stack_alias (int type, char *args, int which) {
	char		*name;
	AliasStack	*aptr, **aptrptr;
	Alias		*alptr;
	int		cnt;
	
	if (args)
	    upper(args);

	if (which == STACK_DO_ALIAS) {
	    name = "ALIAS";
	    aptrptr = &alias_stack;
	} else {
	    name = "ASSIGN";
	    aptrptr = &assign_stack;
	}

	if (!*aptrptr && (type == STACK_POP || type == STACK_LIST)) {
	    say("%s stack is empty!", name);
	    return;
	}

	if (type == STACK_PUSH) {
	    if (which == STACK_DO_ALIAS) {
		if ((alptr = find_cmd_alias(args)) != NULL) {
		    hash_delete(alptr->nspace->ftable, alptr);
		    LIST_REMOVE(alptr, lp);
		}
	    } else {
		if ((alptr = find_variable(args, 0)) != NULL) {
		    hash_delete(alptr->nspace->vtable, alptr);
		    LIST_REMOVE(alptr, lp);
		}
	    }

	    if (alptr == NULL)
		return; /* there's no reason to stack dead entries.. */

	    aptr = (AliasStack *)new_malloc(sizeof(AliasStack));
	    aptr->list = alptr;
	    aptr->name = m_strdup(args);
	    aptr->nspace = namespace_get_full_name(alptr->nspace, 1);
	    aptr->next = aptrptr ? *aptrptr : NULL;
	    *aptrptr = aptr;
	    return;
	}

	if (type == STACK_POP) {
	    AliasStack *prev = NULL;

	    for (aptr = *aptrptr; aptr; prev = aptr, aptr = aptr->next) {
		/* have we found it on the stack? */
		if (!my_stricmp(args, aptr->name)) {
		    /* remove it from the list */
		    if (prev == NULL)
			*aptrptr = aptr->next;
		    else
			prev->next = aptr->next;

		    /* throw away anything we already have */
		    if (which == STACK_DO_ALIAS)
			delete_cmd_alias(args, 0);

		    /* See if the namespace for 'aptr' is still valid.  if
		     * it isn't, don't do anything else.. */
		    if ((aptr->list->nspace = namespace_find(aptr->nspace)) !=
			    NULL) {
			/* put the new one in. */
			if (which == STACK_DO_ALIAS) {
			    hash_insert(aptr->list->nspace->ftable,
				    aptr->list);
			    LIST_INSERT_HEAD(&aptr->list->nspace->flist,
				    aptr->list, lp);
			} else {
			    hash_insert(aptr->list->nspace->vtable,
				    aptr->list);
			    LIST_INSERT_HEAD(&aptr->list->nspace->vlist,
				    aptr->list, lp);
			}
		    }

		    /* free it */
		    new_free((char **)&aptr->name);
		    new_free((char **)&aptr->nspace);
		    new_free((char **)&aptr);
		    return;
		}
	    }
	    say("%s is not on the %s stack!", args, name);
	    return;
	}

	if (type == STACK_LIST) {
	    AliasStack	*tmp;

	    say("%s STACK LIST", name);
	    for (tmp = *aptrptr; tmp; tmp = tmp->next) {
		if (tmp->list->stub)
		    say("%s\t%s STUBBED TO %s", tmp->nspace, tmp->name,
			    tmp->list->stub);
		else
		    say("%s\t%s\t%s", tmp->nspace, tmp->name,
			    tmp->list->stuff);
	    }
	    return;
	}
	say("Unknown STACK type ??");
}

