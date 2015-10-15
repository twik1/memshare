/* Tlog, syslog wrapper with IPC control.                                          */
/* Copyright (C) 2012  Tommy Wiklund                                               */
/* This file is part of Tlog.                                                      */
/*                                                                                 */
/* Tlog is free software: you can redistribute it and/or modify                    */
/* it under the terms of the GNU Lesser General Public License as published by     */
/* the Free Software Foundation, either version 3 of the License, or               */
/* (at your option) any later version.                                             */
/*                                                                                 */
/* Tlog is distributed in the hope that it will be useful,                         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of                  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                   */
/* GNU Lesser General Public License for more details.                             */
/*                                                                                 */
/* You should have received a copy of the GNU Lesser General Public License        */
/* along with Tlog.  If not, see <http://www.gnu.org/licenses/>.                   */

#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "memshare_api.h"
#include "tlog_api.h"

#define SHMEMSIZE 50
#define QUEUESIZE 512

static int t_init = 0;
static int t_mask = 31;
/* Setting all bits up to LOG_WARNING as default */

static char tproc[PROC_NAME_SIZE];
/* we protect the t_mask variable */
static pthread_mutex_t mask_mutex = PTHREAD_MUTEX_INITIALIZER;

static void signal2_callback(char *proc, int value1, int value2)
{
	switch (value1) {
	case 1:
		if ((value2 < 0) || (value2 > 8))
			return;
		/* Set a bit in the mask */
		pthread_mutex_lock(&mask_mutex);
		t_mask |= (1 << value2);
		pthread_mutex_unlock(&mask_mutex);
		break;

	case 2:
		if ((value2 < 0) || (value2 > 8))
			return;
		/* Del a bit in the mask */
		pthread_mutex_lock(&mask_mutex);
		t_mask &= ~(1 << value2);
		pthread_mutex_unlock(&mask_mutex);
		break;

	case 3:
		if ((value2 < 0) || (value2 > 255))
			return;
		pthread_mutex_lock(&mask_mutex);
		t_mask = value2;
		pthread_mutex_unlock(&mask_mutex);
		break;

	default:
		return;
		break;
	}

	return;
}

int tsyslog_set(int value)
{
	if (!t_init)
		return 1;
	signal2_callback("", 1, value);
	return 0;
}

int tsyslog_del(int value)
{
	if (!t_init)
		return 1;
	signal2_callback("", 2, value);
	return 0;
}

int tsyslog_replace(int value)
{
	if (!t_init)
		return 1;
	signal2_callback("", 3, value);
	return 0;
}

void tsyslog(int priority, const char *fmt, ...)
{
	va_list ap;

	if (!t_init)
		return;		/* Not initialized */
	/* check mask */
	if (t_mask & (1 << priority)) {
		va_start(ap, fmt);
		vsyslog(priority, fmt, ap);
		va_end(ap);
	}
}

int tsyslog_prio_init(char *name, int priomask)
{
	int retvalue;
	if (t_init)
		return 3;
	if ((priomask < 0) || (priomask > 255))
		return 4;
	signal2_callback("", 3, priomask);
	if ((retvalue = tsyslog_init(name)) != 0)
		return retvalue;
	return 0;
}

int tsyslog_init(char *name)
{
	int oldvalue = 0;
	int retvalue = 0;
	if (t_init) {
		syslog(LOG_INFO,
		       "tsyslog_init: Already registered in this process (%d) as %s. You wanted %s.",
		       getpid(), tproc, name);
		return 3;
	}
	if (name == NULL)
		return 1;
	strncpy(tproc, name, (PROC_NAME_SIZE - 1));

	/* let memshare use tsyslog as well */
	logfunction_register(tsyslog);

	openlog(tproc, LOG_NDELAY | LOG_CONS, LOG_LOCAL0);

	oldvalue = t_mask;	/* Store old value, whatever it is */
	t_mask = 64;		/* Setting bit 6, LOG_INFO to allow info printout */
	tsyslog(LOG_INFO, "Initializing tsyslog\n");
	t_mask = oldvalue;	/* Set back old value */

	/* we don't need much space */
	if ((retvalue = init_memshare(tproc, SHMEMSIZE, 512)) != 0) {
		syslog(LOG_ERR, "tsyslog_init: init_memshare returned %d",
		       retvalue);
		return 2;
	}

	/* register the callback */
	signal2_register(signal2_callback);

	t_init = 1;
	return 0;
}
