/* File:      system_xsb.h
** Author(s): kifer
** Contact:   xsb-contact@cs.sunysb.edu
** 
** Copyright (C) The Research Foundation of SUNY, 1999
** 
** XSB is free software; you can redistribute it and/or modify it under the
** terms of the GNU Library General Public License as published by the Free
** Software Foundation; either version 2 of the License, or (at your option)
** any later version.
** 
** XSB is distributed in the hope that it will be useful, but WITHOUT ANY
** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
** more details.
** 
** You should have received a copy of the GNU Library General Public License
** along with XSB; if not, write to the Free Software Foundation,
** Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: system_xsb.h,v 1.5 2000-06-28 06:40:43 kifer Exp $
** 
*/


#ifndef fileno                          /* fileno may be a  macro */
extern int    fileno(FILE *f);          /* this is defined in POSIX */
#endif

/* In WIN_NT, this gets redefined into _fdopen by wind2unix.h */
extern FILE *fdopen(int fildes, const char *type);

#ifndef WIN_NT
extern int kill(pid_t pid, int sig);
#endif

#ifdef WIN_NT
#define PIPE(filedes_array)  _pipe(filedes_array, 5*MAXBUFSIZE, _O_TEXT)
#define WAIT(pid, status)    _cwait(&status, pid, 0)
#define KILL_FAILED(pid)     !TerminateProcess((HANDLE) pid,-1) /* -1=retval */
#else
#define PIPE(filedes_array)  pipe(filedes_array)
#define WAIT(pid, status)    waitpid(pid, &status, 0)
#define KILL_FAILED(pid)     kill(pid, SIGKILL) < 0
#endif

#define FREE_PROC_TABLE_CELL(pid)   ((pid < 0) \
				     || ((process_status(pid) != RUNNING) \
					 && (process_status(pid) != STOPPED)))

/* return codes from xsb_spawn */
#define  PIPE_TO_PROC_FAILED	-1
#define  PIPE_FROM_PROC_FAILED	-2
#define  SUB_PROC_FAILED	-3

#define MAX_SUBPROC_PARAMS 30  /* max # of cmdline params in a subprocess */

#define MAX_SUBPROC_NUMBER 20  /* max number of subrocesses allowed       */


#define RUNNING	       1
#define STOPPED	       2
#define EXITED	       3
#define ABORTED	       4
#define INVALID	       5
#define UNKNOWN	       6
