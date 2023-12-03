// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;

typedef unsigned int uint;

// Mutex used to synchronize access to the graph
pthread_mutex_t mutex_graph;

static void free_function(void *arg)
{
	free(arg);
}

static void process_node(void *arg)
{
	uint idx = *(uint *) arg;

	DIE(pthread_mutex_lock(&mutex_graph) != 0, "pthread_mutex_lock");

	// Check if the node is not visited and process it
	if (graph->visited[idx] == NOT_VISITED) {
		os_node_t *node = graph->nodes[idx];

		sum += node->info;

		graph->visited[idx] = DONE;

		for (uint i = 0; i < node->num_neighbours; i++) {
			if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
				// Get the neighbour node and create a new task for it
				uint *new_arg = malloc(sizeof(uint));

				DIE(!new_arg, "malloc");

				*new_arg = node->neighbours[i];

				os_task_t *new_task = create_task(process_node, new_arg,
												  free_function);
				enqueue_task(tp, new_task);
			}
		}
	}

	DIE(pthread_mutex_unlock(&mutex_graph) != 0, "pthread_mutex_unlock");
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	// Initialize graph synchronization mechanisms
	pthread_mutex_init(&mutex_graph, NULL);
	sum = 0;

	// Initialize the visited array
	for (uint i = 0; i < graph->num_nodes; i++)
		graph->visited[i] = NOT_VISITED;

	tp = create_threadpool(NUM_THREADS);

	// Create the initial task argument
	uint *initial_arg = malloc(sizeof(uint));

	DIE(initial_arg == NULL, "malloc");

	// Start processing the graph from the node 0
	*initial_arg = 0;
	os_task_t *initial_task = create_task(process_node, initial_arg,
										  free_function);
	enqueue_task(tp, initial_task);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	pthread_mutex_destroy(&mutex_graph);

	printf("%d", sum);

	return 0;
}
