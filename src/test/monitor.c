/* Memshare, quick and easy IPC.                                                   */
/* Copyright (C) 2012  Tommy Wiklund                                               */
/* This file is part of Memshare.                                                  */
/*                                                                                 */
/* Memshare is free software: you can redistribute it and/or modify                */
/* it under the terms of the GNU Lesser General Public License as published by     */
/* the Free Software Foundation, either version 3 of the License, or               */
/* (at your option) any later version.                                             */
/*                                                                                 */
/* Memshare is distributed in the hope that it will be useful,                     */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of                  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                   */
/* GNU Lesser General Public License for more details.                             */
/*                                                                                 */
/* You should have received a copy of the GNU Lesser General Public License        */
/* along with Memshare.  If not, see <http://www.gnu.org/licenses/>.               */

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <string.h>
#include <memshare_api.h>

#define THREADSIZE  25
#define PROCNUMBERS 19

typedef struct {
	int status;
	int result;
	int index;
	int number;
	pid_t pid;
	pthread_t threads;
	char procname[50];
} procdata_t;
#define SIZEOF_PROCDATA sizeof(procdata)

static void close_procnumber(int);

static procdata_t procdata[THREADSIZE];
int procnumber[PROCNUMBERS];

int global1;
int global2;
int global3;
char globalmsg[512];

char *argv[3];

void *waiting(void *i)
{
	int status, retval;
	procdata_t *procd = (procdata_t *) i;
	print(LOG_INFO, "I'm waiting for pid %d\n", procd->pid);
	waitpid(procd->pid, &status, 0);
	retval = WEXITSTATUS(status);
	procd->pid = 0;
	procd->status = 0;
	close_procnumber(procd->number);
	if (retval != 0 && procd->result == 0) {
		print(LOG_DEBUG,
		      "Proc %s at index %d has terminated with status %d\n",
		      procd->procname, procd->index, retval);
		exit(retval);
	}
	print(LOG_DEBUG, "Proc %s terminated normally\n", procd->procname);
	return (void *)0;
}

/* Callbacks */
void data_callback(char *proc, char *msg, int len)
{
	if (len < 512) {
		strncpy(globalmsg, msg, 511);
		print(LOG_INFO, "Received %s from %s\n", msg, proc);
	} else {
		print(LOG_ERR, "Received to long string in data\n");
		exit(1);
	}
}

void signal1_callback(char *proc, int value)
{
	print(LOG_INFO, "Received %d from %s\n", value, proc);
	global1 = value;
}

void signal2_callback(char *proc, int value1, int value2)
{
	print(LOG_INFO, "Received %d, %d from %s\n", value1, value2, proc);
	global1 = value1;
	global2 = value2;
}

void signal3_callback(char *proc, int value1, int value2, int value3)
{
	print(LOG_INFO, "Received %d,%d,%d from %s\n", value1, value2, value3,
	      proc);
	global1 = value1;
	global2 = value2;
	global3 = value3;
}

/*            */

static int start_thread(procdata_t * procd)
{
	pthread_attr_t tattr;

	if (pthread_attr_init(&tattr) != 0) {
		print(LOG_ERR, "Unable to init thread attribute\n");
		return 1;
	}
	if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED) != 0) {
		print(LOG_ERR, "Unable to set detached state to thread\n");
		return 1;
	}
	if (pthread_attr_setinheritsched(&tattr, PTHREAD_INHERIT_SCHED) != 0) {
		print(LOG_ERR, "Unable to set inherit scheduling\n");
		return 1;
	}
	if (pthread_create(&procd->threads, &tattr, waiting, (void *)procd) !=
	    0) {
		print(LOG_ERR, "Unable to create rec thread\n");
		return 1;
	}

	return 0;
}

int get_free_thread_space()
{
	int i;
	for (i = 0; i < THREADSIZE; i++) {
		if (procdata[i].status == 0)
			return i;
	}
	return -1;
}

/* Procnumber functions */
int get_first_idle_number()
{
	int i;
	for (i = 0; i < PROCNUMBERS; i++) {
		if (procnumber[i] == 0) {
			procnumber[i] = 1;
			return i;
		}
	}
	return -1;
}

static void close_procnumber(int i)
{
	procnumber[i] = 0;
}

void clear_procnumber()
{
	int i;
	for (i = 0; i < PROCNUMBERS; i++)
		procnumber[i] = 0;
}

/*               */

void clear_thread_space()
{
	int i;
	for (i = 0; i < THREADSIZE; i++) {
		procdata[i].status = 0;
		procdata[i].pid = 0;
		procdata[i].index = 0;
		procdata[i].result = 0;
	}
}

int spawn(char *cmd, char *const argv[], int number)
{
	int index, ret;

	if (access(cmd, X_OK) == -1) {
		return -1;
	}

	index = get_free_thread_space();
	if (index < 0) {
		print(LOG_ERR, "Unable to find a free index\n");
		return -1;
	}

	pid_t child_pid = fork();
	if (child_pid >= 0) {
		if (child_pid == 0) {
			// Child ecex my process
			ret = execv(cmd, argv);
			if (ret == -1)
				print(LOG_ERR,
				      "Error: %s, trying to execute %s\n",
				      strerror(errno), cmd);
			sleep(1);
			exit(0);
		} else {
			print(LOG_INFO,
			      "Index %d is free, locking with pid %d\n", index,
			      child_pid);
			procdata[index].pid = child_pid;
			procdata[index].status = 1;
			procdata[index].number = number;
			procdata[index].index = index;
			strncpy(procdata[index].procname, argv[1], 50);
			start_thread(&procdata[index]);
		}
		return 0;
	} else {
		print(LOG_ERR, "Unable to fork\n");
		return -1;
	}
}

/* terminate pid                                 */
/* terminates the process with pid or if         */
/* pid = 0 kill all processes started with monit */

int terminate_pid(pid_t pid)
{
	print(LOG_INFO, "Terminating pid %d\n", pid);
	int i, flag = 0;
	for (i = 0; i < THREADSIZE; i++) {
		if (pid == 0) {
			if (procdata[i].pid && procdata[i].status) {
				kill(procdata[i].pid, SIGTERM);
				flag = 1;
			}
		} else {
			if (procdata[i].pid == pid) {
				kill(procdata[i].pid, SIGTERM);
				return pid;
			}
		}
	}
	if (flag)
		return 0;
	else
		return -1;
}

int getrand(int min, int max)
{
	return (rand() % (max - min) + min);
}

/* test api */

int test1(int index)
{
	int val, retval;
	val = getrand(0, 1000);
	print(LOG_NOTICE, "Test1 sending %d to %s\n", val,
	      procdata[index].procname);
	if ((retval = signal1(procdata[index].procname, val)) != 0) {
		print(LOG_ERR, "Test1 failed: signal1 to %s\n", procdata[index].procname);
		exit(2);
	}
	sleep(1);
	if (global1 != val) {
		print(LOG_ERR, "Test1 failed: %d expected but %d found\n", val,
		      global1);
		exit(2);
	}
	return 0;
}

int test2(int index)
{
	int val1, val2, retval;
	val1 = getrand(0, 1000);
	val2 = getrand(0, 1000);
	print(LOG_NOTICE, "Test2 sending %d,%d to %s\n", val1, val2,
	      procdata[index].procname);
	if ((retval = signal2(procdata[index].procname, val1, val2)) != 0) {
		print(LOG_ERR, "Test2 failed: signal2 to %s\n", procdata[index].procname);
		exit(2);
	}
	sleep(1);
	if ((global1 != val1) || (global2 != val2)) {
		print(LOG_ERR, "Test2 failed: %d,%d expected but %d,%d found\n",
		      val1, val2, global1, global2);
		exit(2);
	}
	return 0;
}

int test3(int index)
{
	int val1, val2, val3, retval;
	val1 = getrand(0, 1000);
	val2 = getrand(0, 1000);
	val3 = getrand(0, 1000);
	print(LOG_NOTICE, "Test3 sending %d,%d,%d to %s\n", val1, val2, val3,
	      procdata[index].procname);
	if ((retval = signal3(procdata[index].procname, val1, val2, val3)) != 0) {
		print(LOG_ERR, "Test3 failed: signal3 to %s\n", procdata[index].procname);
		exit(2);
	}
	sleep(1);
	if ((global1 != val1) || (global2 != val2) || global3 != val3) {
		print(LOG_ERR,
		      "Test3 failed: %d,%d,%d expected but %d,%d,%d found\n",
		      val1, val2, val3, global1, global2, global3);
		exit(2);
	}
	return 0;
}

int test4(int index)
{
	int val1, val2, retval;
	val1 = getrand(35, 122);
	val2 = getrand(35, 122);
	char msg[40] = "111112222233333444445555566666777778888\0";
	msg[0] = val1;
	msg[38] = val2;
	print(LOG_NOTICE, "Test4 sending %s to %s\n", msg,
	      procdata[index].procname);
	if ((retval = data(procdata[index].procname, msg, 40)) != 0) {
		print(LOG_ERR, "Test4 failed: data to %s\n", procdata[index].procname);
		exit(2);
	}
	sleep(1);
	if (strncmp(msg, globalmsg, 40)) {
		print(LOG_ERR, "Test4 failed: %s expected but %s found\n", msg,
		      globalmsg);
		exit(2);
	}
	return 0;
}

int test5(int index)
{
	print(LOG_NOTICE, "Test5 terminating %s\n", procdata[index].procname);
	terminate_pid(procdata[index].pid);
	return 0;
}

int start_proc(int number)
{
	int ret;
	char procname[51];
	if ((number = get_first_idle_number()) == -1) {
		print(LOG_ERR, "Start proc failed\n");
		exit(1);
	}
	snprintf(procname, 50, "memshare-test-%d", number);
	/*strncpy(procdata[index].procname, procname, 50); */
	strncpy(argv[0], "./replyer", 255);
	strncpy(argv[1], procname, 255);
	strncpy(argv[2], "\0", 255);
	ret = spawn(argv[0], argv, number);
	if (ret == -1) {
		print(LOG_ERR, "Error: %s, trying to execute %s\n",
		      strerror(errno), argv[0]);
		exit(1);
	}
	return 0;
}

int main()
{
	int i, action;
	time_t t;

	srand((unsigned)time(&t));

	print(LOG_NOTICE, "Starting memshare monitor test !\n");
	
	/* Preparations */
	argv[0] = malloc(256);
	argv[1] = malloc(256);
	argv[2] = malloc(256);

	clear_thread_space();

	init_memshare("basetest", 1024, 50);
	set_print_level(5); /* NOTICE level */
	data_register(data_callback);
	signal1_register(signal1_callback);
	signal2_register(signal2_callback);
	signal3_register(signal3_callback);

	/* test one start all the processes instantly */
	for (i = 0; i < PROCNUMBERS; i++) {
		start_proc(i);
	}
	sleep(2);
	/*start_proc(PROCNUMBERS); */
	while (1) {
		/* which index to operate on */
		i = getrand(0, PROCNUMBERS);
		if (procdata[i].status == 0) {
			start_proc(i);
			sleep(3);
		} else {
			action = getrand(1, 6);
			switch (action) {
			case 1:
				test1(i);
				break;
			case 2:
				test2(i);
				break;
			case 3:
				test3(i);
				break;
			case 4:
				test4(i);
				break;
			case 5:
				test5(i);
				break;
			default:
				print(LOG_ERR, "Action not defined %d\n",
				      action);
				exit(1);
			}
			sleep(2);
		}
		print(LOG_DEBUG, "Yet another testround\n");
	}
	/* terminate_pid(procdata[0].pid); */
	sleep(1);
	terminate_pid(0);
	sleep(5);
	return 0;
}
