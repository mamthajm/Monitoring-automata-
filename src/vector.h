/*
 * vector.h
 *
 *  Created on: Nov 11, 2016
 *      Author: shrikanth
 *      This Vector implementation is  adopted from
 *      https://www.happybearsoftware.com/implementing-a-dynamic-array
 */

#ifndef VECTOR_H_
#define VECTOR_H_

#define VECTOR_INITIAL_CAPACITY 100

// 
struct map
{
	int key_id;
	void *value;	
};


// Define a vector type
typedef struct {
  int count;      // slots used so far
  int total_elements;  // total available rows
  struct map *elem;
} Vector;

void vector_init(Vector *vector);

void vector_append(Vector *vector, const struct map);

int vector_get(Vector *vector, int index);

//void vector_set(Vector *vector, int index, int value);

//void vector_double_capacity_if_full(Vector *vector);

void vector_free(Vector *vector);



#endif /* VECTOR_H_ */
