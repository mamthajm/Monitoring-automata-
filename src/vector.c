/*
 * vector.c
 *
 *  Created on: Nov 11, 2016
 *      Author: shrikanth
 */


#include <stdio.h>
#include <stdlib.h>
#include "vector.h"


//Below Vector can be further improvised by implementing a sort on the vector elements
//after ADD/REMOVE operations.

void vector_init(Vector *vector) {
  // initialize size and capacity
  vector->count = 0;
  vector->total_elements = VECTOR_INITIAL_CAPACITY;
  // allocate memory for vector->data
  vector->elem = (struct map*)malloc(sizeof(struct map) * (VECTOR_INITIAL_CAPACITY));
}

void vector_append(Vector *vector, const struct map map_elem) {
 	// append the value and increment vector->size
  	for(int i = 0; i <= vector->count; i++)
	{
		if(vector->elem[i].key_id == 0xFFFF)
		{
	      printf("\n Inserted element in to the vector here: %d \n",map_elem.key_id);
		  vector->elem[i].key_id = map_elem.key_id;
		  vector->elem[i].value = map_elem.value;
		  return;		
		}
	}
	// At this point, there was no empty space found, so insert the element at the end.
    if(vector->count < vector->total_elements)
    {
      printf("\n Inserted element in to the vector: %d \n",map_elem.key_id);
	  vector->elem[vector->count].key_id = map_elem.key_id;
	  vector->elem[vector->count].value = map_elem.value;
	  vector->count++;
    } 
}

int vector_remove(Vector *vector, int key_id) {
  // Returns 1 if there exists a key_id, else returs 0.
	for(int i = 0; i <= vector->count; i++)
	{
		if(vector->elem[i].key_id == key_id)
		{
			printf("\n vector->coun:%d  vector_remove: Found the key %d \n",vector->count,key_id);
			vector->elem[i].key_id = 0xFFFF;
			vector->elem[i].value = NULL;
			return 1;
		}
	}
	return 0;
}

void* vector_get_value_by_key(Vector *vector, int key_id)
{
	//There might be memory locations that are not valid due to vector_remove operations.
	//vector->count only gives the number of used (valid and invalid both) vectors out of VECTOR_INITIAL_CAPACITY.
	for(int i = 0; i <= vector->count; i++)
	{
		if(vector->elem[i].key_id == key_id)
		{
			  printf("\nvector_get_value_by_key: Found the key %d \n",key_id);
			  return (vector->elem[i].value);
		}

	}
	return NULL;
}

int vector_get_valid_count(Vector *vector)
{
	int count = 0;
	for(int i = 0; i <= vector->count; i++)
	{
		if(vector->elem[i].key_id != 0xFFFF)
			count++;
	}
	return count;
}
