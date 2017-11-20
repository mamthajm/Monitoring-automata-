/*
 * lists.h
 *
 *  Created on: Nov 12, 2016

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#ifndef LISTS_H_
#define LISTS_H_

#include <stdlib.h>

struct Node{
	int key; //Monitoring ID.
	void *data; //Monitoring Parameters object.
	struct Node *nextNode;
  pthread_mutex_t lock;
};

typedef struct Node *NODE;
//NODE firstNode;

NODE getNode(unsigned int key, NODE firstNode); //Get the AgentNode by agentID
NODE createNode(); //allocates required memory for a node.
NODE removeNode(unsigned int key,NODE firstNode,int syncList);
NODE insertNode(unsigned int key,void *data,NODE firstNode,int syncList);


#endif /* LISTS_H_ */
