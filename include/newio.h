/*
 * newio.h -- header file for newio.c
 *
 * Copyright 1990, 1995 Michael Sandrof, Matthew Green
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __newio_h__
#define __newio_h__

extern 	int 	dgets_errno;

	int 	dgets 			(int, char *, size_t, int, void *);
	int 	new_select 		(fd_set *, fd_set *, struct timeval *);
	int	new_open		(int);
	int	new_open_for_writing	(int);
	int 	new_close 		(int);
	void 	set_socket_options 	(int);
	size_t	get_pending_bytes	(int);
	int	new_hold_fd		(int);
	int	new_unhold_fd		(int);

#define IO_BUFFER_SIZE 8192

#endif
