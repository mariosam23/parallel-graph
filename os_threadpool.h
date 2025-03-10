/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __OS_THREADPOOL_H__
#define __OS_THREADPOOL_H__	1

#include "os_list.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct {
	void *argument;
	void (*action)(void *arg);
	void (*destroy_arg)(void *arg);
	os_list_node_t list;
} os_task_t;

typedef struct os_threadpool {
	unsigned int num_threads;
	pthread_t *threads;
	unsigned int num_tasks;

	// Flag to check if the threadpool is finished
	bool finished;

	/*
	 * Head of queue used to store tasks.
	 * First item is head.next, if head.next != head (i.e. if queue
	 * is not empty).
	 * Last item is head.prev, if head.prev != head (i.e. if queue
	 * is not empty).
	 */
	os_list_node_t head;

	// Mutex used to synchronize access to the queue
	pthread_mutex_t mutex_queue;

	// Condition variable used to signal the threads
	// that there is a new task
	pthread_cond_t cond_queue;
} os_threadpool_t;

os_task_t *create_task(void (*f)(void *), void *arg, void (*destroy_arg)(void *));
void destroy_task(os_task_t *t);

os_threadpool_t *create_threadpool(unsigned int num_threads);
void destroy_threadpool(os_threadpool_t *tp);

void enqueue_task(os_threadpool_t *q, os_task_t *t);
os_task_t *dequeue_task(os_threadpool_t *tp);
void wait_for_completion(os_threadpool_t *tp);

#endif
