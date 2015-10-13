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

#include "tlog_api.h"
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

void printemall(void) {
	tsyslog(LOG_DEBUG, "Detta är en debug utskrift\n");
	tsyslog(LOG_INFO, "Detta är en info utskrift\n");
	tsyslog(LOG_NOTICE, "Detta är en notice utskrift\n");
	tsyslog(LOG_WARNING, "Detta är en warning utskrift\n");
	tsyslog(LOG_ERR, "Detta är en error utskrift\n");
	tsyslog(LOG_CRIT, "Detta är en critical utskrift\n");
	tsyslog(LOG_ALERT, "Detta är en alert utskrift\n");
	tsyslog(LOG_EMERG, "Detta är en emergency utskrift\n");
}	

int main(int argc, char *argv[])
{
	int retvalue;
	printemall(); /* Nothing will show up since tsyslog isn't initialized */

	/* You could either initalize like this, together with a prio */
	if ((retvalue = tsyslog_prio_init("logtest", 3)) != 0) {
		printf("Unable to initalize tsyslog %d\n", retvalue);
		exit(1);
	}

/*     or like this 
 *	if ((retvalue = tsyslog_init("logtest")) != 0) {
 *		printf("Unable to initialize tsyslog %d\n", retvalue);
 *		exit(1);
 *	}
 *      if you don't set a priority it will default to:
 *      LOG_EMERG | LOG_ALERT | LOG_CRIT | LOG_ERR | LOG_WARNING
 * 
 *	if (retvalue = tsyslog_replace(3) != 0) {
 *		printf("Unable to replace priority %d\n", retvalue);
 *		exit(1);
 *	}
 */

	printemall(); /* This will print emergancy och alert logs */

	/* Add also the critical level logs */
	if ((retvalue = tsyslog_set(LOG_CRIT)) != 0) {
		printf("Unable to set priority %d\n", retvalue);
		exit(1);
	}
	
	printemall(); /* This will print emergancy, alert and critical logs */

	if ((retvalue = tsyslog_del(LOG_EMERG)) != 0) {
		printf("Unable to del priority %d\n", retvalue);
		exit(1);
	}
	
	printemall(); /* This will print alert and critical logs */
	
/* 	
 *  to change loglevel from outside the process use memsend
 *  as described in tlog_api.h
 */
	
	return(0);
}
