/*
 * alias.h: header for alias.c 
 *
 * Written by Jeremy Nelson
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __alias_h__
#define __alias_h__

#include "bsdqueue.h"

/*
 * XXXX - These need to go away
 */
#define COMMAND_ALIAS 		0
#define VAR_ALIAS 		1
#define VAR_ALIAS_LOCAL 	2

/* This had to be sucked out of alias.c because namespaces need to know the
 * size of the alias item structure. */
typedef struct AliasItemStru {
	char	*name;			/* name of alias */
	struct namespace *nspace;		/* namespace we live in */
	char	*stuff;			/* what the alias is */
	char	*stub;			/* the file its stubbed to */
	char	*filename;		/* file it was loaded from */
	int	line;			/* line it was loaded from */
	int	global;			/* set if loaded from * `global' */
	struct ArgListT *arglist;	/* List of * arguments to * alias */
	LIST_ENTRY(AliasItemStru) lp;
}	Alias;

LIST_HEAD(alias_list, AliasItemStru);

/*
 * These are the user commands.  Dont call these directly.
 */
	BUILT_IN_COMMAND(aliascmd);
	BUILT_IN_COMMAND(assigncmd);
	BUILT_IN_COMMAND(localcmd);
	BUILT_IN_COMMAND(stubcmd);
	BUILT_IN_COMMAND(dumpcmd);
	BUILT_IN_COMMAND(unloadcmd);

	void 	add_variable      	(char *name, char *stuff, int noisy,
					 int stub);
	void 	add_local_var    	(char *name, char *stuff, int noisy);
#if 0	/* Internal now */
	void 	add_cmd_alias 	   	(void);
#endif

	char *	get_variable		(char *name);
	char **	glob_cmd_alias		(char *name, int *howmany);
	char *	get_cmd_alias   	(char *name, int *howmany, 
					 char **complete_name, void **args);
	char **	get_subarray_elements 	(char *root, int *howmany, int type);


/* These are in expr.c */
/*
 * This function is a general purpose interface to alias expansion.
 * The second argument is the text to be expanded.
 * The third argument are the command line expandoes $0, $1, etc.
 * The fourth argument is a flag whether $0, $1, etc are used
 * The fifth argument, if set, controls whether only the first "command"
 *   will be expanded.  If set, this argument will be set to the "rest"
 *   of the commands (after the first semicolon, or the null).  If NULL,
 *   then the entire text will be expanded.
 */
	char *	expand_alias 		(const char *, const char *, int *, char **);

/*
 * This is the interface to the "expression parser"
 * The first argument is the expression to be parsed
 * The second argument is the command line expandoes ($0, $1, etc)
 * The third argument will be set if the command line expandoes are used.
 */
	char *	parse_inline 		(char *, const char *, int *);

/*
 * This function is used to call a user-defined function.
 * Noone should be calling this directly except for call_function.
 */
	char *	call_user_function 	(char *, char *);
	void	call_user_alias		(char *, char *, char *, void *);


/*
 * This function is used to save all the current aliases to a global
 * file.  This is used by /SAVE and /ABORT.
 */
	void	save_assigns		(FILE *, int);
	void	save_aliases 		(FILE *, int);

/*
 * This function is in functions.c
 * This function allows you to execute a primitive "BUILT IN" expando.
 * These are the $A, $B, $C, etc expandoes.
 * The argument is the character of the expando (eg, 'A', 'B', etc)
 *
 * This is in functions.c
 */
	char *	built_in_alias		(char, int *);



/* BOGUS */

/*
 * This function is used to prepare the $* string before calling a user
 * alias or function.  You should pass in the last argument from get_cmd_alias
 * to this function, and also the $* value.  The second value may very well
 * be modified.
 */
	void	prepare_alias_call	(void *, char **);
	void	destroy_alias_call	(void *);

/*
 * This is in functions.c
 * This is only used by next_unit and expand_alias to call built in functions.
 * Noone should call this function directly.
 */
	char *	call_function		(char *, const char *, int *);



/*
 * These are the two primitives for runtime stacks.
 */
	void	make_local_stack 	(const char *);
	void	destroy_local_stack 	(void);
	void	set_current_command 	(char *);
	void	bless_local_stack 	(void);
	void	unset_current_command 	(void);
	void	lock_stack_frame	(void);
	void	unlock_stack_frame	(void);
	void	destroy_call_stack	(void);
	void	dump_call_stack		(void);
	void	panic_dump_call_stack 	(void);

/*
 * This is the alias interface to the /STACK command.
 */
	void	do_stack_alias 		(int, char *, int);

/*
 * Truly bogus. =)
 */
	char	*parse_line_with_return (char *, char *, char *, int, int);
	char 	*canon_number (char *input);

#endif /* _ALIAS_H_ */
