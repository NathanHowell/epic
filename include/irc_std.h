/*
 * irc_std.h: This is where we make up for operating system lossage
 * Originally written by Matthew Green, Copyright 1993
 * Various modifications by various people since then.
 *
 * See the copyright file, or do a help ircii copyright 
 */

#ifndef __irc_std_h
#define __irc_std_h

#include "defs.h"

/*
 * Try to turn back the IPv6 monster at the gate
 */
#ifdef DO_NOT_USE_IPV6
# undef INET6
#else
# define INET6
#endif

/*
 * Everybody needs these ANSI headers...
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

/*
 * Everybody needs these POSIX headers...
 */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/param.h>
#include <errno.h>
#include <sys/stat.h>

/*
 * Everybody needs these INET headers...
 */
#ifdef USE_SOCKS5
# include <socks.h>
#endif
# include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif


/*
 * Some systems define tputs, etc in this header
 */
#ifdef HAVE_TERMCAP_H
#include <termcap.h>
#endif


/*
 * Deal with brokenness in <time.h> and <sys/time.h>
 */
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/*
 * Deal with brokenness in <fcntl.h> and <sys/fcntl.h>
 */
#ifdef HAVE_SYS_FCNTL_H
# include <sys/fcntl.h>
#else
# ifdef HAVE_FCNTL_H
#  include <fcntl.h>
# endif
#endif

/*
 * Deal with brokenness figuring out struct direct
 */
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif




/*
 * First try to figure out if we can use GNU CC special features...
 */
#ifndef __GNUC__
# define __inline__		/* delete gcc keyword */
# define __inline
# define __A(x)
# define __N
#else
# if (__GNUC__ >= 2) && (__GNUC_MINOR__ >= 7)
#  define __A(x) __attribute__ ((format (printf, x, x + 1)))
#  define __N    __attribute__ ((noreturn))
# else
#  define __A(x)
#  define __N
# endif
#endif

/*
 * Figure out how to make alloca work
 * I took this from the autoconf documentation
 */
#if defined(__GNUC__) && !defined(HAVE_ALLOCA_H)
# define alloca __builtin_alloca
#else
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca
char *alloca();
#   endif
#  endif
# endif
#endif

/*
 * Define the MIN and MAX macros if they don't already exist.
 */
#ifndef MIN
# define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b))?(a):(b))
#endif


/*
 * Deal with brokenness with sys_errlist.
 */
#ifndef HAVE_STRERROR
# ifndef SYS_ERRLIST_DECLARED
extern	char	*sys_errlist[];
# endif
#define strerror(x) sys_errlist[x]
#endif

/*
 * Deal with brokenness with realpath.
 */
#ifdef HAVE_BROKEN_REALPATH
# define realpath my_realpath
#endif

/*
 * Dont trust anyone else's NULLs.
 */
#ifdef NULL
#undef NULL
#endif
#define NULL (void *) 0

/*
 * Make sure there is TRUE and FALSE
 */
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/*
 * Can you believe some systems done #define this?
 * I was told that hurd doesn't, so this helps us on hurd.
 */
#ifndef MAXPATHLEN
# ifndef PATHSIZE
#  define MAXPATHLEN 1024
# else
#  define MAXPATHLEN  PATHSIZE
# endif
#endif

/*
 * Define generic macros for signal handlers and built in commands.
 */
typedef RETSIGTYPE sigfunc (int);
int	block_signal (int);
int	unblock_signal (int);
sigfunc *my_signal (int, sigfunc *);
#define SIGNAL_HANDLER(x) \
	RETSIGTYPE x (int unused)

#define BUILT_IN_COMMAND(x) \
	void x (const char *command, char *args, const char *subargs)

typedef char Filename[MAXPATHLEN + 1];


/*
 * Deal with our brokenness wrt ANSI.  Sigh.
 */
#ifndef HAVE_MEMMOVE
#define memmove(x, y, z) bcopy(y, x, z)
#endif

/*
 * DCC specification requires exactly a 32 bit checksum.
 * Kind of lame, actually.
 */
#ifdef UNSIGNED_LONG32
  typedef		unsigned long		u_32int_t;
#else
# ifdef UNSIGNED_INT32
  typedef		unsigned int		u_32int_t;
# else
  typedef		unsigned long		u_32int_t;
# endif
#endif

/*
 * Some systems (AIX) have sys/select.h, but dont include it from sys/types.h
 * Some systems (Solaris) have sys/select.h, but include it from sys/types.h
 * and dont want you to do it again.  Some systems dont have sys/select.h
 * Configure has this all figured out for us already.
 */
#if defined(HAVE_SYS_SELECT_H) && defined(NEED_SYS_SELECT_H)
#include <sys/select.h>
#endif

/*
 * Now we deal with lame systems that dont have correct select()
 * support (like aix 3.2.5, and older linux systems.)
 */
#ifndef NBBY
# define NBBY	8		/* number of bits in a byte */
#endif /* NBBY */

#ifndef NFDBITS
# define NFDBITS	(sizeof(long) * NBBY)	/* bits per mask */
#endif /* NFDBITS */

#ifndef FD_SETSIZE
#define FD_SETSIZE      256
#endif

#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif

#if defined(HAVE_SYS_SYSCTL_H)
#include <sys/sysctl.h>
#endif

/*
 * Define an RFC2553 compatable "struct sockaddr_storage" if we do not
 * already have one.
 */
#ifndef HAVE_STRUCT_SOCKADDR_STORAGE
struct sockaddr_storage {
#ifdef HAVE_SA_LEN
	u_char ss_len;
	u_char ss_family;
#else
	u_short ss_family;
#endif
	u_char padding[128 - 2];
};
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#ifndef HAVE_STRUCT_SOCKADDR_IN6
#undef INET6
#endif

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO) || !defined(HAVE_STRUCT_ADDRINFO)
# define NEED_GAILIB
# undef INET6
# ifndef HAVE_GETADDRINFO
#  define getaddrinfo getaddrinfo__compat
#  define freeaddrinfo freeaddrinfo__compat
#  define gai_strerror gai_strerror__compat
   struct addrinfo__compat {
        int     ai_flags;       /* AI_PASSIVE, AI_CANONNAME */
        int     ai_family;      /* PF_xxx */
        int     ai_socktype;    /* SOCK_xxx */
        int     ai_protocol;    /* 0 or IPPROTO_xxx for IPv4 and IPv6 */
        size_t  ai_addrlen;     /* length of ai_addr */
        char    *ai_canonname;  /* canonical name for hostname */
        struct sockaddr *ai_addr;       /* binary address */
        struct addrinfo__compat *ai_next;    /* next structure in linked list */
   };
#  define addrinfo addrinfo__compat
# endif
# ifndef HAVE_GETNAMEINFO
#  define getnameinfo getnameinfo__compat
# endif
# include "gailib.h"
#endif


/*
 * Define some lazy shorthand typedefs for commonly used structures
 */
typedef struct sockaddr 	SA;
typedef struct sockaddr_storage	SS;
typedef struct sockaddr_in 	ISA;
typedef struct in_addr		IA;

#ifdef INET6
typedef struct sockaddr_in6	ISA6;
typedef struct sockaddr_in6	I6SA;
typedef struct in6_addr		IA6;
typedef struct in6_addr		I6A;
#endif

typedef struct addrinfo		AI;
typedef struct hostent		Hostent;
typedef struct timeval		Timeval;
typedef struct stat		Stat;

#endif /* __irc_std_h */
