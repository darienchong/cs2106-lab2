/**
 * CS2106 AY 20/21 Semester 1 - Lab 2
 *
 * This file contains function definitions. Your implementation should go in
 * this file.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sm.h"

// For pipes
#define READ_END 0
#define WRITE_END 1

// Debug flag for debugging messages.
bool debug = true;

// Tracks how many services we have launched.
int number_of_services_launched = 0;

// Array of (Array of PIDs)
// Each index corresponds to one service, but any one service
// might have more than one associated PID (since multiple processes might launch)
int *arr_pids[SM_MAX_SERVICES];

// Each index corresponds to the number of processes for this service.
// Used to keep track of array size.
int arr_num_of_processes[SM_MAX_SERVICES];

// Array of (Array of paths)
// Each index corresponds to one service, and we track
// every process path.
char **arr_paths[SM_MAX_SERVICES];

// Array of (Array of bools)
// Tracks whether the process at arr[service_idx][process_idx]
// has exited or not.
// This is necessary as we use waitpid() to track whether a child process
// has exited or not. If it has, we flag it here and do not call waitpid() 
// on that child anymore.
bool *arr_exited[SM_MAX_SERVICES];

// Stores the given pid at the given coordinates, essentially arr_pids[service_table_idx][process_table_idx].
void _store_pid(int service_idx, int process_idx, int pid) {
	arr_pids[service_idx][process_idx] = pid;
}

// Stores the path for the given service index.
// Allocates enough memory for it as well.
void _store_path(int service_idx, int process_idx, const char* path) {
	arr_paths[service_idx][process_idx] = malloc((strlen(path) + 1) * sizeof(char));
	if (arr_paths[service_idx][process_idx] == NULL) {
		if (debug) {
			printf("[%d][_store_path] Failed to allocate memory in arr_paths to store [%s] at [%d][%d].\n", getpid(), path, service_idx, process_idx);
		}
		exit(1);
	}
	strcpy(arr_paths[service_idx][process_idx], path);
}

// Stores the number of processes initiated for the given service index.
void _store_num_of_processes(int service_index, int num_of_processes) {
	arr_num_of_processes[service_index] = num_of_processes;
}

/**
 * Stores the process information (process pid, path) into the arrays defined above.
 * For use when we're checking up on them.
 * DOES NOT STORE THE NUMBER OF PROCESSES, THIS MUST BE DONE SEPARATELY.
 */
void _store_process_information(int pid, const char *path, int service_index, int process_index) {
	_store_pid(service_index, process_index, pid);
	_store_path(service_index, process_index, path);
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
	bool is_previous_elt_null = false;
	for (int i = 0; true; i++) {
		const char *current_string = processes[i];
		if (is_previous_elt_null && current_string == NULL) {
			// Reached end of array
			return number_of_commands;
		}
		
		if (current_string == NULL) {
			is_previous_elt_null = true;
			number_of_commands++;
			continue;
		}
		
		// Reset the flag
		if (current_string != NULL) {
			is_previous_elt_null = false;
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
	// (The length of the processes array). We also zero out all the entries.
	char *buf[processes_len];
	for (int l = 0; l < processes_len; l++) {
		buf[l] = NULL;
	}
	
	// The array we are going to return.
	const char ***res = malloc(sizeof(char*) * number_of_processes);
	
	// Index of the current process in our results array.
	int current_process = 0;
	
	// Current number of arguments for our current process.
	int current_process_arg_count = 0;
	
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
			res[current_process] = malloc(sizeof(char*) * (current_process_arg_count + 1));
			for (int j = 0; j < current_process_arg_count; j++) {
        		res[current_process][j] = malloc((strlen(buf[j]) + 1) * sizeof(char));
				strcpy((char *) res[current_process][j], buf[j]);
			}
			
			res[current_process][current_process_arg_count] = NULL;
			current_process++;

      		if (current_process_arg_count > highest_number_of_args) {
        		highest_number_of_args = current_process_arg_count;
     		}
			
			
			current_process_arg_count = 0;
			continue;
		}
		
		// We're going to be resizing this buffer as we need to, so we use realloc instead of malloc.
    	buf[current_process_arg_count] = realloc(buf[current_process_arg_count], (strlen(processes[i]) + 1) * sizeof(char));
		strcpy(buf[current_process_arg_count], processes[i]);
    	current_process_arg_count++;
	}

	// Free buf
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
		if (arr[i] == NULL) {
			return len + 1;
		}
		len++;
	}
}

/**
 * Returns true if a child process with the given pid is running, and false otherwise.
 */
bool _is_process_running(int pid, int service_idx, int process_idx) {
	bool is_process_observed_terminated = arr_exited[service_idx][process_idx]; 
	
	if (is_process_observed_terminated) {
		return false;
	}
	
	int status;
	pid_t return_pid = waitpid(pid, &status, WNOHANG);
	
	if (return_pid == -1) {
		// waitpid has reported an error.
		bool is_errno_ECHILD = (errno == ECHILD);
		bool is_errno_EINTR = (errno == EINTR);
		bool is_errno_EINVAL = (errno == EINVAL);

		if (debug) {
			printf("[%d][_is_process_running] waitpid(%d, &status, WNOHANG) call failed.\n", getpid(), pid);
			
			if (is_errno_ECHILD) {
				printf("[%d][_is_process_running] errno = ECHILD\n", getpid());
			}
			
			if (is_errno_EINTR) {
				printf("[%d][_is_process_running] errno = EINTR\n", getpid());
			}
	
			if (is_errno_EINVAL) {
				printf("[%d][_is_process_running] errno = EINVAL\n", getpid());
			}
			
			printf("[%d][_is_process_running] WIFEXITED(status) = %d\n", getpid(), WIFEXITED(status));
			printf("[%d][_is_process_running] WEXITSTATUS(status) = %d\n", getpid(), WEXITSTATUS(status));
			printf("[%d][_is_process_running] WIFSIGNALED(status) = %d\n", getpid(), WIFSIGNALED(status));
	
			if (WIFSIGNALED(status)) {
				printf("[%d][_is_process_running] WTERMSIG(status) = %d\n", getpid(), WTERMSIG(status));
			}
		}
		
		exit(1);
	} else if (return_pid == 0) {
		// Still running
		return true;
	} else if (return_pid == pid) {
		// Stopped running
		arr_exited[service_idx][process_idx] = true;
		return false;
	}
	
	// We'll never reach this, but just to satisfy the compiler.
	return false;
}

// Sets the left pipe for the process calling this to the given pipe.
// If the pipe given is NULL, then no pipe is set.
void _set_pipe_read_from(int *pipe) {
	if (pipe != NULL) {
		// The process will now read in from pipe instead of stdin.
		dup2(pipe[READ_END], STDIN_FILENO);

		// Good practice to close the file descriptors
		// now that we have a copy of it (dup2)
		// Note that this won't close the write end of the pipe itself, since
		// another open file descriptor (STDIN_FILENO) now refers to it.
		close(pipe[READ_END]);
		close(pipe[WRITE_END]);
	}
}

// Set the right pipe for the process calling this to the given pipe.
// If the pipe given is NULL, then no pipe is set.
void _set_pipe_write_to(int *pipe) {
	if (pipe != NULL) {
		// The process will not write out to pipe instead of stdout.
		dup2(pipe[WRITE_END], STDOUT_FILENO);

		// Good practice to close the file descriptors
		// now that we have a copy of it (dup2)
		// Note that this won't close the write end of the pipe itself, since
		// another open file descriptor (STDIN_FILENO) now refers to it.
		close(pipe[READ_END]);
		close(pipe[WRITE_END]);
	}
}

// Helper function for instantiating processes.
// Forks, executes the given process over the child process.
// Stores the information of the child process.
// Note that storing should only be done by the parent process,
// So this method should only ever be invoked by the parent.
// Returns the child's PID.

// Params:
// process_name: The name/path of the process to run.
// args: The arguments, as an array of char*. Must be NULL-terminated.
// left_pipe: The pipe that the child will read from. NULL if no pipe is to be attached.
// right_pipe: The pipe that the child will write to. NULL if no pipe is to be attached.
// service_index: The order of which the service was ran.
//                e.g. the fifth service to run on the sm will have service_index of 4 (0-indexed).
// process_index: The order of which the processes in the service were invoked.
//                e.g. if a single service has three processes, they will be given the process indices of 0, 1, 2 respectively.
// is_log_enabled: Boolean flag. If enabled, we redirect stderr to stdout, and redirect stdout to a file named service{service_index}.log.
int _run_process_and_store_information(const char *process_name, const char *args[], int *left_pipe, int *right_pipe, int service_index, int process_index, bool is_log_enabled) {
	int child_pid = fork();
	bool is_fork_successful = (child_pid >= 0);
	bool is_parent = (child_pid > 0);
	bool is_child = (child_pid == 0);
	
	if (!is_fork_successful) {
		if (debug) {
			printf("[%d][_run_process_and_store_information]: Failed to fork process.\n", getpid());
		}
		exit(1);
	}
	
	if (is_parent) {
		_store_process_information(child_pid, process_name, service_index, process_index);
		return child_pid;
		
	}
	
	if (is_child) {
		_set_pipe_read_from(left_pipe);
		_set_pipe_write_to(right_pipe);
		
		if (is_log_enabled) {
			// We allocate enough space for "service.log" up to two digits, and the NULL terminator.
			// We know two digits is enough, since we will only work with up to 32 services.
			char log_filename[(strlen("service.log") + 2 + 1) * sizeof(char)];
			sprintf(log_filename, "service%d.log", service_index);
			FILE *log_file_pointer = fopen(log_filename, "a");

			if (log_file_pointer == NULL) {
				if (debug) {
					printf("[%d][_run_process_and_store_information] Failed to open/create %s.\n", getpid(), log_filename);
					exit(1);
				}
			}

			dup2(fileno(log_file_pointer), STDOUT_FILENO);
			dup2(fileno(log_file_pointer), STDERR_FILENO);
			fclose(log_file_pointer);

			/*
			int fd = open(log_filename, O_CREAT | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);

			if (fd == 1) {
				if (debug) {
					printf("[%d][_run_process_and_store_information] Failed to open/create %s.\n", getpid(), log_filename);
				}
			}
			
			dup2(fd, fileno(stdout));
			dup2(fd, STDERR_FILENO);
			close(fd);
			*/
		}

		execv(process_name, (char * const * ) args);
		
		// We won't reach this if execv() executes correctly,
		// but this prevents the child from hanging around otherwise.
		exit(1);
	}

	// Again, we won't reach here ever, but to shut up the compiler...
	return -1;
}

// Sends a SIGTERM signal to the targeted process, if it is running.
void _terminate_process(int process_pid, int service_index, int process_index) {
	bool is_process_running = _is_process_running(process_pid, service_index, process_index);
	if (is_process_running) {
		kill(process_pid, SIGTERM);
	}
}

// Waits for the given process.
// If it is terminated, then this process returns immediately.
void _wait_for_process(int process_pid, int service_index, int process_index) {
	bool is_process_running = _is_process_running(process_pid, service_index, process_index);
	if (is_process_running) {
		// We only care about waiting for it to terminate, we don't need any options not do we care about
		// the exit code, hence the parameters NULL (for the exit code variable) and 0 (for options).
		int wait_status;
		int returned_pid = waitpid(process_pid, &wait_status, 0);
		if (returned_pid == -1) {
			if (debug) {
				printf("[%d][_wait_for_process] waitpid(%d, NULL, 0) call failed.\n", getpid(), process_pid);
			}
		}
		arr_exited[service_index][process_index] = true;
	}
}

// Use this function to any initialisation if you need to.
void sm_init(void) {
	for (int i = 0; i < SM_MAX_SERVICES; i++) {
		arr_pids[i] = malloc(sizeof(int) * SM_MAX_SERVICES);
		arr_paths[i] = malloc(sizeof(char*) * SM_MAX_SERVICES);
		arr_exited[i] = malloc(sizeof(bool) * SM_MAX_SERVICES);

		for (int j = 0; j < SM_MAX_SERVICES; j++) {
			arr_paths[i][j] = NULL;
			arr_exited[i][j] = false;
		}
	}
}

// Use this function to do any cleanup of resources.
void sm_free(void) {
	for (int i = 0; i < SM_MAX_SERVICES; i++) {
		free(arr_pids[i]);
		
		free(arr_exited[i]);
		
		for (int j = 0; j < SM_MAX_SERVICES; j++) {
			free(arr_paths[i][j]);
		}
		free(arr_paths[i]);
	}
}

// Starts all the processes as indicated in processes[].
// If the log flag is set to true, then it will redirect
// the last process's output to a file named "serviceN.log", where N
// is the number of services launched. The output will be appended to the file.
void _start_processes(const char *processes[], bool is_log_enabled) {
	// The number of processes in this service.
	int number_of_processes = _get_num_of_processes_from_sm_start_args(processes);
	
	// An array of (array of process args). For easier processing.
	char const ***process_args = _split_sm_start_args(processes);
	
	// Left and right pipe.
	// We use the right pipe during forking for IPC.
	// The left pipe serves as a temp variable to hold the right pipe
	// between creating more child processes.
	int left_pipe[2];
	int right_pipe[2];
	
	// We initialise the first output pipe:
	pipe(right_pipe);

	for (int i = 0; i < number_of_processes; i++) {
		char const **current_process_args = process_args[i];
		char const *current_process_name = current_process_args[0];
		
		// We need to connect the first child with the parent, so we handle it specially.
		bool is_first_child = (i == 0);
		bool is_last_child = (i == number_of_processes - 1);
		bool is_only_child = is_first_child && is_last_child;

		if (is_only_child) {
			// Special case, we do no piping
			_run_process_and_store_information(
				current_process_name, 
				current_process_args,
				NULL, // The left pipe to use. Since this is the only process, we don't need to redirect input.
				NULL, // The right pipe to use.
				number_of_services_launched, // The service index to use.
				i, // The process index to use.
				is_log_enabled // Flag to en/disable log.
			);
			continue;
		}

		if (is_first_child) {
			// Special case, we don't alter the left pipe.
			_run_process_and_store_information(
				current_process_name,
				current_process_args,
				NULL,
				right_pipe,
				number_of_services_launched,
				i,
				false
			);

			// We do a "relay" of moving the pipes over.
			// This allows us to pipe the children into each other.
			left_pipe[READ_END] = right_pipe[READ_END];
			left_pipe[WRITE_END] = right_pipe[WRITE_END];
			continue;
		} 

		if (is_last_child) {
			// Special case, we don't alter the right pipe.
			_run_process_and_store_information(
				current_process_name,
				current_process_args,
				left_pipe,
				NULL,
				number_of_services_launched,
				i,
				is_log_enabled
			);

			// We close these on the parent process since we won't be using them at all.
			// These only facilitate child-child communication for later children.
			close(left_pipe[READ_END]);
			close(left_pipe[WRITE_END]);
			continue;
		}
		
		// Otherwise, we're a middle child (1 <= i < processes_len - 1)

		// We make the next output pipe.
		pipe(right_pipe);

		// The previous output pipe (which is connected to the previous child's output) is now our left pipe, and connected
		// to the input of our next child. The right pipe here is connected to the child's output, and will be what we use
		// to pipe their output to the child after it.
		_run_process_and_store_information(
			current_process_name,
			current_process_args,
			left_pipe,
			right_pipe,
			number_of_services_launched,
			i,
			false
		);

		// On the parent process, we close these file descriptors since we don't use them (we're not writing anything from parent to
		// a middle child.
		close(left_pipe[READ_END]);
		close(left_pipe[WRITE_END]);

		// Doing the relay again, see above.
		left_pipe[READ_END] = right_pipe[READ_END];
		left_pipe[WRITE_END] = right_pipe[WRITE_END];			
	}
	
	// Storing the number of processes
	_store_num_of_processes(number_of_services_launched, number_of_processes);
	
	// Free process_args
	for (int j = 0; j < number_of_processes; j++) {
		char const **current_process_args = process_args[j];
		int current_process_args_len = _get_length_of_null_terminated_array(current_process_args);
		for (int k = 0; k < current_process_args_len; k++) {
			char const *current_process_arg = current_process_args[k];
			if (current_process_arg == NULL) {
				// No need to free a NULL pointer.
				continue;
			}
			// Yes, it's not needed to cast it.
			// But it silences a compiler warning.
			// Why does that warning exist? I don't know.
			free((void *) current_process_arg);
		}
		free(current_process_args);
	}
	free(process_args);
	
	// Increment the counter so that we know how many services we have.
	number_of_services_launched++;
}

// Returns false if a file with this name does not exist.
// Returns true otherwise.
// Note that this means it may return true in the case where
// we fail to open the file, but for reasons other than "the file does not exist".
bool _does_file_exist(char *filename) {
	FILE *fp = fopen(filename, "r");
	if (fp == NULL && errno == ENOENT) {
		return false;
	}
	fclose(fp);
	return true;
}
// Exercise 1a/2: start services
void sm_start(const char *(processes[])) {
	_start_processes(processes, false);
}

// Exercise 1b: print service status
size_t sm_status(sm_status_t statuses[]) {
	for (int i = 0; i < number_of_services_launched; i++) {
		// Recall we use 0-indexing for both, so we need to subtract 1 from the number of processes (which is 1-indexed).
		int process_pid = arr_pids[i][arr_num_of_processes[i] - 1];
		char *process_path = arr_paths[i][arr_num_of_processes[i] - 1];
		bool is_process_running = _is_process_running(process_pid, i, arr_num_of_processes[i] - 1);

		sm_status_t *current_status = &statuses[i];
		
		current_status -> pid = process_pid;
		current_status -> path = process_path;
		current_status -> running = is_process_running;
	}
	
	return number_of_services_launched;
}

// Exercise 3: stop service, wait on service, and shutdown
void sm_stop(size_t index) {
	int service_index = index;
	int num_of_processes = arr_num_of_processes[index];
	int *pids = arr_pids[index];
	
	// Send a SIGTERM signal to all running processes in this service.
	for (int process_index = 0; process_index < num_of_processes; process_index++) {
		int current_process_pid = pids[process_index];
		_terminate_process(current_process_pid, service_index, process_index);
	}
	
	// Wait for any non-terminated processes.
	for (int process_index_ = 0; process_index_ < num_of_processes; process_index_++) {
		int current_process_pid = pids[process_index_];
		_wait_for_process(current_process_pid, service_index, process_index_);
	}
}

void sm_wait(size_t index) {
	int service_index = index;
	int num_of_processes = arr_num_of_processes[index];
	int *pids = arr_pids[index];

	for (int process_index = 0; process_index < num_of_processes; process_index++) {
		int current_process_pid = pids[process_index];
		_wait_for_process(current_process_pid, service_index, process_index);
	}
}

void sm_shutdown(void) {
	for (int service_index = 0; service_index < number_of_services_launched; service_index++) {
		sm_stop(service_index);
	}
}

// Exercise 4: start with output redirection
void sm_startlog(const char *processes[]) {
	_start_processes(processes, true);
}

// Exercise 5: show log file
void sm_showlog(size_t index) {
	// Ideally, I wouldn't have to replicate this code from the earlier portion.
	// But extracting this into a function means I have to malloc for it.
	// That's more of a hassle than just redeclaring it here.
	// Admittedly if this were an enterprise setting and I was facing
	// having to potentially refactor the log filenames...
	char log_filename[(strlen("service.log") + 2 + 1) * sizeof(char)];
	sprintf(log_filename, "service%d.log", (int) index);
	bool is_log_file_exists = _does_file_exist(log_filename);

	if (!is_log_file_exists) {
		printf("service has no log file");
		return;
	}

	FILE *log_file_pointer = fopen(log_filename, "r");
	char c; // Yes, we're going to read the file one character at a time.
	if (log_file_pointer == NULL) {
		if (debug) {
			printf("[%d][sm_showlog] Failed to open log file.\n", getpid());
		}
		exit(1);
	}

	c = fgetc(log_file_pointer);
	while (c != EOF) {
		printf("%c", c);
		c = fgetc(log_file_pointer);
	}
	fclose(log_file_pointer);
	return;
}
