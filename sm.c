/**
 * CS2106 AY 20/21 Semester 1 - Lab 2
 *
 * This file contains function definitions. Your implementation should go in
 * this file.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sm.h"

int current_service_number = 0;

// Array of (Array of PIDs)
// Each index corresponds to one service, but any one service
// might have more than one associated PID (since multiple processes might launch)
int *arr_pid[SM_MAX_SERVICES];

// Each index corresponds to the number of processes for this service.
// Used to keep track of array size.
int arr_num_of_processes[SM_MAX_SERVICES];

// Array of Strings
// Each index corresponds to one service, and we only track
// the last process path.
char *arr_path[SM_MAX_SERVICES];

// Array of (Array of bools)
// Each index corresponds to one service.
// This is for Exercise 3, where we need to know if
// a process has terminated; if so, we don't send a signal.
// This tracks if a process has terminated.
bool *arr_exited[SM_MAX_SERVICES];

// Stores the given pid at the given coordinates, essentially arr_pid[service_table_idx][process_table_idx].
void _store_pid(int service_table_idx, int process_table_idx, int pid) {
	int *service_array = arr_pid[service_table_idx];
	service_array[process_table_idx] = pid;
}

// Stores the path for the given service index.
// Allocates enough memory for it as well.
void _store_path(int service_idx, const char* path) {
	arr_path[service_idx] = malloc((strlen(path) + 1) * sizeof(char));
	if (arr_path[service_idx] == NULL) {
		printf("[%d][_store_path] Failed to allocate memory in arr_path to store [%s].\n", getpid(), path);
		exit(1);
	}
	strcpy(arr_path[service_idx], path);
}

/**
 * Stores the process information (process pid, path) into the arrays defined above.
 * For use when we're checking up on them.
 * num_of_processes should be at least 1.
 */
void _store_process_information(int pids[], const char *path, int num_of_processes) {
	// Short circuit
	if (num_of_processes < 1) {
		printf("[%d][_store_process_information] Number of processes given was 0. Returning from function.\n", getpid());
		return;
	}
	
	arr_num_of_processes[current_service_number] = num_of_processes;
	
	for (int i = 0; i < num_of_processes; i++) {
		_store_pid(current_service_number, i, pids[i]);
	}
	
	
	_store_path(current_service_number, path);
	
	current_service_number++;
	return;
}

// Helper function to determine the number of processes that sm_start
// should launch.
int _get_num_of_processes_from_sm_start_args(const char *processes[]) {
	// Idea: We traverse the array.
	// We won't segfault because we know that as long as we see *one* NULL,
	// we always have another element after it.
	// If, however, the previous element was a NULL and we see another NULL,
	// we know we've reached the end of the array.
	
	int number_of_commands = 0;
	bool is_previous_elt_was_null = false;
	bool have_not_seen_double_null = false;
	for (int i = 0; true; i++) {
		const char *current_string = processes[i];
		if (is_previous_elt_was_null && current_string == NULL) {
			// Reached end of array
			return number_of_commands;
		}
		
		if (current_string == NULL) {
			is_previous_elt_was_null = true;
			number_of_commands++;
			continue;
		}
		
		// Reset the flag
		if (current_string != NULL) {
			is_previous_elt_was_null = false;
			continue;
		}
	}
}

// Returns the length of the array
// We can do this without segfault-ing because we know
// the array will always end in two NULLs.
int _get_sm_start_args_length(const char *processes[]) {
	int len = 0;
	bool is_previous_elt_null = false;
	
	for (int i = 0; true; i++) {
		const char *current_string = processes[i];
		if (is_previous_elt_null && current_string == NULL) {
			// Reached end of array
			return len + 1;
		}
		
		if (current_string == NULL) {
			is_previous_elt_null = true;
		}
		
		if (current_string != NULL) {
			is_previous_elt_null = false;
		}
		
		len++;
	}
}

// Helper function to split the argument to sm_start.
// Takes in the full array as specified in Exercise 2,
// and returns an array of arrays split by process e.g.
// {"/bin/echo", "hello", NULL, "/bin/cat", "NULL", "NULL"} becomes
// { {"/bin/echo", "hello", NULL}, {"/bin/cat", "NULL"} }
const char*** _split_sm_start_args(const char *processes[]) {
	// Length of the array.  
  	int processes_len = _get_sm_start_args_length(processes);
	int number_of_processes = _get_num_of_processes_from_sm_start_args(processes);
	
	// Buffer to hold stuff. We initialize it to the maximum size required
	// (The length of the processes array).
	char *buf[processes_len];
	
	// The array we are going to return.
	const char ***res = malloc(sizeof(char*) * number_of_processes);
	
	// Index of the current process in our results array.
	int current_process = 0;
	
	// Current number of arguments for our current process.
	int current_number_of_args = 0;
	
	// Tracks the maximum number of slots used in buf[].
  	// We shall need this later to free buf[] properly.
  	int highest_number_of_args = 0;
	
	// Note here we use the upper bound of processes_len - 1
	// This is because the end of the array is a double NULL
	// We don't care about the last NULL, as it doesn't denote a process
	// So we stop early.
	for (int i = 0; i < processes_len - 1; i++) {
		if (processes[i] == NULL) {
			// End of args
			// Flush buffer to our result.
			// We add one to account for the NULL at the end.
			res[current_process] = malloc(sizeof(char*) * (current_number_of_args + 1));
			for (int j = 0; j < current_number_of_args; j++) {
        		res[current_process][j] = malloc(strlen(buf[j] + 1) * sizeof(char));
				strcpy((char *) res[current_process][j], buf[j]);
			}
			
			res[current_process][current_number_of_args] = NULL;
			current_process++;

      	if (current_number_of_args > highest_number_of_args) {
        	highest_number_of_args = current_number_of_args;
     	}
			current_number_of_args = 0;
			continue;
	}
		
    buf[current_number_of_args] = malloc((strlen(processes[i] + 1) * sizeof(char)));
	strcpy(buf[current_number_of_args], processes[i]);
    current_number_of_args++;
	}
	
	// Don't forget to free the members of buf
	// since we had to malloc for them.
  	for (int k = 0; k < highest_number_of_args; k++) {
    	free(buf[k]);
  	}
	return res;
}

// Helper function that returns the length of a NULL-terminated array.
// Includes the NULL at the end.
// e.g. {"a", "b", NULL} -> 3
int _get_length_of_null_terminated_array(const char *arr[]) {
	int len = 0;
	for (int i = 0; true; i++) {
		const char *curr_str = arr[i];
		if (arr[i] == NULL) {
			return len + 1;
		}
		len++;
	}
}
/**
 * Returns true if a process with the given pid is running, and false otherwise.
 * In theory this might be an issue if pid reuse occurs, but we're dealing with
 * 32 processes in a system where the pid reuse occurs on wraparound (default 32768),
 * so that's 3 orders of magnitude off. We should be okay.
 */
bool _is_process_running(int pid) {
	int status;
	pid_t return_pid = waitpid(pid, &status, WNOHANG);
	
	if (return_pid == -1) {
		// Error
		printf("[%d][_is_process_running] waitpid(%d, &status, WNOHANG) call failed.\n", getpid(), pid);
		exit(1);
	} else if (return_pid == 0) {
		// Still running
		return true;
	} else if (return_pid == pid) {
		// Stopped running
		return false;
	}
	
	// We'll never reach this, but just to satisfy the compiler.
	return false;
}

// Use this function to any initialisation if you need to.
void sm_init(void) {
	for (int i = 0; i < SM_MAX_SERVICES; i++) {
		arr_pid[i] = malloc(sizeof(int) * 32);
		arr_exited[i] = malloc(sizeof(bool) * 32);
		arr_exited[i] = false;
	}
}

// Use this function to do any cleanup of resources.
void sm_free(void) {
	for (int i = 0; i < SM_MAX_SERVICES; i++) {
		free(arr_pid[i]);
		free(arr_path[i]);
		free(arr_exited[i]);
	}
}

// Exercise 1a/2: start services
// NEED TO CHANGE THIS FOR MULTI-PROCESS
void sm_start(const char *(processes[])) {
	// The number of processes in this service.
	int number_of_processes = _get_num_of_processes_from_sm_start_args(processes);
	
	// An array of (array of process args). For easier processing.
	char const **process_args = _split_sm_start_args(processes);
	
	// An array of process pids, for storing process information later.
	int *process_pids[number_of_processes];
	
	// The process name. This will get overwritten until the last one,
	// so it'll store the very last process's name.
	char *process_name;

	// Iterate over the number of processes.
	// Treat each one like a single process startup.
	// Only difference is how we store process information.
	// TODO: Add in piping
	for (var i = 0; i < number_of_processes; i++) {
		char const **current_process_args = process_args[i];
		process_name = current_process_args[0];
		
		int child_pid = fork();
		bool is_parent_process = child_pid > 0;
		bool is_child_process = child_pid == 0;
		bool is_fork_successful = child_pid >= 0;
	
		if(!is_fork_successful) {
			printf("[%d][sm_start] Failed to fork child process.\n", getpid());
			exit(1);
		}
		
		if (is_parent_process) {
			// Store the pids for later use.
			pids[i] = child_pid;
			return;
		}
		
		if (is_child_process) {
			execv(process_name, (char * const *) processes);
		}
	}
	
	_store_process_information(pids, process_name, number_of_processes);
	
	// TODO: Free process_args
}

// Exercise 1b: print service status
size_t sm_status(sm_status_t statuses[]) {
	for (int i = 0; i < current_service_number; i++) {
		int process_pid = arr_pid[i][arr_num_of_processes[i]];
		char *process_path = arr_path[i];
		
		// We include this short circuit here so that it returns false without
		// a function call if our process has exited before.
		bool is_there_a_process_with_this_pid_running = _is_process_running(process_pid);
		bool is_process_running = (!arr_exited[i]) && is_there_a_process_with_this_pid_running;
		
		// Note to self: A possible pitfall is that between start and status, the process exits,
		// and a new process with the same pid starts up (so we never find out the original process actually exited).
		// then we could erroneously believe that "our" process is still running, when its pid was actually recycled
		// to some other process. Is this a problem worth solving?
		sm_status_t *current_status = &statuses[i];
		
		current_status -> pid = process_pid;
		current_status -> path = process_path;
		current_status -> running = is_process_running
		
		
		// Flag the process as terminated if we discovered that it is.
		if (!is_there_a_process_with_this_pid_running) {
			arr_exited[i] = true;
		}
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
