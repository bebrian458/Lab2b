#include "SortedList.h"
#include <string.h>
#include <sched.h>


void SortedList_insert(SortedList_t *list, SortedListElement_t *element){

	// If there is no list or no element to put in list, do nothing
	if(list == NULL || element == NULL)
		return;

	// Start from where head's (list) next is pointing to
	SortedListElement_t *curr = list->next;
	
	// Iterate through list until element can be placed in correct position
	// or until we loop back to the head
	while(curr != list){

		// If the element value is less than the curr value
		if(strcmp(element->key, curr->key) < 0){
			
			// We found the correct position
			break; 
		}

		// Otherwise, move on to the next thing in the list
		curr = curr->next;
	}

	if(opt_yield & INSERT_YIELD)
		sched_yield();

	// Insert the element between the curr value and the prev value
	// If curr == list, the element must have the largest value in the list
	// Insert it at the end
	element->next = curr;
	element->prev = curr->prev;
	curr->prev->next = element;
	curr->prev = element;
}

int SortedList_delete(SortedListElement_t *element){

	// If there is no element to delete, do nothing
	if(element == NULL)
		return 0;

	// Check to make sure pointers are not corrupted
	if(element->prev->next == element->next->prev){

		if(opt_yield & DELETE_YIELD)
			sched_yield();

		// Remove element from the list
		element->next->prev = element->prev;
		element->prev->next = element->next;
		return 0;
	}

	// Pointers are corrupted, unsuccessful deletion
	return 1;
}

SortedListElement_t *SortedList_lookup(SortedList_t *list, const char *key){

	// If there is no list or no key, do nothing
	if(list == NULL || key == NULL)
		return NULL;

	// Start from where head's (list) next is pointing to
	SortedListElement_t *curr = list->next;
	
	// Iterate through list until key is found
	// or until we loop back to the head
	while(curr != list){

		// If an element has a matching key
		if(strcmp(curr->key, key) == 0)
			return curr;

		if(opt_yield & LOOKUP_YIELD)
			sched_yield();

		// Otherwise, move on to the next thing in the list
		curr = curr->next;
	}

	// If we looped back to the head, we did not find an element with matching key
	return NULL;
}

int SortedList_length(SortedList_t *list){

	// Default is no elements yet counted
	int counter = 0;

	// If there is no list, do nothing
	if(list == NULL || list->key != NULL)
		return counter;

	// Start from where head's (list) next is pointing to
	SortedListElement_t *curr = list->next;
	
	// Iterate through list until we find corruption in list
	// or until we loop back to the head
	while(curr != list){

		// Check for corrupted pointers around curr element
		if(curr->prev->next != curr->next->prev)
			return -1;

		if(opt_yield & LOOKUP_YIELD)
			sched_yield();

		// Update counter
		counter++;

		// Otherwise, move on to the next thing in the list
		curr = curr->next;
	}

	// If we looped back to the head, there was no corruption
	return counter;

}









