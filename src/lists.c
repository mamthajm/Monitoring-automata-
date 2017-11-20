/*
 * lists.c
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

#include "lists.h"
#include <pthread.h>



NODE createNode(int syncList)
{
	struct Node *temp;
	temp = (NODE) malloc(sizeof(NODE));
	if (temp != NULL) {
		if(syncList)
		{
		  pthread_mutex_init(&(temp->lock),NULL);
		}
		return temp;
	}
	return NULL;
}

NODE getNode(unsigned int key, NODE firstNode)
{
	NODE temp;
	temp = firstNode;
	if(temp!=NULL){
		for (;temp ->nextNode != NULL; temp = temp->nextNode) {
			if (temp->key == key) {
				return temp;
			}
		}
	}
	return NULL;
}

//Inserts elements at the end of the list.
//In parameter- syncList: Pass 1 for synchronized list.
NODE insertNode(unsigned int key,void *data,NODE firstNode, int syncList)
{
	 NODE cur = firstNode;
	 NODE newNode;

     if (cur!= NULL) {
		while (cur -> nextNode != NULL)
		{
			cur = cur -> nextNode;
		}
		if(syncList)
			pthread_mutex_lock(&cur->lock);   
		
		newNode = createNode(syncList);	
		newNode->data = (void*)data; //Monitoring parameters in our Context.
		newNode->key = key; //AgendID in our context
		cur->nextNode = newNode;
		
		if(syncList)
			pthread_mutex_unlock(&cur->lock);
		
		newNode->nextNode = NULL;
		return firstNode;
     } 
     else {
			firstNode = createNode(syncList);
			firstNode -> nextNode = NULL;
			firstNode -> data = (void*)data;
			firstNode -> key = key;
			return firstNode;
     }
}

int search(unsigned int key,NODE firstNode)
{
    int i=0;
    NODE temp;
    temp=firstNode;
    while(temp->nextNode!=NULL)
    {
        i++;
        if(key==temp->key)
        {
            return i;
        }
        temp=temp->nextNode;
    }
    return 0;
}

NODE removeNode(unsigned int key, NODE firstNode,int syncList)
{

	NODE prev, current;
	prev = firstNode;


	if(firstNode->nextNode == NULL)
	{
		//Only one node present.
		free(firstNode->data);
		free(firstNode);
		firstNode=NULL;
		return firstNode;
	}
	if(syncList)
		pthread_mutex_lock(&prev->lock); 

	pthread_mutex_lock(&prev->lock); 
	while ((current = prev->nextNode) != NULL) 
	{ 
		pthread_mutex_lock(&current->lock); 
		if (current->key == key) 
		{ 
			prev->nextNode = current->nextNode;
			
			if(syncList)
			{
				pthread_mutex_unlock(&current->lock); 
				pthread_mutex_unlock(&prev->lock);
			}
			//current->nextNode = NULL; 
			free(current->data);
			current->data = NULL;
			free(current);
			return(firstNode); 
		} 
		if(syncList)
		{
			pthread_mutex_unlock(&prev->lock);
		}		
		prev = current; 
	} 
	if(syncList)
		pthread_mutex_unlock(&prev->lock); 
	return(firstNode);
}
