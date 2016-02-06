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

#include "memshare.h"
#include "memshare_api.h"
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include "queue.h"

static pthread_t recthread1_t, recthread2_t;

union semun {
	int val;
	struct semid_ds *buf;
	ushort *array;
};

/* global pointer to the ctrl area, initialized by get_shm() */
char *shm_ctrl_ptr = NULL;
/* global mutex protecting the ctrl area */
int lock_ctrl_sem = 0;
/* internal memory view of the ctrl area */
mem_proc_entry mem_entry[NUMBER_OF_PROCS];

/* initialized flag */
int initialized = 0, send_only = 1;

/* My own process and index */
char my_proc[PROC_NAME_SIZE];
int my_index;

int queue_index, sequence = 0;

static int create_lock(int key, int value);
static int lock(int sem);
static int unlock(int sem);
static int set_active(int sem);
static int try_lock1(int sem);

static void init_mem_proc(void);
static int clear_shm(int key, int size);
static char *get_shm(int key, int size, int *mode);

static int free_index(int index);
static int inc_sent(void);
static int inc_received(void);
static int destroy_lock(int);
static int update_cache(int, proc_entry *);

callback_1 callback1 = NULL;
callback_2 callback2 = NULL;
callback_3 callback3 = NULL;
callback_data callbackdata = NULL;
callback_extlog callbackextlog = NULL;

int current_level = LOG_WARNING;

/* print functions either printf or user specific */
/*static int print(int level, const char *format, ...)*/
int print(int level, const char *format, ...)
{
	va_list ap;
	int retval = 0;

	va_start(ap, format);
	if (callbackextlog) {
		callbackextlog(level, format, ap);
	} else {
		if (level <= current_level) {
			retval = vprintf(format, ap);
		}
	}
	va_end(ap);
	return retval;
}

/* Consumer thread, take from queue and execute user callback */
static void *recthread2(void *arg)
{
	header *hdr;
	signal *sig;
	char *msg;

	for (;;) {
		msg = qget(queue_index);
		hdr = (header *) msg;
		print(LOG_INFO, "Taking msg from queue with msg_type %d\n",
		      hdr->msg_type);
		switch (hdr->msg_type) {
		case DATA:
			if (callbackdata != NULL) {
				callbackdata(hdr->proc_name,
					     (msg + SIZEOF_HEADER),
					     hdr->msg_len);
				free(msg);
			} else {
				print(LOG_WARNING,
				      "No callback for msg_type %d\n", DATA);
			}
			break;

		case SIGNAL1:
			sig = (signal *) (msg + SIZEOF_HEADER);
			if (callback1 != NULL) {
				callback1(hdr->proc_name, sig->signal1);
				free(msg);
			} else {
				print(LOG_WARNING,
				      "No callback for msg_type %d\n", SIGNAL1);
			}
			break;

		case SIGNAL2:
			sig = (signal *) (msg + SIZEOF_HEADER);
			if (callback2 != NULL) {
				callback2(hdr->proc_name, sig->signal1,
					  sig->signal2);
				free(msg);
			} else {
				print(LOG_WARNING,
				      "No callback for msg_type %d\n", SIGNAL2);
			}
			break;

		case SIGNAL3:
			sig = (signal *) (msg + SIZEOF_HEADER);
			if (callback3 != NULL) {
				callback3(hdr->proc_name, sig->signal1,
					  sig->signal2, sig->signal3);
				free(msg);
			} else {
				print(LOG_WARNING,
				      "No callback for msg_type %d\n", SIGNAL3);
			}
			break;

		default:
			print(LOG_ERR, "Illeagal msg_type %d\n", hdr->msg_type);
			free(msg);
			break;
		}
	}
	return (void *)0;
}

static void *recthread1(void *arg)
{
	header *hdr;
	char *msg;

	for (;;) {
		/* Add check for prio, TODO */
		print(LOG_INFO, "Recthread1 going to lock\n");
		while (lock(mem_entry[my_index].rlock) < 0) ;
		print(LOG_DEBUG, "Entry inserted in shm for my process %s\n",
		      my_proc);
		hdr = (header *) mem_entry[my_index].shm;
		/* check return of allocation TODO */
		msg = malloc(hdr->msg_len + SIZEOF_HEADER);
		memcpy(msg, mem_entry[my_index].shm,
		       (hdr->msg_len + SIZEOF_HEADER));
		if (lo_qadd(queue_index, &msg)) {
			/* Failed to put in queue, msg lost */
			print(LOG_ERR,
			      "Failed to put msg in queue, msg with seq nr %d is lost!\n",
			      hdr->seq);
			free(msg);
		} else {
			/* msg concidered received */
			inc_received();
		}
		while (unlock(mem_entry[my_index].wlock) < 0) ;
	}
	return (void *)0;
}

/************* New procs to be used *****************/
static proc_entry *get_proc_at_index(int index)
{
	char *tmp_ptr;
	proc_entry *entry;
	tmp_ptr = shm_ctrl_ptr + (SIZEOF_PROC_ENTRY * index);
	entry = (proc_entry *) tmp_ptr;
	return entry;
}

int check_proc_at_index(int index)
{
	proc_entry *entry = get_proc_at_index(index);
	int sem;

	/* Check if this entry has ever been seized or not */
	if (entry->key_active) {
		print(LOG_DEBUG, "key_active for index %d\n", index);
		/* get sem for the key */
		if ((sem = create_lock(entry->key_active, 0)) == -1) {
			print(LOG_ERR,
			      "Unable to create active lock in check_proc_at_index for key %d\n",
			      entry->key_active);
			return 0;
		}
		/* check if cache is updated with sem */
		if (mem_entry[index].active) {
			/* check if cache contains correct sem for the key */
			if (sem != mem_entry[index].active) {
				print(LOG_DEBUG,
				      "Cache and sem for key %d doesn't match, updating cache\n",
				      entry->key_active);
				update_cache(index, entry);
				/*mem_entry[index].active = sem; */
			}
		} else {
			print(LOG_DEBUG, "Cache empty update sem from key\n");
			/*mem_entry[index].active = sem; */
			update_cache(index, entry);
		}
		/* use sem in cache to check if active */
		if (try_lock1(mem_entry[index].active)) {
			if (!memcmp
			    (mem_entry[index].proc_name, entry->proc_name,
			     PROC_NAME_SIZE)) {
				print(LOG_DEBUG,
				      "Index %d is occupied by %s and active\n",
				      index, entry->proc_name);
			} else {
				print(LOG_DEBUG,
				      "Index %d is occupied by %s, cache is however outdated\n",
				      index, entry->proc_name);
				update_cache(index, entry);
			}
			return 1;
		} else {
			print(LOG_DEBUG,
			      "Index %d has been active but isn't any more, reset key_active\n",
			      index);
			free_index(index);
		}
	} else {
		print(LOG_DEBUG,
		      "Index %d has never been active (or released)\n", index);
	}
	return 0;
}

int get_index_for_proc(char *proc)
{
	int i;

	for (i = 0; i < NUMBER_OF_PROCS; i++) {
		if (check_proc_at_index(i)) {
			if (!strcmp(mem_entry[i].proc_name, proc)) {
				return i;
			}
		}
	}
	return -1;
}

static int get_first_free(void)
{
	int i;

	for (i = 0; i < NUMBER_OF_PROCS; i++) {
		if (!check_proc_at_index(i)) {
			return i;
		}
	}
	return -1;
}

static int update_cache(int index, proc_entry * entry)
{
	int mode = 0;
	print(LOG_INFO, "Updating cache for %s at index %d\n", entry->proc_name,
	      index);
	if ((mem_entry[index].rlock = create_lock(entry->key_rlock, 0)) == -1) {
		print(LOG_ERR, "Unable to create rlock\n");
		return -1;
	}
	print(LOG_INFO, "Cache rlock for %s at index %d, key %d, sem %d\n",
	      entry->proc_name, index, entry->key_rlock,
	      mem_entry[index].rlock);

	if ((mem_entry[index].wlock = create_lock(entry->key_wlock, 0)) == -1) {
		print(LOG_ERR, "Unable to create wlock\n");
		return -1;
	}
	print(LOG_INFO, "Cache wlock for %s at index %d, key %d, sem %d\n",
	      entry->proc_name, index, entry->key_wlock,
	      mem_entry[index].wlock);

	if ((mem_entry[index].shm =
	     (char *)get_shm(entry->key_shm, entry->size_shm, &mode)) == 0) {
		print(LOG_ERR, "Unable to map shmc\n");
		return -1;
	}
	print(LOG_INFO, "Cache shm for %s at index %d, key %d, sem &p\n",
	      entry->proc_name, index, entry->key_shm, mem_entry[index].shm);

	if ((mem_entry[index].active = create_lock(entry->key_active, 0)) == -1) {
		print(LOG_ERR, "Unable to create  active lock\n");
		return -1;
	}
	memcpy(mem_entry[index].proc_name, entry->proc_name, PROC_NAME_SIZE);
	return 0;
}

static int seize_index(int index, int size, char *proc)
{
	int mode = 0, key_base = 0;
	print(LOG_INFO, "Seizing index %d with %s\n", index, proc);
	proc_entry *entry = get_proc_at_index(index);
	/* setting up ctr area for my index */
	key_base = index * 4 + SEM_CTRL_KEY;
	entry->key_shm = key_base + 1;
	entry->key_rlock = key_base + 2;
	entry->key_wlock = key_base + 3;
	entry->key_active = key_base + 4;
	entry->size_shm = size;
	entry->sent = 0;
	entry->received = 0;
	my_index = index;

	/* map up the cache (mem_entry) */
	if ((mem_entry[index].active = create_lock(entry->key_active, 0)) == -1) {
		return -1;
	}
	/* signal active */
	if (set_active(mem_entry[index].active)) {
		return -1;
	}

	strncpy(entry->proc_name, proc, PROC_NAME_SIZE - 1);
	memcpy(mem_entry[index].proc_name, entry->proc_name, PROC_NAME_SIZE);

	if ((mem_entry[index].rlock = create_lock(entry->key_rlock, 0)) == -1) {
		return -1;
	}
	print(LOG_INFO, "Seize rlock for %s at index %d, key %d, sem %d\n",
	      entry->proc_name, index, entry->key_rlock,
	      mem_entry[index].rlock);

	if ((mem_entry[index].wlock = create_lock(entry->key_wlock, 1)) == -1) {
		return -1;
	}
	print(LOG_INFO, "Seize wlock for %s at index %d, key %d, sem %d\n",
	      entry->proc_name, index, entry->key_wlock,
	      mem_entry[index].wlock);

	if ((mem_entry[index].shm =
	     (char *)get_shm(entry->key_shm, entry->size_shm, &mode)) == 0) {
		return -1;
	}
	print(LOG_INFO, "Seize shm for %s at index %d, key %d, sem %p\n",
	      entry->proc_name, index, entry->key_shm, mem_entry[index].shm);

	return 0;
}

static int free_index(int index)
{
	proc_entry *entry;
	entry = (proc_entry *) get_proc_at_index(index);
	print(LOG_INFO, "Remove proc %s from index %d\n", entry->proc_name,
	      index);
	clear_shm(entry->key_shm, entry->size_shm);
	entry->key_shm = 0;
	entry->size_shm = 0;
	mem_entry[index].shm = 0;
	destroy_lock(entry->key_rlock);
	entry->key_rlock = 0;
	mem_entry[index].rlock = 0;
	destroy_lock(entry->key_wlock);
	entry->key_wlock = 0;
	mem_entry[index].wlock = 0;
	destroy_lock(entry->key_active);
	entry->key_active = 0;
	mem_entry[index].active = 0;
	entry->active = 0;
	return 0;
}

/********** ipc semaphore functions
            to be used as mutexes    ***********/
static int chase_semget_error(int err)
{
	int retvalue = -1;
	switch (errno) {
	case EACCES:
		print(LOG_ERR,
		      "A semaphore set exists for key, but the calling process does not\n"
		      "have  permission  to  access  the  set,  and  does  not have the\n"
		      "CAP_IPC_OWNER capability.\n");
		break;

	case EEXIST:
		/* Trying to open it exclusively failed, try normal */
		retvalue = 0;
		break;

	case EINVAL:
		print(LOG_ERR,
		      "nsems  is less than 0 or greater than the limit on the number of\n"
		      "semaphores per semaphore set (SEMMSL), or a semaphore set corre-\n"
		      "sponding  to  key  already  exists, and nsems is larger than the\n"
		      "number of semaphores in that set.\n");
		break;

	case ENOENT:
		print(LOG_ERR,
		      "No semaphore set exists for  key  and  semflg  did  not  specify\n"
		      "IPC_CREAT.\n");
		break;

	case ENOMEM:
		print(LOG_ERR,
		      "A  semaphore  set has to be created but the system does not have\n"
		      "enough memory for the new data structure.\n");
		break;

	case ENOSPC:
		print(LOG_ERR,
		      "A semaphore set has to be created but the system limit  for  the\n"
		      "maximum  number  of  semaphore sets (SEMMNI), or the system wide\n"
		      "maximum number of semaphores (SEMMNS), would be exceeded.\n");
		break;

	default:
		print(LOG_ERR, "Unknown semget() error (%m)\n");
		break;
	}
	return retvalue;
}

static int chase_semop_error(int err, int sem, int what)
{
	int retvalue = 0;
	switch (err) {
	case E2BIG:
		print(LOG_ERR,
		      "the argument nsops is greater than semopm, the maximum number of\n"
		      "operations allowed per system call.\n");
		break;

	case EACCES:
		print(LOG_ERR,
		      "the  calling  process  does not have the permissions required to\n"
		      "perform the specified semaphore operations, and  does  not  have\n"
		      "the CAP_IPC_OWNER capability.\n");
		break;

	case EAGAIN:
		/*print(LOG_ERR,
		   "An operation could not proceed immediately and either IPC_NOWAIT\n"
		   "was specified in sem_flg or the time limit specified in  timeout\n"
		   "expired.\n"); */
		retvalue = -1;
		break;

	case EFAULT:
		print(LOG_ERR,
		      "An  address specified in either the sops or the timeout argument\n"
		      "isn't accessible.\n");
		break;

	case EFBIG:
		print(LOG_ERR,
		      "For some operation the value  of  sem_num  is  less  than  0  or\n"
		      "greater than or equal to the number of semaphores in the set.\n");
		break;

	case EIDRM:
		print(LOG_ERR,
		      "The semaphore set was removed. sem %d, from %d\n", sem,
		      what);
		break;

	case EINTR:
		print(LOG_DEBUG,
		      "While  blocked in this system call, the process caught a signal;\n"
		      "see signal(7).\n");
		retvalue = -1;
		break;

	case EINVAL:
		print(LOG_ERR,
		      "The semaphore set doesn't exist, or semid is less than zero,  or\n"
		      "nsops has a non-positive value.\n");
		break;

	case ENOMEM:
		print(LOG_ERR,
		      "The  sem_flg of some operation specified SEM_UNDO and the system\n"
		      "does not have enough memory to allocate the undo structure.\n");
		break;

	case ERANGE:
		print(LOG_ERR,
		      "For some operation sem_op+semval is  greater  than  SEMVMX,  the\n"
		      "implementation dependent maximum value for semval.\n");
		break;

	default:
		print(LOG_ERR, "Unknown return from semop() (%m)\n");
		break;
	}
	return retvalue;
}

/* This function will try to create a semaphore for an index, exclusively    */
/* If it already has been created it will fail and we will open it normally  */
/* If the exclusive open succede we know we are the first process and we so  */
/* we will initialize it to work as a mutex                                  */
static int create_lock(int key, int value)
{
	/*struct sembuf op[1]; */
	union semun ctrl;
	int sem;

	print(LOG_INFO, "Create_lock key=%d, value=%d\n", key, value);
	if ((sem = semget(key, 1, IPC_EXCL | IPC_CREAT | 0666)) == -1) {
		if (!chase_semget_error(errno)) {	/* true if retvalue EEXIST */
			print(LOG_DEBUG, "Create_lock Key=%d already open\n",
			      key);
			if ((sem = semget(key, 1, IPC_CREAT | 0666)) == -1) {
				print(LOG_ERR,
				      "Unable to create semaphore (%m)\n");
				return -1;
			}
			return sem;
		}
		return -1;
	}
	ctrl.val = value;
	print(LOG_INFO, "Create_lock Init sem=%d to %d\n", sem, value);
	/* Its the first time for this key, init it to work as a mutex */
	if (semctl(sem, 0, SETVAL, ctrl) == -1) {
		print(LOG_ERR, "Unable to initialize semaphore (%m)\n");
		return -1;
	}
	return sem;
}

static int destroy_lock(int key)
{
	union semun ctrl;
	int sem;

	print(LOG_INFO, "Destroy_lock key=%d\n", key);
	if ((sem = semget(key, 1, IPC_CREAT | 0600)) == -1) {
		print(LOG_ERR, "Unable to create semaphore (%m)\n");
		return 1;
	}
	if (semctl(sem, 0, IPC_RMID, ctrl) == -1) {
		print(LOG_ERR, "Unable to remove semaphore (%m)\n");
		return 1;
	}
	return 0;
}

static int lock(int sem)
{
	int retvalue = 0;
	struct sembuf op[1];
	op[0].sem_num = 0;
	op[0].sem_op = -1;
	op[0].sem_flg = SEM_UNDO;
	if (semop(sem, op, 1)) {
		retvalue = chase_semop_error(errno, sem, 1);
		print(LOG_DEBUG, "semop release err %d\n", retvalue);
	}
	return retvalue;
}

static int unlock(int sem)
{
	int retvalue = 0;
	struct sembuf op[1];
	op[0].sem_num = 0;
	op[0].sem_op = 1;
	op[0].sem_flg = SEM_UNDO;
	if (semop(sem, op, 1))
		retvalue = chase_semop_error(errno, sem, 2);
	return retvalue;
}

static int set_active(int sem)
{
	int retvalue = 0;
	struct sembuf op[1];
	op[0].sem_num = 0;
	op[0].sem_op = 1;
	op[0].sem_flg = SEM_UNDO;
	if (semop(sem, op, 1))
		retvalue = chase_semop_error(errno, sem, 3);
	return retvalue;
}

/* Should be used when leaving gracefully */
/*static int clear_active(int sem)
{
    int retvalue = 0;
	struct sembuf op[1];
	op[0].sem_num = 0;
	op[0].sem_op = -1;
	op[0].sem_flg = 0;
	if (semop(sem, op, 1))
	 	retvalue = chase_semop_error(errno);
	return retvalue;
}*/

static int try_lock1(int sem)
{
	int retvalue = 0;
	struct sembuf op[1];
	op[0].sem_num = 0;
	op[0].sem_op = 0;
	op[0].sem_flg = IPC_NOWAIT;
	if (semop(sem, op, 1)) {
		if (chase_semop_error(errno, sem, 4)) {
			retvalue = 1;
		}
	}
	return retvalue;
}

/********** mem_proc functions **********/
static void init_mem_proc(void)
{
	int i;

	for (i = 0; i < NUMBER_OF_PROCS; i++) {
		mem_entry[i].shm = NULL;
		mem_entry[i].rlock = 0;
		mem_entry[i].wlock = 0;
		mem_entry[i].active = 0;
	}
}

static int start_listen_thread(void)
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

	if (pthread_create(&recthread1_t, &tattr, recthread1, (void *)NULL) !=
	    0) {
		print(LOG_ERR, "Unable to create worker thread1\n");
		return 1;
	}

	if (pthread_create(&recthread2_t, &tattr, recthread2, (void *)NULL) !=
	    0) {
		print(LOG_ERR, "Unable to create worker thread2\n");
		return 1;
	}
	return 0;
}

/********** the shared memory functions ***********/
static int clear_shm(int key, int size)
{
	int shmid;

	if ((shmid = shmget(key, size, IPC_CREAT | 0666)) < 0) {
		print(LOG_ERR, "Unable get shared mem for key\n", key);
		return 1;
	}
	shmctl(shmid, IPC_RMID, NULL);
	return 0;
}

/* This function will try to map shared memory for an index(key)  */
/* The return value is a pointer to the shared memory or NULL if  */
/* the function fail.                                             */
/* If mode is set to 1 the shared memory will be created if it    */
/* not has been done before.                                      */
/* With mode = 0 the function will fail if the index has been     */
/* created before                                                 */
static char *get_shm(int key, int size, int *mode)
{
	int shmid;
	char *data;

	print(LOG_INFO, "Map the shmc for key %d with size %d\n", key, size);
	if (*mode) {
		if ((shmid =
		     shmget(key, size, IPC_CREAT | IPC_EXCL | 0666)) < 0) {
			/* already open */
			*mode = 0;
		} else {
			print(LOG_DEBUG, "Creating shared mem for key%d\n",
			      key);
		}
	}
	if (*mode == 0) {
		if ((shmid = shmget(key, size, IPC_CREAT | 0666)) < 0) {
			print(LOG_ERR, "Unable get shared mem for key %d\n",
			      key);
			return NULL;
		}
	}

	if ((data = shmat(shmid, NULL, 0)) == (char *)-1) {
		print(LOG_ERR, "Unable to alloc shared mem for key %d\n", key);
		return NULL;
	}
	return data;
}

static int inc_sent(void)
{
	proc_entry *entry;

	/* The locks might not be needed TODO */
	/*lock(lock_ctrl_sem); */
	entry = (proc_entry *) get_proc_at_index(my_index);
	entry->sent++;
	/*unlock(lock_ctrl_sem); */
	return 0;
}

static int inc_received(void)
{
	proc_entry *entry;

	/* The locks might not be needed TODO */
	/*lock(lock_ctrl_sem); */
	entry = (proc_entry *) get_proc_at_index(my_index);
	entry->received++;
	/*unlock(lock_ctrl_sem); */
	return 0;
}

int send_ack(int seq)
{
	return 0;
}

/****************API***********************/
void logfunction_register(callback_extlog cbe)
{
	callbackextlog = cbe;
}

int set_print_level(int level)
{
	if ((level >= LOG_EMERG) && (level <= LOG_DEBUG)) {
		current_level = level;
		return 0;
	}
	return 1;
}

void data_register(callback_data cbd)
{
	callbackdata = cbd;
}

void signal1_register(callback_1 cb1)
{
	callback1 = cb1;
}

void signal2_register(callback_2 cb2)
{
	callback2 = cb2;
}

void signal3_register(callback_3 cb3)
{
	callback3 = cb3;
}

int init_memshare(char *proc_name, int size, int qsize)
{
	int ctrl_mode = 1;
	int retvalue = 0, index;
	print(LOG_INFO, "Init_memshare start for %s with size %d\n", proc_name,
	      size);

	if (initialized)
		return 1;
	/* a source proc is a must */
	if (proc_name == NULL)
		return 2;

	memset(my_proc, 0, PROC_NAME_SIZE);
	strncpy(my_proc, proc_name, PROC_NAME_SIZE - 1);

	/* If I don't set a qsize I'm considered to be a send proc only */
	if (size)
		send_only = 0;

	if (!send_only) {
		init_queues();
		seize_queue(&queue_index, "memshare", qsize);
	}

	/* clear the cache */
	init_mem_proc();

	/* start off by locking the ctrl lock */
	if ((lock_ctrl_sem = create_lock(SEM_CTRL_KEY, 1)) == -1) {
		print(LOG_ERR, "Unable to create ctrl lock\n");
		return 3;
	}

	while (lock(lock_ctrl_sem) < 0) ;
	print(LOG_DEBUG, "Ctrl locked (init) by %s, %d\n\n", proc_name,
	      lock_ctrl_sem);
	/*print(LOG_ERR, "%d trylock (init) key=%d, sem=%d\n",
	   try_lock1(lock_ctrl_sem), SEM_CTRL_KEY, lock_ctrl_sem); */

	/* map up the ctrl area */
	if ((shm_ctrl_ptr = get_shm(SHM_CTRL_KEY, CTRL_SIZE, &ctrl_mode)) == 0) {
		print(LOG_ERR, "Unable to alloc shared mem\n");
		while (unlock(lock_ctrl_sem) < 0) ;
		return 6;
	}

	if (get_index_for_proc(my_proc) != -1) {
		print(LOG_ERR, "Procname %s already exists\n", my_proc);
		while (unlock(lock_ctrl_sem) < 0) ;
		return 4;
	}

	if (!send_only) {

		if ((index = get_first_free()) < 0) {
			while (unlock(lock_ctrl_sem) < 0) ;
			print(LOG_ERR, "Max num of processes registered\n");
			return 4;
		}

		print(LOG_DEBUG, "Next free index is %d\n", index);

		retvalue = seize_index(index, size, my_proc);

		if (retvalue == -1) {
			while (unlock(lock_ctrl_sem) < 0) ;
			return 6;
		}
	} else {
		print(LOG_INFO, "%s is a send only proc\n", my_proc);
	}

	print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", proc_name);
	while (unlock(lock_ctrl_sem) < 0) ;

	if (!send_only)
		start_listen_thread();
	print(LOG_DEBUG, "Init_memshare done for %s\n", my_proc);
	initialized = 1;
	return 0;
}

int data(char *proc, char *data, int len)
{
	int index;
	header hdr;

	memset(&hdr, 0, sizeof(hdr));

	if (!initialized)
		return 2;

	while (lock(lock_ctrl_sem) < 0) ;
	print(LOG_DEBUG, "Ctrl locked by %s\n\n", my_proc);
	/*print(LOG_DEBUG, "%d trylock %d\n", try_lock1(lock_ctrl_sem),
	   lock_ctrl_sem); */

	if ((index = get_index_for_proc(proc)) < 0) {
		print(LOG_NOTICE, "No such process %s\n", proc);
		print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
		while (unlock(lock_ctrl_sem) < 0) ;
		return 1;
	}

	print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
	while (unlock(lock_ctrl_sem) < 0) ;

	print(LOG_DEBUG, "Sending data to %s at index %d\n", proc, index);
	while (lock(mem_entry[index].wlock) < 0) ;

	hdr.msg_type = DATA;
	hdr.msg_len = len;
	hdr.seq = sequence++;
	memcpy(hdr.proc_name, my_proc, PROC_NAME_SIZE);
	memcpy(mem_entry[index].shm, &hdr, SIZEOF_HEADER);
	memcpy((mem_entry[index].shm + SIZEOF_HEADER), data, len);
	if (!send_only)
		inc_sent();
	while (unlock(mem_entry[index].rlock) < 0) ;
	return 0;
}

int signal1(char *proc, int data1)
{
	int index;
	header hdr;
	signal sig;

	memset(&hdr, 0, sizeof(hdr));
	memset(&sig, 0, sizeof(sig));

	if (!initialized)
		return 2;

	while (lock(lock_ctrl_sem) < 0) ;
	print(LOG_DEBUG, "Ctrl locked by %s\n\n", my_proc);
	/*print(LOG_DEBUG, "%d trylock %d\n", try_lock1(lock_ctrl_sem),
	   lock_ctrl_sem); */

	if ((index = get_index_for_proc(proc)) < 0) {
		print(LOG_NOTICE, "No such process %s\n", proc);
		print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
		while (unlock(lock_ctrl_sem) < 0) ;
		return 1;
	}

	print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
	while (unlock(lock_ctrl_sem) < 0) ;

	print(LOG_DEBUG, "Sending signal to %s at index %d\n", proc, index);
	while (lock(mem_entry[index].wlock) < 0) ;
	print(LOG_DEBUG, "Sent signal to %s at index %d\n", proc, index);
	hdr.msg_type = SIGNAL1;
	hdr.msg_len = SIZEOF_SIGNAL;
	hdr.seq = sequence++;
	memcpy(hdr.proc_name, my_proc, PROC_NAME_SIZE);
	sig.signal1 = data1;
	memcpy(mem_entry[index].shm, &hdr, SIZEOF_HEADER);
	memcpy((mem_entry[index].shm + SIZEOF_HEADER), &sig, SIZEOF_SIGNAL);
	if (!send_only)
		inc_sent();
	while (unlock(mem_entry[index].rlock) < 0) ;
	return 0;
}

int signal2(char *proc, int data1, int data2)
{
	int index;
	header hdr;
	signal sig;

	memset(&hdr, 0, sizeof(hdr));
	memset(&sig, 0, sizeof(sig));

	if (!initialized)
		return 2;

	while (lock(lock_ctrl_sem) < 0) ;
	print(LOG_DEBUG, "Ctrl locked by %s\n\n", my_proc);
	/*print(LOG_DEBUG, "%d trylock %d\n", try_lock1(lock_ctrl_sem),
	   lock_ctrl_sem); */

	if ((index = get_index_for_proc(proc)) < 0) {
		print(LOG_NOTICE, "No such process %s\n", proc);
		print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
		while (unlock(lock_ctrl_sem) < 0) ;
		return 1;
	}

	print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
	while (unlock(lock_ctrl_sem) < 0) ;

	print(LOG_DEBUG, "Sending signal to %s at index %d\n", proc, index);
	while (lock(mem_entry[index].wlock) < 0) ;

	hdr.msg_type = SIGNAL2;
	hdr.msg_len = SIZEOF_SIGNAL;
	hdr.seq = sequence++;
	memcpy(hdr.proc_name, my_proc, PROC_NAME_SIZE);
	sig.signal1 = data1;
	sig.signal2 = data2;
	memcpy(mem_entry[index].shm, &hdr, SIZEOF_HEADER);
	memcpy((mem_entry[index].shm + SIZEOF_HEADER), &sig, SIZEOF_SIGNAL);
	if (!send_only)
		inc_sent();
	while (unlock(mem_entry[index].rlock) < 0) ;
	return 0;
}

int signal3(char *proc, int data1, int data2, int data3)
{
	int index;
	header hdr;
	signal sig;

	memset(&hdr, 0, sizeof(hdr));
	memset(&sig, 0, sizeof(sig));

	if (!initialized)
		return 2;

	while (lock(lock_ctrl_sem) < 0) ;
	print(LOG_DEBUG, "Ctrl locked by %s\n\n", my_proc);
	/*print(LOG_DEBUG, "%d trylock %d\n", try_lock1(lock_ctrl_sem),
	   lock_ctrl_sem); */

	if ((index = get_index_for_proc(proc)) < 0) {
		print(LOG_NOTICE, "No such process %s\n", proc);
		print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
		while (unlock(lock_ctrl_sem) < 0) ;
		return 1;
	}

	print(LOG_DEBUG, "Ctrl unlocked by %s\n\n", my_proc);
	while (unlock(lock_ctrl_sem) < 0) ;

	print(LOG_DEBUG, "Sending signal to %s at index %d\n", proc, index);
	while (lock(mem_entry[index].wlock) < 0) ;

	hdr.msg_type = SIGNAL3;
	hdr.msg_len = SIZEOF_SIGNAL;
	hdr.seq = sequence++;
	memcpy(hdr.proc_name, my_proc, PROC_NAME_SIZE);
	sig.signal1 = data1;
	sig.signal2 = data2;
	sig.signal3 = data3;
	memcpy(mem_entry[index].shm, &hdr, SIZEOF_HEADER);
	memcpy((mem_entry[index].shm + SIZEOF_HEADER), &sig, SIZEOF_SIGNAL);
	if (!send_only)
		inc_sent();
	while (unlock(mem_entry[index].rlock) < 0) ;
	return 0;
}

int get_datasize(char *proc)
{
	int index;
	proc_entry *entry;

	if ((index = get_index_for_proc(proc)) < 0) {
		print(LOG_NOTICE, "No such process %s\n", proc);
		return 0;
	}
	entry = get_proc_at_index(index);
	return (entry->size_shm);
}

int get_proc_info(int index, int *send_count, int *rec_count, int *data_size,
		  char **procptr)
{
	proc_entry *entry;

	/* this call will not check if the entry is active */
	entry = get_proc_at_index(index);
	*send_count = entry->sent;
	*rec_count = entry->received;
	*data_size = entry->size_shm;
	*procptr = entry->proc_name;
	return 0;
}

 /*
    int
    s_data(int index, char *data, int len)
    {
    return 0;
    }

    int
    s_signal1(int index, int data)
    {
    return 0;
    }

    int
    s_signal2(int index, int data1, int data2)
    {
    return 0;
    }

    int
    s_signal3(int index, int data1, int data2, int data3)
    {
    return 0;
    } */
