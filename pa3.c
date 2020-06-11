/**********************************************************************
 * Copyright (c) 2020
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "types.h"
#include "locks.h"
#include "atomic.h"
#include "list_head.h"

/*********************************************************************
 * Spinlock implementation
 *********************************************************************/
struct spinlock
{
	int held;
};

/*********************************************************************
 * init_spinlock(@lock)
 *
 * DESCRIPTION
 *   Initialize your spinlock instance @lock
 */
void init_spinlock(struct spinlock *l)
{
	l->held = 0;
	return;
}

/*********************************************************************
 * acqure_spinlock(@lock)
 *
 * DESCRIPTION
 *   Acquire the spinlock instance @lock. The returning from this
 *   function implies that the calling thread grapped the lock.
 *   In other words, you should not return from this function until
 *   the calling thread gets the lock.
 */
void acquire_spinlock(struct spinlock *l)
{
	while (compare_and_swap(&l->held, 0, 1))
		;
	return;
}

/*********************************************************************
 * release_spinlock(@lock)
 *
 * DESCRIPTION
 *   Release the spinlock instance @lock. Keep in mind that the next thread
 *   can grap the @lock instance right away when you mark @lock available;
 *   any pending thread may grap @lock right after marking @lock as free
 *   but before returning from this function.
 */
void release_spinlock(struct spinlock *l)
{
	l->held = 0;
	return;
}

/********************************************************************
 * Blocking mutex implementation
 ********************************************************************/
struct thread
{
	pthread_t pthread;
	struct list_head list;
};

struct mutex
{
	struct list_head Q;
	int S;
	int held;
};

/*********************************************************************
 * init_mutex(@mutex)
 *
 * DESCRIPTION
 *   Initialize the mutex instance pointed by @mutex.
 */

void init_mutex(struct mutex *mutex)
{
	struct list_head *head = &mutex->Q;
	head->next = head;
	head->prev = head;
	mutex->held = 0;
	mutex->S = 1;
	return;
}

/*********************************************************************
 * acquire_mutex(@mutex)
 *
 * DESCRIPTION
 *   Acquire the mutex instance @mutex. Likewise acquire_spinlock(), you
 *   should not return from this function until the calling thread gets the
 *   mutex instance. But the calling thread should be put into sleep when
 *   the mutex is acquired by other threads.
 *
 * HINT
 *   1. Use sigwaitinfo(), sigemptyset(), sigaddset(), sigprocmask() to
 *      put threads into sleep until the mutex holder wakes up
 *   2. Use pthread_self() to get the pthread_t instance of the calling thread.
 *   3. Manage the threads that are waiting for the mutex instance using
 *      a custom data structure containing the pthread_t and list_head.
 *      However, you may need to use a spinlock to prevent the race condition
 *      on the waiter list (i.e., multiple waiters are being inserted into the 
 *      waiting list simultaneously, one waiters are going into the waiter list
 *      and the mutex holder tries to remove a waiter from the list, etc..)
 */

void print_thread(struct mutex *mutex)
{
	struct thread *t;
	list_for_each_entry(t, &mutex->Q, list)
	{
		printf("\n%d", t->pthread);
	}
	printf("\nS: %d\n", mutex->S);
	return;
}

void acquire_mutex(struct mutex *mutex)
{
	sigset_t mask;
	int sig_no;
	struct thread *new = malloc(sizeof(struct thread));
	// printf("\n\n//acquire//");
	// print_thread(mutex);
	while (compare_and_swap(&mutex->held, 0, 1))
		;
	mutex->S--;
	if (mutex->S < 0)
	{
		sigemptyset(&mask);
		sigaddset(&mask, SIGINT);
		sigprocmask(SIG_BLOCK, &mask, NULL);
		new->pthread = pthread_self();
		list_add_tail(&new->list, &mutex->Q);
		mutex->held = 0;
		// printf("\nadd: %d", new->pthread);
		while (1)
		{
			if (sigwait(&mask, &sig_no) != 0)
			{
				continue;
			}
			if (sig_no == SIGINT)
			{
				while (compare_and_swap(&mutex->held, 0, 1))
					;
				sigprocmask(SIG_UNBLOCK, &mask, NULL);
				break;
			}
		}
		// printf("\nacquire end\n");
	}
	free(new);
	mutex->held = 0;
	return;
}

/*********************************************************************
 * release_mutex(@mutex)
 *
 * DESCRIPTION
 *   Release the mutex held by the calling thread.
 *
 * HINT
 *   1. Use pthread_kill() to wake up a waiter thread
 *   2. Be careful to prevent race conditions while accessing the waiter list
 */
void release_mutex(struct mutex *mutex)
{
	struct thread *next;
	// printf("\n\n//release//");
	// print_thread(mutex);

	while (compare_and_swap(&mutex->held, 0, 1))
		;
	mutex->S++;
	if (mutex->S <= 0)
	{
		next = list_first_entry(&mutex->Q, struct thread, list);
		list_del_init(&next->list);
		mutex->held = 0;
		// printf("\nkill-thread: %d\n", next->pthread);
		pthread_kill(next->pthread, SIGINT);
		// printf("\nrelease end\n");
	}
	mutex->held = 0;
	return;
}

/*********************************************************************
 * Ring buffer
 *********************************************************************/
struct ringbuffer
{
	/** NEVER CHANGE @nr_slots AND @slots ****/
	/**/ int nr_slots; /**/
	/**/ int *slots;	 /**/
	int held;
	int count;
	int out;
	int in; /*****************************************/
};

struct ringbuffer ringbuffer = {};

/*********************************************************************
 * enqueue_into_ringbuffer(@value)
 *
 * DESCRIPTION
 *   Generator in the framework tries to put @value into the buffer.
 */
void enqueue_into_ringbuffer(int value)
{
again:
	while (ringbuffer.count == ringbuffer.nr_slots)
		;
	while (compare_and_swap(&ringbuffer.held, 0, 1))
		;
	if (ringbuffer.count == ringbuffer.nr_slots)
	{
		ringbuffer.held = 0;
		goto again;
	}
	*(ringbuffer.slots + ringbuffer.in) = value;
	ringbuffer.in = (ringbuffer.in + 1) % ringbuffer.nr_slots;
	ringbuffer.count++;
	ringbuffer.held = 0;
}

/*********************************************************************
 * dequeue_from_ringbuffer(@value)
 *
 * DESCRIPTION
 *   Counter in the framework wants to get a value from the buffer.
 *
 * RETURN
 *   Return one value from the buffer.
 */
int dequeue_from_ringbuffer(void)
{
	int tmp;
again:
	while (ringbuffer.count == 0)
		;
	while (compare_and_swap(&ringbuffer.held, 0, 1))
		;
	if (ringbuffer.count == 0)
	{
		ringbuffer.held = 0;
		goto again;
	}
	tmp = ringbuffer.out;
	ringbuffer.out = (ringbuffer.out + 1) % ringbuffer.nr_slots;
	ringbuffer.count--;
	ringbuffer.held = 0;
	return *(ringbuffer.slots + tmp);
}

/*********************************************************************
 * fini_ringbuffer
 *
 * DESCRIPTION
 *   Clean up your ring buffer.
 */
void fini_ringbuffer(void)
{
	free(ringbuffer.slots);
	ringbuffer.in = 0;
	ringbuffer.out = 0;
	ringbuffer.count = 0;
}

/*********************************************************************
 * init_ringbuffer(@nr_slots)
 *
 * DESCRIPTION
 *   Initialize the ring buffer which has @nr_slots slots.
 *
 * RETURN
 *   0 on success.
 *   Other values otherwise.
 */
int init_ringbuffer(const int nr_slots)
{
	/** DO NOT MODIFY THOSE TWO LINES **************************/
	/**/ ringbuffer.nr_slots = nr_slots;										/**/
	/**/ ringbuffer.slots = malloc(sizeof(int) * nr_slots); /**/
	/***********************************************************/
	ringbuffer.in = 0;
	ringbuffer.out = 0;
	ringbuffer.count = 0;
	ringbuffer.held = 0;
	return 0;
}
