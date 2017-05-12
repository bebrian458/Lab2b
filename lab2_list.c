#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>	
#include <pthread.h>
#include <time.h>
#include <string.h>	// strcat()
#include <ctype.h> // isdigit()
#include <sched.h>
#include "SortedList.h"

// Global variables
int numthreads = 1, numIters = 1, opt_yield = 0, spin_lock = 0, numlists = 1, numelems = 0, listlen = 0;
char m_sync = 0;
char* m_yield;
pthread_mutex_t mutex;
SortedList_t *list;
SortedListElement_t *elements;

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

// Thread routine
void* worker(void* tID){

	// Insert elements into list

	/* Fine Grained Locking */
	// int i;
	// for(i = *(int*)tID; i < numelems; i+= numthreads){
	// 	switch(m_sync){
	//		
	// 		// Mutex
	// 		case 'm':
	// 			pthread_mutex_lock(&mutex);
	// 			SortedList_insert(list, &elements[i]);
	// 			pthread_mutex_unlock(&mutex);
	// 			break;
	//		
	// 		// Spin-lock
	// 		case 's':
	// 			while(__sync_lock_test_and_set(&spin_lock, 1))
	// 				;
	// 			SortedList_insert(list, &elements[i]);
	// 			__sync_lock_release(&spin_lock);
	// 			break;
	//
	// 		// Without locks
	// 		default:
	// 			SortedList_insert(list, &elements[i]);
	// 			break;
	// 	}
	// }

	/* Course Grained Locking */
	switch(m_sync){
	
	// Mutex
	case 'm':
		pthread_mutex_lock(&mutex);
		break;
	
	// Spin-lock
	case 's':
		while(__sync_lock_test_and_set(&spin_lock, 1))
			;
		break;

	// Without locks
	default:
		break;
	}

	int i;
	for(i = *(int*)tID; i < numelems; i+= numthreads)
		SortedList_insert(list, &elements[i]);

	// Get the list length
	listlen = SortedList_length(list);
	
	// Check if the length of list is zero
	if(listlen == -1){
		fprintf(stderr, "Error: List length is corrupted after insertion; it is: %d\n",listlen);
		exit(2);
	}

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


	// Course Grained Locking
	int j;
	for(j = *(int*)tID; j < numelems; j+= numthreads)
		SortedList_delete(SortedList_lookup(list, elements[j].key));

	switch(m_sync){
	
		// Mutex
		case 'm':
			pthread_mutex_unlock(&mutex);
			break;
		
		// Spin-lock
		case 's':
			__sync_lock_release(&spin_lock);
			break;

		// Without locks
		default:
			break;
	}


	// Get the list length
	listlen = SortedList_length(list);

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
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "t:i:y:s:", longopts, NULL)) != -1){
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
			case 'y':
				m_yield = optarg;
				check_yield();
				break;
			case 's':
				m_sync = *optarg;
				check_sync();
				break;
			default:
				fprintf(stderr, "Usage: ./lab1b --threads=numthreads --iterations=numIters --yield=[ild] --sync=[ms]\n");
				exit(1);
				break;
		}
	}

	// Initialize mutex
	if(m_sync == 'm')
		pthread_mutex_init(&mutex, NULL);

	// Initialize empty list
	list = malloc(sizeof(SortedList_t));
	if(list == NULL){
		fprintf(stderr, "Error allocating memory for list\n");
		exit(1);
	}
	list->key = NULL;
	list->next = list;
	list->prev = list;

	// Create and initialize with random keys the required number of list elements
	numelems = numthreads * numIters;
	elements = malloc(numelems * sizeof(SortedListElement_t));
	if(elements == NULL){
		fprintf(stderr, "Error allocating memory for elements\n");
		exit(1);
	}
	make_keys();

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

	// Create threads
	int i;
	for(i = 0; i < numthreads; i++){
		tIDs[i] = i;
		if(pthread_create(threads+i, NULL, worker, &tIDs[i]) != 0){
			fprintf(stderr, "Error creating threads\n");

			// Free memory before exiting
			free(tIDs);
			free(list);
			free(elements);
			free(threads);
			exit(1);
		}
	}

	// Join threads
	for(i = 0; i < numthreads; i++){
		if(pthread_join(*(threads+i), NULL) != 0){
			fprintf(stderr, "Error joining threads\n");

			// Free memory before exiting
			free(tIDs);
			free(list);
			free(elements);
			free(threads);
			exit(1);
		}
	}

	// Stop timer
	if(clock_gettime(CLOCK_MONOTONIC, &end) == -1){
		fprintf(stderr, "Error stopping timer\n");
		exit(1);
	}

	// Free allocated memory
	free(tIDs);
	free(list);
	free(elements);
	free(threads);

	// Check if the length of list is zero
	if(listlen != 0){
		fprintf(stderr, "Error: List length is corrupted after deletion; it is: %d\n",listlen);
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
	fprintf(stdout, "%s,%d,%d,%d,%d,%lld,%lld\n",
		message,numthreads,numIters,numlists,numops,total_time,avg_time_per_op);

	exit(0);
}