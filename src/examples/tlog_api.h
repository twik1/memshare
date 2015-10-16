/*
 *
 * Tlog, syslog wrapper with IPC control.
 * Copyright (C) 2012  Tommy Wiklund
 *
 */

/*****************************************************************************/
/* Function Name      : tsyslog_init                                         */
/* Description        : Initializes tlog, sets the syslog tag name           */
/* Input(s)           : char* (string 20 char)                               */
/* Output(s)          : None.                                                */
/* Return Value(s)    : 0 ok                                                 */
/*                    : 1 a name has to be present                           */
/*                    : 2 unable to initialize memshare                      */
/*                    : 3 already initialized                                */
/*****************************************************************************/
int tsyslog_init(char *name);

/*****************************************************************************/
/* Function Name      : tsyslog_prio_init                                    */
/* Description        : Initializes tlog, sets the syslog tag name and       */
/*                    : prio mask                                            */
/* Input(s)           : char* (string 20 char)                               */
/*                    : int priority                                         */
/* Output(s)          : None.                                                */
/* Return Value(s)    : 0 ok                                                 */
/*                    : 1 a name has to be present                           */
/*                    : 2 unable to initialize memshare                      */
/*                    : 3 already initialized                                */
/*                    : 4 prio not within range                              */
/*****************************************************************************/
int tsyslog_prio_init(char *name, int);

/*****************************************************************************/
/* variable Name      : tlog_mask                                            */
/* Description        : bitmask that tells user which log levels are active  */
/*****************************************************************************/
extern int tlog_mask;

/*****************************************************************************/
/* Macro Name         : tsyslog                                              */
/* Description        : Push a string to syslog depending on priority mask   */
/* Input(s)           : priority                                             */
/*                    : args format string and args to be sent to syslog     */
/* Output(s)          : None.                                                */
/*****************************************************************************/
#define tsyslog(priority, args...)              \
    do {                                        \
        if (tlog_mask & (1 << priority)) {      \
            syslog(priority, args);             \
        }                                       \
    } while(0)

/*****************************************************************************/
/* Function Name      : tsyslog_set                                          */
/* Description        : Set a bit in the mask for syslog output              */
/* Input(s)           : int priority according to syslog.h                   */
/* Output(s)          : None.                                                */
/* Return Value(s)    : 0 ok                                                 */
/*                    : 1 it has to be initalized first                      */
/*****************************************************************************/
int tsyslog_set(int);

/*****************************************************************************/
/* Function Name      : tsyslog_del                                          */
/* Description        : Delete a bit in the priority mask for syslog output  */
/* Input(s)           : int priority according to syslog.h                   */
/* Output(s)          : None.                                                */
/* Return Value(s)    : 0 ok                                                 */
/*                    : 1 it has to be initalized first                      */
/*****************************************************************************/
int tsyslog_del(int);

/*****************************************************************************/
/* Function Name      : tsyslog_replace                                      */
/* Description        : Replace the priority mask for syslog output          */
/* Input(s)           : int priority according to syslog.h                   */
/* Output(s)          : None.                                                */
/* Return Value(s)    : 0 ok                                                 */
/*                    : 1 it has to be initalized first                      */
/*****************************************************************************/
int tsyslog_replace(int);

/*****************************************************************************/
/*                                                                           */
/* Set priority bit      :   memsend -s2 <process > 1 <priority>             */
/* Del priority bit      :   memsend -s2 <process > 2 <priority>             */
/* Replace priority mask :   memsend -s2 <process > 3 <priority mask>        */
/* This can be used to dynamically set/delete/replace the priority mask for  */
/* different processes                                                       */
/*                                                                           */
/* LOG_EMERG     priority=0                                                  */
/* LOG_ALERT     priority=1                                                  */
/* LOG_CRIT      priority=2                                                  */
/* LOG_ERR       priority=3                                                  */
/* LOG_WARNING   priority=4                                                  */
/* LOG_NOTICE    priority=5                                                  */
/* LOG_INFO      priority=6                                                  */
/* LOG_DEBUG     priority=7                                                  */
/*                                                                           */
/*****************************************************************************/
