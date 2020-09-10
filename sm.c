/**
 * CS2106 AY 20/21 Semester 1 - Lab 2
 *
 * This file contains function definitions. Your implementation should go in
 * this file.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "sm.h"

int current_service_number = 0;

// Array of PIDs
int arr_pid[SM_MAX_SERVICES];

// Array of char pointers
char *arr_path[SM_MAX_SERVICES];

// Array of bools
bool arr_running[SM_MAX_SERVICES];

void _store_process_information(int pid, const char *name, bool is_running) {
	arr_pid[current_service_number] = pid;
	
	arr_path[current_service_number] = malloc((strlen(name) + 1) * sizeof(char));
	if (arr_path[current_service_number] == NULL) {
		printf("[%d][_store_process_information] Failed to allocate memory to store process name for [%d, %s, %d].\n", getpid(), pid, name, is_running);
		exit(1);
	}
	strcpy(arr_path[current_service_number], name);
	
	arr_running[current_service_number] = is_running;
	
	current_service_number++;
	return;
}

// Use this function to any initialisation if you need to.
void sm_init(void) {
}

// Use this function to do any cleanup of resources.
void sm_free(void) {
	for (int i = 0; i < SM_MAX_SERVICES; i++) {
		free(arr_path[i]);
	}
}

// Exercise 1a/2: start services
void sm_start(const char *(processes[])) {
	char const *process_name = processes[0];

	int child_pid = fork();
	bool is_parent_process = child_pid > 0;
	bool is_child_process = child_pid == 0;
	bool is_fork_successful = child_pid >= 0;
	
	if(!is_fork_successful) {
		printf("[%d][sm_start] Failed to fork child process.\n", getpid());
		exit(1);
	}
	
	if (is_parent_process) {
		int process_pid = child_pid;
		bool is_running = true;
		_store_process_information(process_pid, process_name, is_running);
		return;
	}
	
	if (is_child_process) {
		execv(process_name, (char * const *) processes);
	}
}

// Exercise 1b: print service status
size_t sm_status(sm_status_t statuses[]) {
	for (int i = 0; i < current_service_number; i++) {
		sm_status_t *current_status = &statuses[i];
		current_status -> pid = arr_pid[i];
		current_status -> path = arr_path[i];
		current_status -> running = arr_running[i];
	}
	
	return current_service_number;
}

// Exercise 3: stop service, wait on service, and shutdown
void sm_stop(size_t index) {
}

void sm_wait(size_t index) {
}

void sm_shutdown(void) {
}

// Exercise 4: start with output redirection
void sm_startlog(const char *processes[]) {
}

// Exercise 5: show log file
void sm_showlog(size_t index) {
}
