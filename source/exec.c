/* $EPIC: exec.c,v 1.10.2.1 2003/02/27 15:29:55 wd Exp $ */
/*
 * exec.c: handles exec'd process for IRCII 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright � 1997, 2002 EPIC Software Labs
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
#include "dcc.h"
#include "exec.h"
#include "vars.h"
#include "ircaux.h"
#include "commands.h"
#include "window.h"
#include "screen.h"
#include "hook.h"
#include "input.h"
#include "list.h"
#include "server.h"
#include "output.h"
#include "parse.h"
#include "newio.h"
#include "if.h"

#include <sys/wait.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif


/* Process: the structure that has all the info needed for each process */
typedef struct
{
	int	index;			/* Where in the proc array it is */
	char	*name;			/* full process name */
	char	*logical;
	pid_t	pid;			/* process-id of process */
	int	p_stdin;		/* stdin description for process */
	int	p_stdout;		/* stdout descriptor for process */
	int	p_stderr;		/* stderr descriptor for process */
	int	counter;		/* output line counter for process */
	char	*redirect;		/* redirection command (MSG, NOTICE) */
	unsigned refnum;		/* a window for output to go to */
	int	server;			/* the server to use for output */
	char	*who;			/* nickname used for redirection */
	int	exited;			/* true if process has exited */
	int	termsig;		/* The signal that terminated
					 * the process */
	int	retcode;		/* return code of process */
	List	*waitcmds;		/* commands queued by WAIT -CMD */
	int	dumb;			/* 0 if input still going, 1 if not */
	int	disowned;		/* 1 if we let it loose */
	char	*stdoutc;
	char	*stdoutpc;
	char	*stderrc;
	char	*stderrpc;
}	Process;


static	Process **process_list = NULL;
static	int	process_list_size = 0;

static 	void 	handle_filedesc 	(Process *, int *, int, int);
static 	void 	cleanup_dead_processes 	(void);
static 	void 	ignore_process 		(int index);
static 	void 	close_in		(int index);
static 	void 	kill_process 		(int, int);
static 	void 	kill_all_processes 	(int signo);
static 	int 	valid_process_index 	(int proccess);
static 	int 	is_logical_unique 	(char *logical);
static	int 	logical_to_index 	(const char *logical);

/*
 * A nice array of the possible signals.  Used by the coredump trapping
 * routines and in the exec.c package 
 */
#ifndef SYS_SIGLIST_DECLARED
#include "sig.inc"
#endif


/*
 * exec: the /EXEC command.  Does the whole shebang for ircII sub procceses
 * I melded in about half a dozen external functions, since they were only
 * ever used here.  Perhaps at some point theyll be broken out again, but
 * not until i regain control of this module.
 */
BUILT_IN_COMMAND(execcmd)
{
	char		*who = NULL,
			*logical = NULL,
			*redirect = NULL,
			*flag;
	unsigned 	refnum = 0;
	int		sig,
			len,
			i,
			refnum_flag = 0,
			logical_flag = 0;
	int		direct = 0;
	Process		*proc;
	char		*stdoutc = NULL,
			*stderrc = NULL,
			*stdoutpc = NULL,
			*stderrpc = NULL,
			*endc = NULL;

	/*
	 * If no args are given, list all the processes.
	 */
	if (!*args)
	{
		int	i;

		if (!process_list)
		{
			say("No processes are running");
			return;
		}

		say("Process List:");
		for (i = 0; i < process_list_size; i++)
		{
			Process	*proc;

			if (((proc = process_list[i])) == NULL)
				continue;

			if (proc->logical)
				say("\t%d (%s) (pid %d): %s", 
				    i, proc->logical, proc->pid, proc->name);
			else
				say("\t%d (pid %d): %s", 
				    i, proc->pid, proc->name);
		}
		return;
	}

	/*
	 * Walk through and parse out all the args
	 */
	while ((*args == '-') && (flag = next_arg(args, &args)))
	{
		flag++;
		len = strlen(flag);

		/*
		 * /EXEC -OUT redirects all output from the /exec to the
		 * current channel for the current window.
		 */
		if (my_strnicmp(flag, "OUT", len) == 0)
		{
			if (get_server_doing_privmsg(from_server))
				redirect = "NOTICE";
			else
				redirect = "PRIVMSG";

			if (!(who = get_channel_by_refnum(0)))
			{
			     say("No current channel in this window for -OUT");
			     return;
			}
		}

		/*
		 * /EXEC -NAME gives the /exec a logical name that can be
		 * refered to as %name 
		 */
		else if (my_strnicmp(flag, "NAME", len) == 0)
		{
			logical_flag = 1;
			if (!(logical = next_arg(args, &args)))
			{
				say("You must specify a logical name");
				return;
			}
		}

		/*
		 * /EXEC -WINDOW forces all output for an /exec to go to
		 * the window that is current at this time
		 */
		else if (my_strnicmp(flag, "WINDOW", len) == 0)
		{
			refnum_flag = 1;
			refnum = current_refnum();
		}

		/*
		 * /EXEC -MSG <target> redirects the output of an /exec 
		 * to the given target.
		 */
		else if (my_strnicmp(flag, "MSG", len) == 0)
		{
			if (get_server_doing_privmsg(from_server))
				redirect = "NOTICE";
			else
				redirect = "PRIVMSG";

			if (!(who = next_arg(args, &args)))
			{
				say("No nicknames specified");
				return;
			}
		}

		/*
		 * /EXEC -LINE  specifies the stdout callback
		 */
		else if (my_strnicmp(flag, "LINE", len) == 0)
		{
			if ((stdoutc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -LINE flag");
		}

		/*
		 * /EXEC -LINEPART specifies the stdout partial line callback
		 */
		else if (my_strnicmp(flag, "LINEPART", len) == 0)
		{
			if ((stdoutpc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -LINEPART flag");
		}

		/*
		 * /EXEC -ERROR specifies the stderr callback
		 */
		else if (my_strnicmp(flag, "ERROR", len) == 0)
		{
			if ((stderrc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -ERROR flag");
		}

		/*
		 * /EXEC -ERRORPART specifies the stderr part line callback
		 */
		else if (my_strnicmp(flag, "ERRORPART", len) == 0)
		{
			if ((stderrpc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -ERRORPART flag");
		}

		/*
		 * /EXEC -END  specifies the final collection callback
		 */
		else if (my_strnicmp(flag, "END", len) == 0)
		{
			if ((endc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -END flag");
		}

		/*
		 * /EXEC -CLOSE forcibly closes all the fd's to a process,
		 * in the hope that it will take the hint.
		 */
		else if (my_strnicmp(flag, "CLOSE", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			ignore_process(i);
			return;
		}

		/*
		 * /EXEC -CLOSEIN close the processes STDIN,
		 * in the hope that it will take the hint.
		 */
		else if (my_strnicmp(flag, "CLOSEIN", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			close_in(i);
			return;
		}

		/*
		 * /EXEC -NOTICE <target> redirects the output of an /exec 
		 * to a specified target
		 */
		else if (my_strnicmp(flag, "NOTICE", len) == 0)
		{
			redirect = "NOTICE";
			if (!(who = next_arg(args, &args)))
			{
				say("No nicknames specified");
				return;
			}
		}

		/*
		 * /EXEC -IN sends a line of text to a process
		 */
		else if (my_strnicmp(flag, "IN", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			text_to_process(i, args, 1);
			return;
		}

		/*
		 * /EXEC -DIRECT suppresses the use of a shell
		 */
		else if (my_strnicmp(flag, "DIRECT", len) == 0)
			direct = 1;


		/*
		 * All other - arguments are implied KILLs
		 */
		else
		{
			if (*args != '%')
			{
				say("%s is not a valid process", args);
				return;
			}

			/*
			 * Check for a process to kill
			 */
			if ((i = get_process_index(&args)) == -1)
				return;

			/*
			 * Handle /exec -<num> %<process>
			 */
			if ((sig = my_atol(flag)) > 0)
			{
				if ((sig > 0) && (sig < NSIG))
					kill_process(i, sig);
				else
					say("Signal number can be from 1 to %d", NSIG);
				return;
			}

			/*
			 * Handle /exec -<SIGNAME> %<process>
			 */
			for (sig = 1; sig < NSIG; sig++)
			{
				if (!sys_siglist[sig])
					continue;
				if (!my_strnicmp(sys_siglist[sig], flag, len))
				{
					kill_process(i, sig);
					return;
				}
			}

			/*
			 * Give up! =)
			 */
			say("No such signal: %s", flag);
			return;
		}
	}

	/*
	 * This handles the form:
	 *
	 *	/EXEC <flags> %process
	 *
	 * Where the user wants to redefine some options for %process.
	 */
	if (*args == '%')
	{
		int i;

		/*
		 * Make sure the process is actually running
		 */
		if ((i = get_process_index(&args)) == -1)
			return;

		proc = process_list[i];
		message_to(refnum);

		/*
		 * Check to see if the user wants to change windows
		 */
		if (refnum_flag)
		{
			proc->refnum = refnum;
			if (refnum)
say("Output from process %d (%s) now going to this window", i, proc->name);
			else
say("Output from process %d (%s) not going to any window", i, proc->name);
		}

		/*
		 * Check to see if the user is changing the default target
		 */
		malloc_strcpy(&(proc->redirect), redirect);
		malloc_strcpy(&(proc->who), who);

		if (redirect)
say("Output from process %d (%s) now going to %s", i, proc->name, who);
		else
say("Output from process %d (%s) now going to you", i, proc->name);

		/*
		 * Check to see if the user changed the NAME of %proc.
		 */
		if (logical_flag)
		{
			if (is_logical_unique(logical))
			{
				malloc_strcpy(&proc->logical, logical);
				say("Process %d (%s) is now called %s",
					i, proc->name, proc->logical);
			} 
			else 
				say("The name %s is not unique!", logical);
		}

		/*
		 * Change the stdout and stderr lines _if_ they are given.
		 */
		if (stdoutc)
			malloc_strcpy(&proc->stdoutc, stdoutc);
		if (stdoutpc)
			malloc_strcpy(&proc->stdoutpc, stdoutpc);
		if (stderrc)
			malloc_strcpy(&proc->stderrc, stderrc);
		if (stderrpc)
			malloc_strcpy(&proc->stderrpc, stderrpc);

		message_to(0);
	}

	/*
	 * The user is trying to fire up a new /exec, so pass the buck.
	 */
	else
	{
		int	p0[2], p1[2], p2[2],
			pid, cnt;
		char	*shell,
			*flag,
			*arg;
		char	*name = args;

		if (!is_logical_unique(logical))
		{
			say("The name %s is not unique!", logical);
			return;
		}

		p0[0] = p1[0] = p2[0] = -1;
		p0[1] = p1[1] = p2[1] = -1;

		/*
		 * Open up the communication pipes
		 */
		if (pipe(p0) || pipe(p1) || pipe(p2))
		{
			say("Unable to start new process: %s", strerror(errno));
			new_close(p0[0]);
			new_close(p0[1]);
			new_close(p1[0]);
			new_close(p1[1]);
			new_close(p2[0]);
			new_close(p2[1]);
			return;
		}

		switch ((pid = fork()))
		{
		case -1:
			say("Couldn't start new process!");
			break;

		/*
		 * CHILD: set up and exec the process
		 */
		case 0:
		{
			int i;

			/*
			 * Fire up a new job control session,
			 * Sever all ties we had with the parent ircII process
			 */
			setsid();
			setuid(getuid());
			setgid(getgid());
			my_signal(SIGINT, SIG_IGN);
			my_signal(SIGQUIT, SIG_DFL);
			my_signal(SIGSEGV, SIG_DFL);
			my_signal(SIGBUS, SIG_DFL);

			dup2(p0[0], 0);
			dup2(p1[1], 1);
			dup2(p2[1], 2);
			for (i = 3; i < 256; i++)
				close(i);

			/*
			 * Pretend to be just a dumb terminal
			 */
			setenv("TERM", "tty", 1);

			/*
			 * Figure out what shell (if any) we're using
			 */
			shell = get_string_var(SHELL_VAR);

			/*
			 * If we're not using a shell, doovie up the exec args
			 * array and pass it off to execvp
			 */
			if (direct || !shell)
			{
				char	**args;
				int	max;

				cnt = 0;
				max = 5;
				args = new_malloc(sizeof(char *) * max);
				while ((arg = new_next_arg(name, &name)))
				{
					if (cnt == max)
					{
						max += 5;
						RESIZE(args, char *, max);
					}

					args[cnt++] = arg;
				}
				args[cnt] = NULL;
				execvp(args[0], args);
			}

			/*
			 * If we're using a shell, let them have all the fun
			 */
			else
			{
				if (!(flag = get_string_var(SHELL_FLAGS_VAR)))
					flag = empty_string;

				execl(shell, shell, flag, name, NULL);
			}

			/*
			 * Something really died if we got here
			 */
			printf("*** Error starting shell \"%s\": %s\n", 
					shell, strerror(errno));
			_exit(-1);
			break;
		}

		/*
		 * PARENT: add the new process to the process table list
		 */
		default:
		{
			int	i;
			Process	*proc = new_malloc(sizeof(Process));

			new_close(p0[0]);
			new_close(p1[1]);
			new_close(p2[1]);

			/*
			 * Init the proc list if neccesary
			 */
			if (!process_list)
			{
			    process_list = new_malloc(sizeof(Process *));
			    process_list_size = 1;
			    process_list[0] = NULL;
			}

			/*
			 * Find the first empty proc entry
			 */
			for (i = 0; i < process_list_size; i++)
			{
				if (!process_list[i])
				{
					process_list[i] = proc;
					break;
				}
			}

			/*
			 * If there are no empty proc entries, make a new one.
			 */
			if (i == process_list_size)
			{
			    process_list_size++;
			    RESIZE(process_list, Process *, process_list_size);
			    process_list[i] = proc;
			}

			/*
			 * Fill in the new proc entry
			 */
			proc->name = m_strdup(name);
			if (logical)
				proc->logical = m_strdup(logical);
			else
				proc->logical = NULL;
			proc->index = i;
			proc->pid = pid;
			proc->p_stdin = p0[1];
			proc->p_stdout = p1[0];
			proc->p_stderr = p2[0];
			proc->refnum = refnum;
			proc->redirect = NULL;
			if (redirect)
				proc->redirect = m_strdup(redirect);
			proc->server = from_server;
			proc->counter = 0;
			proc->exited = 0;
			proc->termsig = 0;
			proc->retcode = 0;
			proc->waitcmds = NULL;
			proc->who = NULL;
			proc->disowned = 0;
			if (who)
				proc->who = m_strdup(who);
			proc->dumb = 0;

			if (stdoutc)
				proc->stdoutc = m_strdup(stdoutc);
			else
				proc->stdoutc = NULL;

			if (stdoutpc)
				proc->stdoutpc = m_strdup(stdoutpc);
			else
				proc->stdoutpc = NULL;

			if (stderrc)
				proc->stderrc = m_strdup(stderrc);
			else
				proc->stderrc = NULL;

			if (stderrpc)
				proc->stderrpc = m_strdup(stderrpc);
			else
				proc->stderrpc = NULL;

			if (endc)
				add_process_wait(proc->index, endc);

			new_open(proc->p_stdout);
			new_open(proc->p_stderr);
			break;
		}
		}
	}

	cleanup_dead_processes();
}

/*
 * do_processes: This is called from the main io() loop to handle any
 * pending /exec'd events.  All this does is call handle_filedesc() on
 * the two reading descriptors.  If an EOF is asserted on either, then they
 * are closed.  If EOF has been asserted on both, then  we mark the process
 * as being "dumb".  Once it is reaped (exited), it is expunged.
 */
void 		do_processes (fd_set *rd, fd_set *wd)
{
	int	i;
	int	limit;

	if (!process_list)
		return;

	limit = get_int_var(SHELL_LIMIT_VAR);
	for (i = 0; i < process_list_size; i++)
	{
		Process *proc = process_list[i];

		if (!proc)
			continue;

		if (proc->p_stdout != -1 && FD_ISSET(proc->p_stdout, rd))
		{
			FD_CLR(proc->p_stdout, rd);
			handle_filedesc(proc, &proc->p_stdout, 
					EXEC_PROMPT_LIST, EXEC_LIST);
		}

		if (proc->p_stderr != -1 && FD_ISSET(proc->p_stderr, rd))
		{
			FD_CLR(proc->p_stderr, rd);
			handle_filedesc(proc, &proc->p_stderr,
					EXEC_PROMPT_LIST, EXEC_ERRORS_LIST);
		}

		if (limit && proc->counter >= limit)
			ignore_process(proc->index);
	}

	/* Clean up any (now) dead processes */
	cleanup_dead_processes();
}

/*
 * This is the back end to do_processes, saves some repeated code
 */
static void 	handle_filedesc (Process *proc, int *fd, int hook_nonl, int hook_nl)
{
	char 	exec_buffer[IO_BUFFER_SIZE + 1];
	int	len;

	switch ((len = dgets(exec_buffer, *fd, 0, NULL))) /* No buffering! */
	{
		case -1:	/* Something died */
		{
			*fd = new_close(*fd);
			if (proc->p_stdout == -1 && proc->p_stderr == -1)
				proc->dumb = 1;
			break;
		}
		case 0:		/* We didnt get a full line */
		{
			/* 
			 * XXX XXX Major, world class hack here XXX XXX 
			 * Since this problem has been addressed already
			 * by newio2.c, it doesn't matter if we just fix the
			 * symptom here since this code is all obsolete.
			 */
			if (exec_buffer[0] == '\n')
			{
				exec_buffer[0] = 0;
				goto this_sucks;
			}

			     if (hook_nl == EXEC_LIST && proc->stdoutpc)
				parse_line("EXEC", proc->stdoutpc, 
						exec_buffer, 0, 0);
			else if (hook_nl == EXEC_ERRORS_LIST && proc->stderrpc)
				parse_line("EXEC", proc->stderrpc, 
						exec_buffer, 0, 0);
			else if (proc->logical)
				do_hook(hook_nonl, "%s %s", 
					proc->logical, exec_buffer);
			else
				do_hook(hook_nonl, "%d %s", 
					proc->index, exec_buffer);

			set_prompt_by_refnum(proc->refnum, exec_buffer);
			break;
		}
		default:	/* We got a full line */
this_sucks:
		{
			int ofs;

			ofs = from_server;
			if (proc->refnum)
				from_server = proc->server;
			message_to(proc->refnum);
			proc->counter++;

			while (len > 0 && (exec_buffer[len] == '\n' ||
					   exec_buffer[len] == '\r'))
			{
				exec_buffer[len--] = 0;
			}

			if (proc->redirect) 
				redirect_text(proc->server, proc->who, 
					exec_buffer, proc->redirect, 1);

			     if (hook_nl == EXEC_LIST && proc->stdoutc)
				parse_line("EXEC", proc->stdoutc, 
						exec_buffer, 0, 0);
			else if (hook_nl == EXEC_ERRORS_LIST && proc->stderrc)
				parse_line("EXEC", proc->stderrc, 
						exec_buffer, 0, 0);
			else if (proc->logical)
			{
				if ((do_hook(hook_nl, "%s %s", 
						proc->logical, exec_buffer)))
					if (!proc->redirect)
						put_it("%s", exec_buffer);
			}
			else
			{
				if ((do_hook(hook_nl, "%d %s", 
						proc->index, exec_buffer)))
					if (!proc->redirect)
						put_it("%s", exec_buffer);
			}

			message_to(0);
			from_server = ofs;
		}
	}
}

/*
 * get_child_exit: This looks for dead child processes of the client.
 * There are two main sources of dead children:  Either an /exec'd process
 * has exited, or the client has attempted to fork() off a helper process
 * (such as wserv or gzip) and that process has choked on itself.
 *
 * When SIGCHLD is recieved, the global variable 'dead_children_processes'
 * is incremented.  When this function is called, we go through and call
 * waitpid() on all of the outstanding zombies, conditionally stopping when
 * we reach a specific wanted sub-process.
 *
 * If you want to stop reaping children when a specific subprocess is 
 * reached, specify the process in 'wanted'.  If all youre doing is cleaning
 * up after zombies and /exec's, then 'wanted' should be -1.
 */
int 		get_child_exit (pid_t wanted)
{
	Process	*proc;
	pid_t	pid;
	int	status, i;

	/*
	 * Iterate until we've reaped all of the dead processes
	 * or we've found the one asked for.
	 */
	if (dead_children_processes)
	{
	    block_signal(SIGCHLD);
	    while ((pid = waitpid(wanted, &status, WNOHANG)) > 0)
	    {
		/*
		 * First thing we do is look to see if the process we're
		 * working on is the one that was asked for.  If it is,
		 * then we get its exit status information and return it.
		 */
		if (wanted != -1 && pid == wanted)
		{
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			if (WIFSTOPPED(status))
				return -(WSTOPSIG(status));
			if (WIFSIGNALED(status)) 
				return -(WTERMSIG(status));
		}

		/*
		 * If it wasnt the process asked for, then we've probably
		 * stumbled across a dead /exec'd process.  Look for the
		 * corresponding child process, and mark it as being dead.
		 */
		else
		{
			for (i = 0; i < process_list_size; i++)
			{
				proc = process_list[i];
				if (proc && proc->pid == pid)
				{
					proc->exited = 1;
					proc->termsig = WTERMSIG(status);
					proc->retcode = WEXITSTATUS(status);
					break;
				}
			}
		}
	    }
	    dead_children_processes = 0;
	    unblock_signal(SIGCHLD);
	}

	/*
	 * Now we may have reaped some /exec'd processes that were previously
	 * dumb and have now exited.  So we call cleanup_dead_processes() to 
	 * find and delete any such processes.
	 */
	cleanup_dead_processes();

	/*
	 * If 'wanted' is not -1, then we didnt find that process, and
	 * if 'wanted' is -1, then you should ignore the retval anyhow. ;-)
	 */
	return -1;
}


/*
 * clean_up_processes: In effect, we want to tell all of our sub processes
 * that we're going away.  We cant be 100% sure that theyre all dead by
 * the time this function returns, but we can be 100% sure that they will
 * be killed off next time they come up to run.  This is the only thing that
 * can be guaranteed, and is in fact all we really need to know.
 */
void 		clean_up_processes (void)
{
	if (process_list_size)
	{
		say("Closing all left over exec's");
		kill_all_processes(SIGTERM);
		sleep(2);
		get_child_exit(-1);
		kill_all_processes(SIGKILL);
	}
}


/*
 * text_to_process: sends the given text to the given process.  If the given
 * process index is not valid, an error is reported and 1 is returned.
 * Otherwise 0 is returned. 
 * Added show, to remove some bad recursion, phone, april 1993
 */
int 		text_to_process (int proc_index, const char *text, int show)
{
	Process	*	proc;
	char	*	my_buffer;

	if (valid_process_index(proc_index) == 0)
		return 1;

	proc = process_list[proc_index];

	message_to(proc->refnum);
	if (show)
		put_it("%s%s", get_prompt_by_refnum(proc->refnum), text);
	message_to(0);

	my_buffer = alloca(strlen(text) + 2);
	/* Obviously this is safe. */
	strcpy(my_buffer, text);
	strcat(my_buffer, "\n");
	write(proc->p_stdin, my_buffer, strlen(my_buffer));
	set_prompt_by_refnum(proc->refnum, empty_string);

	return (0);
}

/*
 * This adds a new /wait %proc -cmd   entry to a running process.
 */
void 		add_process_wait (int proc_index, const char *cmd)
{
	Process	*proc = process_list[proc_index];
	List	*new_ewl, *posn;

	new_ewl = new_malloc(sizeof(List));
	new_ewl->next = NULL;
	new_ewl->name = m_strdup(cmd);

	if ((posn = proc->waitcmds))
	{
		while (posn->next)
			posn = posn->next;
		posn->next = new_ewl;
	}
	else
		proc->waitcmds = new_ewl;

}



/* - - - - - - - - -  - - - - - - - - - - - - - - - - - */

/*
 * A process must go through two stages to be completely obliterated from
 * the client.  Either stage may happen first, but until both are completed
 * we keep the process around.
 *
 *	1) We must recieve an EOF on both stdin and stderr, or we must
 *	   have closed stdin and stderr already (handled by do_processes)
 *	2) The process must have died (handled by get_child_exit)
 *
 * The reason why both must happen is becuase the process can die (and
 * we would get an async signal) before we read all of its output on the
 * pipe, and if we simply deleted the process when it dies, we could lose
 * some of its output.  The reason why we cant delete a process that has
 * asserted EOF on its output is because it could still be running (duh! ;-)
 * So we wait for both to happen.
 */

/*
 * This function is called by the three places that can effect a change
 * on the state of a running process:
 * 	1) get_child_exit, which can mark a process as exited
 *	2) do_processes, which can mark a child as being done with I/O
 *	3) execcmd, which can mark a child as being done with I/O
 *
 * Any processes that are found to have both exited and having completed
 * their I/O will be summarily destroyed.
 */
static void 	cleanup_dead_processes (void)
{
	int	i;
	List	*cmd,
		*next;
	Process *dead, *proc;
	char	*exit_info;

	if (!process_list)
		return;		/* Nothing to do */

	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]))
			continue;

		/*
		 * We do not parse the process if it has not 
		 * both exited and finished its io, UNLESS
		 * it has been disowned.
		 */
		if ((!proc->exited || !proc->dumb) && !proc->disowned)
			continue;		/* Not really dead yet */

		dead = process_list[i];
		process_list[i] = NULL;

		/*
		 * First thing to do is fill out the exit information
		 */
		if (dead->logical)
		{
			size_t	len = strlen(dead->logical) + 25;

			exit_info = alloca(len);
			snprintf(exit_info, len, "%s %d %d", 
					dead->logical, dead->termsig,
					dead->retcode);
		}
		else
		{
			exit_info = alloca(40);
			snprintf(exit_info, 32, "%d %d %d",
				dead->index, dead->termsig, dead->retcode);
		}


		/*
		 * First thing we do is run any /wait %proc -cmd commands
		 */
		next = dead->waitcmds;
		dead->waitcmds = NULL;
		while ((cmd = next))
		{
			next = cmd->next;
			parse_line(NULL, cmd->name, exit_info, 0, 0);
			new_free(&cmd->name);
			new_free((char **)&cmd);
		}

		/*
		 * Throw /on exec_exit
		 */
		if (do_hook(EXEC_EXIT_LIST, "%s", exit_info))
		{
			if (get_int_var(NOTIFY_ON_TERMINATION_VAR))
			{
				if (dead->termsig > 0 && dead->termsig < NSIG)
	say("Process %d (%s) terminated with signal %s (%d)", 
	   dead->index, dead->name, sys_siglist[dead->termsig], dead->termsig);
				else if (dead->disowned)
	say("Process %d (%s) disowned", dead->index, dead->name);
				else
	say("Process %d (%s) terminated with return code %d", 
		dead->index, dead->name, dead->retcode);
			}
		}

		if (dead->p_stdin != -1)
			close(dead->p_stdin);
		new_close(dead->p_stdout);
		new_close(dead->p_stderr);
		new_free(&dead->name);
		new_free(&dead->logical);
		new_free(&dead->who);
		new_free(&dead->redirect);
		new_free(&dead->stdoutc);
		new_free(&dead->stdoutpc);
		new_free(&dead->stderrc);
		new_free(&dead->stderrpc);
		new_free((char **)&dead);
	}

	/*
	 * Resize away any dead processes at the end
	 */
	for (i = process_list_size - 1; i >= 0; i--)
	{
		if (process_list[i])
			break;
	}

	if (process_list_size != i + 1)
	{
		process_list_size = i + 1;
		RESIZE(process_list, Process, process_list_size);
	}
}



/*
 * ignore_process: When we no longer want to communicate with the process
 * any longer, we call here.  It continues execution until it is done, but
 * we are oblivious to any output it sends.  Now, it will get an EOF 
 * condition on its output fd, so it will probably either take the hint, or
 * its output will go the bit bucket (which we want to happen)
 */
static void 	ignore_process (int index)
{
	Process *proc;

	if (valid_process_index(index) == 0)
		return;

	proc = process_list[index];
	if (proc->p_stdin != -1)
		proc->p_stdin = new_close(proc->p_stdin);
	if (proc->p_stdout != -1)
		proc->p_stdout = new_close(proc->p_stdout);
	if (proc->p_stderr != -1)
		proc->p_stderr = new_close(proc->p_stderr);

	proc->dumb = 1;
}

/*
 * close_in:  When we are finished with the process but still want the
 * rest of its output, we close its input, and hopefully it will get the
 * message and close up shop.
 */
static void 	close_in (int index)
{
	Process *proc;

	if (valid_process_index(index) == 0)
		return;

	proc = process_list[index];
	if (proc->p_stdin != -1)
		proc->p_stdin = new_close(proc->p_stdin);
}




/*
 * kill_process: sends the given signal to the specified process.  It does
 * not delete the process from the process table or anything like that, it
 * only is for sending a signal to a sub process (most likely in an attempt
 * to kill it.)  The actual reaping of the children will take place async
 * on the next parsing run.
 */
static void 	kill_process (int kill_index, int sig)
{
	pid_t pgid;

	if (!process_list || kill_index > process_list_size || 
			!process_list[kill_index])
	{
		say("There is no such process %d", kill_index);
		return;
	}

	say("Sending signal %s (%d) to process %d: %s", 
		sys_siglist[sig], sig, kill_index, 
		process_list[kill_index]->name);

#ifdef HAVE_GETPGID
	pgid = getpgid(process_list[kill_index]->pid);
#else
#  ifndef GETPGRP_VOID
	pgid = getpgrp(process_list[kill_index]->pid);
#  else
	pgid = process_list[kill_index]->pid;
#  endif
#endif

#ifndef HAVE_KILLPG
# define killpg(pg, sig) kill(-(pg), (sig))
#endif

	/* The exec'd process shouldn't be in our process group */
	if (pgid == getpid())
	{
		yell("--- exec'd process is in my job control session!  Something is hosed ---");
		return;
	}

	killpg(pgid, sig);
	kill(process_list[kill_index]->pid, sig);
}


/*
 * This kills (sends a signal, *NOT* ``make it stop running'') all of the
 * currently running subprocesses with the given signal.  Presumably this
 * is because you want them to die.
 *
 * Remember that UNIX signals are asynchronous.  At best, you should assume
 * that they have an advisory effect.  You can tell a process that it should
 * die, but you cannot tell it *when* it will die -- that is up to the system.
 * That means that it is pointless to assume the condition of any of the 
 * kill()ed processes after the kill().  They may indeed be dead, or they may
 * be ``sleeping but runnable'', or they might even be waiting for a hardware
 * condition (such as a swap in).  You do not know when the process will
 * actually die.  It could be 15 ns, it could be 15 minutes, it could be
 * 15 years.  Its also useful to note that we, as the parent process, will not
 * recieve the SIGCHLD signal until after the child dies.  That means it is
 * pointless to try to reap any children processes here.  The main io()
 * loop handles reaping children (by calling get_child_exit()).
 */
static void 	kill_all_processes (int signo)
{
	int	i;
	int	tmp;

	tmp = window_display;
	window_display = 0;
	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i])
		{
			ignore_process(i);
			kill_process(i, signo);
		}
	}
	window_display = tmp;
}




/* * * * * * logical stuff * * * * * * */
/*
 * valid_process_index: checks to see if index refers to a valid running
 * process and returns true if this is the case.  Returns false otherwise 
 */
static int 	valid_process_index (int process)
{
	if ((process < 0) || (process >= process_list_size) || 
			!process_list[process])
	{
		say("No such process number %d", process);
		return (0);
	}

	return (1);
}

static int 	is_logical_unique (char *logical)
{
	Process	*proc;
	int	i;

	if (!logical)
		return 1;

	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]) || !proc->logical)
			continue;

		if (!my_stricmp(proc->logical, logical))
			return 0;
	}

	return 1;
}


/*
 * logical_to_index: converts a logical process name to it's approriate index
 * in the process list, or -1 if not found 
 */
static	int 	logical_to_index (const char *logical)
{
	Process	*proc;
	int	i;

	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]) || !proc->logical)
			continue;

		if (!my_stricmp(proc->logical, logical))
			return i;
	}

	return -1;
}

/*
 * get_process_index: parses out a process index or logical name from the
 * given string 
 */
int 		get_process_index (char **args)
{
	char	*s = next_arg(*args, args);
	return is_valid_process(s);
}

/*
 * is_valid_process: tells me if the spec is a process that is either
 * running or still has not closed its pipes, or both.
 */
int		is_valid_process (const char *arg)
{
	if (!arg || *arg != '%')
		return -1;

	arg++;
	if (is_number(arg) && valid_process_index(my_atol(arg)))
		return my_atol(arg);
	else
		return logical_to_index(arg);

	return -1;
}

int		process_is_running (char *arg)
{
	int idx = is_valid_process(arg);

	if (idx == -1)
		return 0;
	else
		return 1;
}
