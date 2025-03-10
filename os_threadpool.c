// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	// Lock the queue so that no other thread can access it
	DIE(pthread_mutex_lock(&tp->mutex_queue) != 0, "pthread_mutex_lock");

	tp->num_tasks++;

	// Add the task to the queue
	tp->head.prev->next = &t->list;
	t->list.prev = tp->head.prev;
	t->list.next = &tp->head;
	tp->head.prev = &t->list;

	// Signal the threads that there is a new task
	DIE(pthread_cond_broadcast(&tp->cond_queue) != 0, "pthread_cond_broad");

	// Unlock the queue
	DIE(pthread_mutex_unlock(&tp->mutex_queue) != 0, "pthread_mutex_unlock");
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t;

	// Lock the queue so that no other thread can access it
	DIE(pthread_mutex_lock(&tp->mutex_queue) != 0, "pthread_mutex_lock");

	// Wait for a task to be added to the queue
	while (queue_is_empty(tp) && !tp->finished)
		DIE(pthread_cond_wait(&tp->cond_queue, &tp->mutex_queue) != 0,
			"pthread_cond_wait");

	// If the queue is empty and the work is done, return NULL
	if (tp->num_tasks <= 0) {
		DIE(pthread_mutex_unlock(&tp->mutex_queue) != 0,
			"pthread_mutex_unlock");
		return NULL;
	}

	tp->num_tasks--;

	// Get the first task from the queue and remove it
	t = list_entry(tp->head.prev, os_task_t, list);
	list_del(tp->head.prev);

	// Unlock the queue
	DIE(pthread_mutex_unlock(&tp->mutex_queue) != 0, "pthread_mutex_unlock");

	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	DIE(pthread_mutex_lock(&tp->mutex_queue) != 0, "pthread_mutex_lock");

	// Put the finished flag to true
	tp->finished = true;

	// Signal all threads that the work is done
	DIE(pthread_cond_broadcast(&tp->cond_queue) != 0, "pthread_cond_broadcast");

	pthread_mutex_unlock(&tp->mutex_queue);

	// Join all worker threads
	for (unsigned int i = 0; i < tp->num_threads; i++)
		DIE(pthread_join(tp->threads[i], NULL) != 0, "pthread_join");
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	// Initialize the synchronization data
	pthread_mutex_init(&tp->mutex_queue, NULL);
	pthread_cond_init(&tp->cond_queue, NULL);
	tp->num_tasks = 0;
	tp->finished = false;

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	// Destroy the synchronization data
	pthread_mutex_destroy(&tp->mutex_queue);
	pthread_cond_destroy(&tp->cond_queue);

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
