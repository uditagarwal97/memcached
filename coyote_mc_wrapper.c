// Make sure this file is included in memcached.c before coyote_mc_wrapper.h file is included
#ifndef COYOTE_MC_WRAP

#include <include_coyote/include/coyote_mc_wrapper.h>
#include <pthread.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

// Declarations of test methods. These methods should be implemented by the test case.
bool CT_is_socket(int);
ssize_t CT_socket_read(int, const void*, int);
ssize_t CT_socket_write(int,  void*, int);
ssize_t CT_socket_recvmsg(int, struct msghdr*, int);
int CT_new_socket();
ssize_t CT_socket_sendto(int, void*, size_t, int, struct sockaddr*,
       socklen_t*);

// Main function of the test case
int CT_main( int (*run_iteration)(int, char**), void (*reset_gloabs)(void), int argc, char** argv );

// Temporary data structure used for passing parameteres to pthread_create
typedef struct pthread_create_params{
	void *(*start_routine) (void *);
	void* arg;
} pthread_c_params;

// This function will be called in pthread_create
void *coyote_new_thread_wrapper(void *p){

	FFI_create_operation((long unsigned)pthread_self());
	FFI_start_operation((long unsigned)pthread_self());

	pthread_c_params* param = (pthread_c_params*)p;

	((param->start_routine))(param->arg);

	FFI_complete_operation((long unsigned)pthread_self());

	return NULL;
}

// Call our wrapper function instead of original parameters to pthread_create
int FFI_pthread_create(void *tid, void *attr, void *(*start_routine) (void *), void* arguments){

	// This assert can trigger in create_worker() becoz they use non-null attributes,
	// though they use only defaults attributes
	// assert(attr == NULL && "We don't yet support using custom attributes");

	pthread_c_params *p = (pthread_c_params *)malloc(sizeof(pthread_c_params));
	p->start_routine = start_routine;
	p->arg = arguments;

	return pthread_create(tid, attr, coyote_new_thread_wrapper, (void*)p);
}

static bool stats_state_read = true;
static bool stats_state_write = true;

void FFI_check_stats_data_race(bool isWrite){

	static int num_readers = 0;

	if(isWrite){

		assert(stats_state_write == true); // Make sure no one is reading this
		stats_state_write = false;
		stats_state_read = false;

		FFI_schedule_next();

		stats_state_write = true;
		stats_state_read  = true;

	} else{

		assert(stats_state_read == true);
		stats_state_write = false;
		num_readers++;

		FFI_schedule_next();

		num_readers--;
		// If all the readers have finished reading
		if(num_readers == 0){
			stats_state_write = true;
		}
	}
}

int FFI_pthread_join(pthread_t tid, void* arg){

	FFI_join_operation((long unsigned) tid); // This is a machine & OS specific hack
	return pthread_join(tid, arg);
}

static int *stop_main = NULL;
void FFI_register_main_stop(int *flag){
	stop_main = flag;
}

int FFI_accept(int sfd, void* addr, void* addrlen){

	int retval = CT_new_socket();

	if(retval < 0){

		*stop_main = 2;
		return retval;
	}

	assert(retval >= 200 && "Please use fds > 200 as others are reserved");
	return retval;
}

// Dummy connection!
int FFI_getpeername(int sfd, void* addr, void* addrlen){

	struct sockaddr_in6 *sockaddr = (struct sockaddr_in6 *)addr;
	sockaddr->sin6_family = AF_INET;
    sockaddr->sin6_port = 8080;
    inet_pton(AF_INET, "192.0.2.33", &(sockaddr->sin6_addr));

    FFI_schedule_next();

    return 0;
}

int *global_pipes = NULL;
static int FFI_pipe_counter = 0;

int FFI_pipe(int pipes[2]){

	int retval = pipe(pipes);

	if(FFI_pipe_counter == 0){
		// There can be at max 1000 pipes
		global_pipes = (int*)malloc(sizeof(int)*1000);
		memset(global_pipes, -1, 1000);

		FFI_pipe_counter++;
	}

	assert(global_pipes[ pipes[1] ] == -1 && "2 pipes with same fds?");
	global_pipes[ pipes[1] ] = pipes[0];

	return retval;
}

// No need to wait on a socket
int FFI_poll(struct pollfd *fds, nfds_t nfds, int timeout){

	FFI_schedule_next();

	fds->revents = POLLOUT;

	return 1;
}

ssize_t FFI_write(int sfd, const void* buff, size_t count){

	FFI_clock_handler();
	ssize_t retval = -1;
	FFI_schedule_next();

	// If you are tying to write to a pipe
	if(global_pipes[sfd] != -1){

		retval = FFI_event_write(sfd, buff, count, global_pipes[sfd]);
	}
	else{

		retval = FFI_event_write(sfd, buff, count, -1);
	}

	// Check if you are tying to signal any event
	if(retval >= 0){

		return retval;
	}
	else {

		// Means that you are trying to write to a socket
		if(CT_is_socket(sfd)){

			return CT_socket_read(sfd, buff, count);
		}
		else {

			// You are trying to write to an actual file
			return write(sfd, buff, count);
		}
	}
}

ssize_t FFI_sendmsg(int sfd, struct msghdr *msg, int flags){

	FFI_schedule_next();
	FFI_clock_handler();

    return CT_socket_recvmsg(sfd, msg, flags);
}

int FFI_fcntl(int fd, int cmd, ...){
	return 1;
}

ssize_t FFI_read(int fd, void* buff, int count){

	FFI_schedule_next();
	FFI_clock_handler();

	if(!CT_is_socket(fd)){

		// You are trying to read from an event fd
		return read(fd, buff, count);
	} else {

		return CT_socket_write(fd, buff, count);
	}
}

ssize_t FFI_recvfrom(int socket, void* buffer, size_t length,
       int flags, struct sockaddr* address,
       socklen_t* addr_len){

	FFI_schedule_next();
	return CT_socket_sendto(socket, buffer, length, flags, address, addr_len);
}

void reset_all_globals(){

	reset_logger_globals();
	reset_memcached_globals();
	reset_thread_globals();
	reset_assoc_globals();
	reset_crawler_globals();
	reset_items_globals();
	reset_slabs_globals();
	// For resetting libevent mock DS
	FFI_event_reset_all();
}

// Delegate the functionality to the test main method
int main(int argc, char **argv){

 	return CT_main( &run_coyote_iteration, &reset_all_globals, argc, argv );
}

#endif /* COYOTE_MC_WRAP */