#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>	
#include <pthread.h>
#include <time.h>
#include <string.h>	// strcat()
#include <ctype.h> // isdigit()
#include <sched.h>
#include "SortedList.h"

// Each sublist will have its own mutex or spinlock 
typedef struct{
	SortedList_t list;
	pthread_mutex_t mutex;
	int spin_lock;
} SubList_t;

// Global variables
int numthreads = 1, numIters = 1, opt_yield = 0, spin_lock = 0, numlists = 1, numelems = 0; //listlen = 0;
char m_sync = 0;
char* m_yield;
pthread_mutex_t mutex;
SubList_t *sublist_arr;
SortedListElement_t *elements;
long long *locktimers;

#define MAX_KEY_LEN 15
#define MIN_KEY_LEN 8
#define MAX_NUM_LETTERS 26 // 26 for one case, 52 for both upper and lower


// Check for correct sync arguments
void check_sync(){
	if(m_sync != 'm' && m_sync != 's'){
		fprintf(stderr, "Incorrect argument for sync. Use m for mutex, s for spin-lock\n");
		exit(1);
	}
}

// Check for correct yield arguments
void check_yield(){

	char curr = *m_yield;
	int index = 0;
	while(curr != '\0'){
		if(curr == 'i')
			opt_yield |= INSERT_YIELD;
		else if(curr == 'd')
			opt_yield |= DELETE_YIELD;
		else if(curr == 'l')
			opt_yield |= LOOKUP_YIELD;
		else{
			fprintf(stderr, "Incorrect argument for yield. Use i for insertion, s for deletion, l for lookups\n");
			exit(1);
		}
		index++;
		curr = *(m_yield+index);
	}
}

void make_keys(){

	// Set the seed to be based on time
	// Ensure that the seed changes every time the application is run
	srand(time(NULL));

	// Generate a random key for each element
	int i, j;
	for(i = 0; i < numelems; i++){

		// Get random data
		int rand_len = rand() % MAX_KEY_LEN + MIN_KEY_LEN;
		int rand_letter;

		// Create key with data
		char* rand_key = malloc((rand_len+1) * sizeof(char));
		for(j = 0; j < rand_len; j++){
			rand_letter  = rand() % MAX_NUM_LETTERS;
			rand_key[j] = 'a'+ rand_letter;
		}

		// Cap the key with a zero byte
		rand_key[rand_len] = '\0';

		// Set element's key to rand_key
		elements[i].key = rand_key;

		// Free allocated memory
		free(rand_key);
	}
}

// djb2 Hash function by Dan Bernstein
// source: http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash(const char *str){
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

// Thread routine
void* worker(void* tID){

	// Lock timers
	struct timespec lock_start, lock_end;

	SubList_t *sublist;
	pthread_mutex_t *curr_mutex;
	int *curr_spin_lock;

	// Insert elements into list

//	fprintf(stderr, "Before insertion\n");

	/* Fine Grained Locking */
	int i;
	for(i = *(int*)tID; i < numelems; i+= numthreads){
		sublist = &sublist_arr[hash(elements[i].key) % numlists];
		switch(m_sync){
			
			// Mutex
			case 'm':
				curr_mutex = &sublist->mutex;
				pthread_mutex_lock(curr_mutex);
				SortedList_insert(&sublist->list, &elements[i]);
				pthread_mutex_unlock(curr_mutex);
				break;
			
			// Spin-lock
			case 's':
				curr_spin_lock = &sublist->spin_lock;
				while(__sync_lock_test_and_set(curr_spin_lock, 1))
					;
				SortedList_insert(&sublist->list, &elements[i]);
				__sync_lock_release(curr_spin_lock);
				break;
	
			// Without locks
			default:
//				fprintf(stderr, "Inside insertion, thread %d\n", i);
				SortedList_insert(&sublist->list, &elements[i]);
				break;
		}
	}

//	fprintf(stderr, "After insertion\n");

	/* Course Grained Locking */
	// switch(m_sync){
	//
	// // Mutex
	// case 'm':
	//	
	// 	// Time the wait for thread to acquire mutex
	// 	if(clock_gettime(CLOCK_MONOTONIC, &lock_start) == -1){
	// 		fprintf(stderr, "Error getting lock start time\n");
	// 		exit(1);
	// 	}
	// 	pthread_mutex_lock(&mutex);
	// 	if(clock_gettime(CLOCK_MONOTONIC, &lock_end) == -1){
	// 		fprintf(stderr, "Error getting lock end time\n");
	// 		exit(1);
	// 	}
	//
	// 	// Calculate wait time
	// 	long long wait_time = 1000000000 * (lock_end.tv_sec - lock_start.tv_sec) + (lock_end.tv_nsec - lock_start.tv_nsec);
	// 	locktimers[*(int*)tID] = wait_time;
	//
	// 	break;
	//
	// // Spin-lock
	// case 's':
	// 	while(__sync_lock_test_and_set(&spin_lock, 1))
	// 		;
	// 	break;
	//
	// // Without locks
	// default:
	// 	break;
	// }
	//
	// int i;
	// for(i = *(int*)tID; i < numelems; i+= numthreads)
	// 	SortedList_insert(list, &elements[i]);

	// Get the list length

//	fprintf(stderr, "Before listlen\n");

	/* Fine Grained Locking */
	int listlen = 0, k = 0, ret = 0;
	switch(m_sync){
		
		// Mutex
		case 'm':

			// First, acquire all of the locks
			for(k = 0; k < numlists; k++){
				curr_mutex = &sublist_arr[k].mutex;
				pthread_mutex_lock(curr_mutex);
			}

			// Then, safely iterate the list without sudden updates
			for(k = 0; k < numlists; k++){
				ret = SortedList_length(&sublist_arr[k].list);
				if(ret < 0){
					fprintf(stderr, "One or more lists is corrupted after insertion\n");
					exit(1);
				}
				listlen += ret;
			}

			// Lastly, release all of the locks
			for(k = 0; k < numlists; k++){
				curr_mutex = &sublist_arr[k].mutex;
				pthread_mutex_unlock(curr_mutex);
			}

		// Spin-lock
		case 's':

			// First, acquire all of the locks
			for(k = 0; k < numlists; k++){
				curr_spin_lock = &sublist_arr[k].spin_lock;
				while(__sync_lock_test_and_set(curr_spin_lock, 1))
					;
			}

			// Then safely iterate the list without sudden updates
			for(k = 0; k < numlists; k++){
				ret = SortedList_length(&sublist_arr[k].list);
				if(ret < 0){
					fprintf(stderr, "One or more lists is corrupted after insertion\n");
					exit(1);
				}
				listlen += ret;
			}

			// Lastly, release all of the locks
			for(k = 0; k < numlists; k++){
				curr_spin_lock = &sublist_arr[k].spin_lock;
				__sync_lock_release(curr_spin_lock);
			}

		// Without locks
		default:
//			fprintf(stderr, "Inside listlen\n");
			for(k = 0; k < numlists; k++){

//				fprintf(stderr, "Inside listlen forloop, k: %d\n", k);
				
				ret = SortedList_length(&sublist_arr[k].list);
				if(ret < 0){
					fprintf(stderr, "One or more lists is corrupted after insertion\n");
					exit(1);
				}
				listlen += ret;
			}
	}

//	fprintf(stderr, "After listlen\n");

	/* Course Grained Locking */
	// listlen = SortedList_length(list);
	//
	// // Check if the length of list is zero
	// if(listlen == -1){
	// 	fprintf(stderr, "Error: List length is corrupted after insertion; it is: %d\n",listlen);
	// 	exit(2);
	// }

	// Look up and delete each of the keys it had previously inserted
	
   	/* Fine Grained Locking */
	// int j;
	// for(j = *(int*)tID; j < numelems; j+= numthreads){
	// 	switch(m_sync){
	//
	// 		// Mutex
	// 		case 'm':
	// 			pthread_mutex_lock(&mutex);
	// 			SortedList_delete(SortedList_lookup(list, elements[j].key));
	// 			pthread_mutex_unlock(&mutex);
	//
	// 		// Spin-lock
	// 		case 's':
	// 			while(__sync_lock_test_and_set(&spin_lock, 1))
	// 				;
	// 			SortedList_delete(SortedList_lookup(list, elements[j].key));
	// 			__sync_lock_release(&spin_lock);
	//
	// 		// Without locks
	// 		default:
	// 			SortedList_delete(SortedList_lookup(list, elements[j].key));
	// 			break;
	// 	}
	// }

//	fprintf(stderr, "Before delete\n");

	int j;
	for(j = *(int*)tID; j < numelems; j+= numthreads){

//		fprintf(stderr, "Before sublist\n");

		sublist = &sublist_arr[hash(elements[j].key) % numlists];
		SortedListElement_t *ret_elem;

//		fprintf(stderr, "After sublist\n");
		switch(m_sync){
			
			// Mutex
			case 'm':
				curr_mutex = &sublist->mutex;
				pthread_mutex_lock(curr_mutex);
				ret_elem = SortedList_lookup(&sublist->list, elements[j].key);
				if(ret_elem == NULL){
					fprintf(stderr, "Error looking up element for deletion\n");
					exit(1);
				}
				if(SortedList_delete(ret_elem) != 0){
					fprintf(stderr, "Error in attempt to delete an element\n");
					exit(1);
				}
				pthread_mutex_unlock(curr_mutex);
				break;
			
			// Spin-lock
			case 's':
				curr_spin_lock = &sublist->spin_lock;
				while(__sync_lock_test_and_set(curr_spin_lock, 1))
					;
				ret_elem = SortedList_lookup(&sublist->list, elements[j].key);
				if(ret_elem == NULL){
					fprintf(stderr, "Error looking up element for deletion\n");
					exit(1);
				}
				if(SortedList_delete(ret_elem) != 0){
					fprintf(stderr, "Error in attempt to delete an element\n");
					exit(1);
				}
				__sync_lock_release(curr_spin_lock);
				break;
	
			// Without locks
			default:
//				fprintf(stderr, "Inside delete\n");

				ret_elem = SortedList_lookup(&sublist->list, elements[j].key);
				if(ret_elem == NULL){
					fprintf(stderr, "Error looking up element for deletion\n");
					exit(1);
				}
				if(SortedList_delete(ret_elem) != 0){
					fprintf(stderr, "Error in attempt to delete an element\n");
					exit(1);
				}
				break;
		}
	}

//	fprintf(stderr, "After delete\n");

	// Course Grained Locking
	// int j;
	// for(j = *(int*)tID; j < numelems; j+= numthreads)
	// 	SortedList_delete(SortedList_lookup(list, elements[j].key));
	//
	// switch(m_sync){
	//
	// 	// Mutex
	// 	case 'm':
	// 		pthread_mutex_unlock(&mutex);
	// 		break;
	//	
	// 	// Spin-lock
	// 	case 's':
	// 		__sync_lock_release(&spin_lock);
	// 		break;
	//
	// 	// Without locks
	// 	default:
	// 		break;
	// }


	// Get the list length
	// listlen = SortedList_length(list);

	return NULL;
}

// Main routine 
int main(int argc, char *argv[]){

	// Default values
	int opt = 0;
	long long counter = 0;

	// Timer structs
	struct timespec start, end;

	struct option longopts[] = {
		{"threads", 	required_argument, 	NULL, 't'},
		{"iterations", 	required_argument, 	NULL, 'i'},
		{"yield", 		required_argument, 	NULL, 'y'},
		{"sync", 		required_argument, 	NULL, 's'},
		{"lists",		required_argument, 	NULL, 'l'},
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "t:i:y:s:l:", longopts, NULL)) != -1){
		switch(opt){
			case 't':
				numthreads = atoi(optarg);
				if(!isdigit(*optarg)){
					fprintf(stderr, "Incorrect argument for threads. Input an integer or use default of value 1\n");
					exit(1);
				}
				break;
			case 'i':
				numIters = atoi(optarg);
				if(!isdigit(*optarg)){
					fprintf(stderr, "Incorrect argument for iterations. Input an integer or use default of value 1\n");
					exit(1);
				}
				break;
			case 'l':
				numlists = atoi(optarg);
				if(!isdigit(*optarg)){
					fprintf(stderr, "Incorrect argument for lists. Input an integer or use default of value 1\n");
					exit(1);
				}
				break;
			case 'y':
				m_yield = optarg;
				check_yield();
				break;
			case 's':
				m_sync = *optarg;
				check_sync();
				break;
			default:
				fprintf(stderr, "Usage: ./lab1b --threads=numthreads --iterations=numIters --yield=[ild] --sync=[ms] --lists=numlists\n");
				exit(1);
				break;
		}
	}

	// Allocate memory for array of sublists
	sublist_arr = malloc(numlists*sizeof(SubList_t));
	if(sublist_arr == NULL){
		fprintf(stderr, "Error allocating memory for sublist array\n");
		exit(1);
	}

	// Initialize sublists' lists and locks
	int l;
	for(l = 0; l < numlists; l++){
		SubList_t *sublist = &sublist_arr[l];
		SortedList_t *list = &sublist->list;
		list->key = NULL;
		list->next = list;
		list->prev = list;
		if(m_sync == 'm'){
			if(pthread_mutex_init(&sublist->mutex, NULL)){
				fprintf(stderr, "Error initializing mutex for sublist %d\n", l);
				exit(1);
			}
		}
		if(m_sync == 's'){
			sublist->spin_lock = 0;
		}
	}

	// Create and initialize with random keys the required number of list elements
	numelems = numthreads * numIters;
	elements = malloc(numelems * sizeof(SortedListElement_t));
	if(elements == NULL){
		fprintf(stderr, "Error allocating memory for elements\n");
		exit(1);
	}
	make_keys();

	// Allocate memory for a lock timer for each thread
	locktimers = malloc(numthreads*sizeof(long long));
	if(locktimers == NULL){
		fprintf(stderr, "Error allocating memory for locktimers\n");
		exit(1);
	}

	// Allocate memory for threads
	pthread_t *threads = malloc(numthreads*sizeof(pthread_t));
	if(threads == NULL){
		fprintf(stderr, "Error allocating memory for threads\n");
		exit(1);
	}

	// Allocate memory for thread IDs
	int* tIDs = malloc(numthreads * sizeof(int));
	if(tIDs == NULL){
		fprintf(stderr, "Error allocating memory for thread IDs\n");
	}

	// Start timer
	if(clock_gettime(CLOCK_MONOTONIC, &start) == -1){
		fprintf(stderr, "Error starting timer\n");
		exit(1);
	}

//	fprintf(stderr, "Before create threads\n");

	// Create threads
	int i;
	for(i = 0; i < numthreads; i++){
		tIDs[i] = i;
		if(pthread_create(threads+i, NULL, worker, &tIDs[i]) != 0){
			fprintf(stderr, "Error creating threads\n");

			// Free memory before exiting
			free(tIDs);
			free(sublist_arr);
			free(elements);
			free(threads);
			free(locktimers);
			exit(1);
		}
	}

//	fprintf(stderr, "After create threads\n");

	// Join threads
	for(i = 0; i < numthreads; i++){
		if(pthread_join(*(threads+i), NULL) != 0){
			fprintf(stderr, "Error joining threads\n");

			// Free memory before exiting
			free(tIDs);
			free(sublist_arr);
			free(elements);
			free(threads);
			free(locktimers);
			exit(1);
		}
	}

//	fprintf(stderr, "After join threads\n");

	// Stop timer
	if(clock_gettime(CLOCK_MONOTONIC, &end) == -1){
		fprintf(stderr, "Error stopping timer\n");
		exit(1);
	}

	// Check if the length of list is zero
	// if(listlen != 0){
	// 	fprintf(stderr, "Error: List length is corrupted after deletion; it is: %d\n",listlen);
	// 	exit(2);
	// }
	int listlen = 0, ret = 0;
	for(i = 0; i < numlists; i++){
		ret = SortedList_length(&sublist_arr[i].list);
		if(ret == -1){
			fprintf(stderr, "One or more lists is corrupted after deletion\n");

			// Free memory before exiting
			free(tIDs);
			free(sublist_arr);
			free(elements);
			free(threads);
			free(locktimers);
			exit(1);
		}
		listlen += ret;
	}
	if(listlen != 0){
		fprintf(stderr, "Error: Final list length is not 0; it is %d\n", listlen);

		// Free memory before exiting
		free(tIDs);
		free(sublist_arr);
		free(elements);
		free(threads);
		free(locktimers);
		exit(2);	
	}

	// Calculations
	int numops = numthreads * numIters * 3;
	long long total_time = 1000000000 * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);
	long long avg_time_per_op = total_time/numops;
	
	// Create name
	char message[15] = "list";
	
	// Add yield tags
	if(!opt_yield){
		const char name[6] = "-none";
		strcat(message, name);
	}
	else{
		const char dash[2] = "-";
		strcat(message, dash);
		if(opt_yield & INSERT_YIELD){
			const char name[2] = "i";
			strcat(message, name);
		}
		if(opt_yield & DELETE_YIELD){
			const char name[2] = "d";
			strcat(message, name);
		}
		if(opt_yield & LOOKUP_YIELD){
			const char name[2] = "l";
			strcat(message, name);
		}
	}

	// Add sync tags
	if(m_sync == 'm'){
		const char name[3] = "-m";
		strcat(message, name);
	}
	else if(m_sync == 's'){
		const char name[3] = "-s";
		strcat(message, name);
	}
	else{
		const char name[6] = "-none";
		strcat(message, name);
	}

	// Print CSV
	fprintf(stdout, "%s,%d,%d,%d,%d,%lld,%lld",
		message,numthreads,numIters,numlists,numops,total_time,avg_time_per_op);

	// Calculations for avg mutex acquiring time
	if(m_sync == 'm'){
		long long total_wait_time = 0;
		int k = 0;
		for(k = 0; k < numthreads; k++){
			total_wait_time += locktimers[k];
		}
		long long avg_wait_per_lock = total_wait_time/numthreads; // Each thread only had one lock op
		fprintf(stdout, ",%lld", avg_wait_per_lock);
	}

	// End of message
	fprintf(stdout, "\n");

	// Free allocated memory
	free(tIDs);
	free(sublist_arr);
	free(elements);
	free(threads);
	free(locktimers);

	exit(0);
}
