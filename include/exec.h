/*
 * exec.h: header for exec.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information
 */

#ifndef __exec_h__
#define	__exec_h__

	BUILT_IN_COMMAND(execcmd);
	void	do_processes		(fd_set *, fd_set *);
	int	get_child_exit		(pid_t);
	void	clean_up_processes	(void);
	int	text_to_process		(int, const char *, int);
	void	exec_server_delete 	(int);
	void	add_process_wait	(int, const char *);
	int	get_process_index	(char **);  /* XXXX */
	int	is_valid_process	(const char *);
	int	process_is_running	(char *);

#endif /* _EXEC_H_ */
