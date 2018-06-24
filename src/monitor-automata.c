/***************************************************************************
 *  Copyright [2016] [TU Darmstadt]
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 *   File: monitor-automata.c
 *   Purpose: this file contains SDN Monitoring automata process related code.
 ****************************************************************************/
 
#include <zmq.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <error.h>
#include "zhelpers.h"
#include "monitor-automata.h"
#include <libxml/xmlreader.h>
#include <lists.h>
#include <czmq.h>



static uint64 total_poll_intervals=0;


uint8  send_switch_notification = true;
struct tcam_thresholds_t tcam_thresholds;

///HHH MSG
struct port_data_stats port_stats;
device_identification_nr = -1;

static int tcam_timer_interval = 60000;
static int tcam_timer_id;

opennsl_field_group_t fp_mon_stats_grps = -1;

static unsigned int call_count = 0;

struct hmap agent_hmap = HMAP_INITIALIZER(&agent_hmap); //HashMap container for storing all the agents configured from the SDN controller.

opennsl_field_stat_t lu_stat_ifp[2]= {opennslFieldStatPackets, opennslFieldStatBytes};

opennsl_stat_val_t stat_arr[MAX_STAT_COUNTERS] =   //Do not change the order, same order is hard-coded in validate_port_input_event_thresholds.
{
    opennsl_spl_snmpIfInUcastPkts, /* 0 */       		  /* rx_packets */
    opennsl_spl_snmpIfInNUcastPkts, /* 1 */
    opennsl_spl_snmpIfOutUcastPkts, /* 2 */      		  /* tx_packets */
    opennsl_spl_snmpIfOutNUcastPkts, /* 3 */
    opennsl_spl_snmpIfInOctets, /* 4 */     	 		  /* rx_bytes */
    opennsl_spl_snmpIfOutOctets, /* 5 */         		  /* tx_bytes */
    opennsl_spl_snmpIfInErrors, /* 6 */          		  /* rx_errors */
    opennsl_spl_snmpIfOutErrors, /* 7 */         		  /* tx_errors */
    opennsl_spl_snmpIfInDiscards, /* 8 */        		  /* rx_dropped */
    opennsl_spl_snmpIfOutDiscards, /* 9 */       		  /* tx_dropped */
    opennsl_spl_snmpEtherStatsMulticastPkts, /* 10 */     /* Multicast */
    opennsl_spl_snmpEtherStatsCollisions, /* 11 */        /* collisions */
    opennsl_spl_snmpEtherStatsCRCAlignErrors, /* 12 */    /* rx_crc_errors */
    opennsl_spl_snmpIfInMulticastPkts, /* 13 */           /* rx multicast */
    opennsl_spl_snmpIfInBroadcastPkts, /* 14 */           /* rx broadcast */
    opennsl_spl_snmpIfInUnknownProtos, /* 15 */           /* rx unknown protos */
    opennsl_spl_snmpIfOutMulticastPkts, /* 16 */          /* tx multicast */
    opennsl_spl_snmpIfOutBroadcastPkts /* 17 */           /* tx broadcast */
};

#ifdef BST
	bst_counter_t id_list[MAX_BST_COUNTERS] = {
		{opennslBstStatIdUcast,            "opennslBstStatIdUcast"},
		{opennslBstStatIdMcast,            "opennslBstStatIdMcast"},
		{opennslBstStatIdPriGroupShared,   "opennslBstStatIdPriGroupShared"},
		{opennslBstStatIdPriGroupHeadroom, "opennslBstStatIdPriGroupHeadroom"},
		{opennslBstStatIdDevice, "opennslBstStatIdDevice"},  /*All device level counters declared here*/
		{opennslBstStatIdEgrPool, "opennslBstStatIdEgrPool"},
		{opennslBstStatIdEgrMCastPool, "opennslBstStatIdEgrMCastPool"},
		{opennslBstStatIdIngPool, "opennslBstStatIdIngPool"},
		{opennslBstStatIdPortPool, "opennslBstStatIdPortPool"},
		{opennslBstStatIdUcastGroup, "opennslBstStatIdUcastGroup"},
		{opennslBstStatIdCpuQueue, "opennslBstStatIdCpuQueue"}
	};
#endif

/*Function is used to interpret the actions specified  by the SDN controller into symbolic constants for easy processing*/
int convert_action_string_to_int(char *action)
{
	if(strcmp(action,"GOTO_NXT_STATE")==0)
	{
		dzlog_info("GOTO_NXT_STATE action identified \n");
		return GOTO_NXT_STATE;
	}
	if(strcmp(action,"NOTIFY_CNTLR")==0)
	{
		dzlog_info("NOTIFY_CNTLR action identified \n");
		return NOTIFY_CNTLR;
	}
	if(strcmp(action,"NOTIFY_LINK_UTILIZATION")==0)
	{
		dzlog_info("NOTIFY_LINK_UTILIZATION action identified \n");
		return NOTIFY_LINK_UTILIZATION;
	}
	if(strcmp(action,"NOTIFY_CNTLR_AFTER_N_INTERVALS")==0)
	{
		dzlog_info("NOTIFY_CNTLR action identified \n");
		return NOTIFY_CNTLR_AFTER_N_INTERVALS;
	}
	/*//HHH
	if(strcmp(action,"NOTIFY_PORT_STATS")==0)
	{
		dzlog_info("NOTIFY_PORT_STATS action identified \n");
		return NOTIFY_PORT_STATS;
	}*/
	if(strcmp(action,"NO_ACTION")==0) //Remain in the same state. But notification to the controller could be sent.
	{
		dzlog_info("NO_ACTION action identified \n");
		return NO_ACTION;
	}
	return -1;
}

/*Get the monitoring agent object from the hash-map*/
struct mon_agent* monitoring_agent_mapping_find(const int mon_id)
{
    struct mon_agent *m;

    HMAP_FOR_EACH_WITH_HASH (m,
                             hmap_node,
                             hash_int(mon_id,0),
                             &agent_hmap) {
        if (mon_id == m->mon_id) {
            #ifdef DEBUG
	     dzlog_info("returning the monitoring id object %d \n",mon_id);
	    #endif	
            return m;
        }
    }
    dzlog_info("Returning NULL \n");
    return NULL;
}

/*Remove the monitoring agent object from the hash-map*/
void monitoring_agent_mapping_destroy(struct mon_agent *m)
{
    if (m)
    {
		hmap_remove(&agent_hmap, &m->hmap_node);
		free(m);
    }
}


/*************************************************************************************************************
 * processNode: This function is adopted and modified from example implementations provided by libxml library.
 * @reader: the xmlReader
 * section: xmlReader
 * copy: see Copyright file for the license details of this function use.
 *************************************************************************************************************/
static void
processNode(xmlTextReaderPtr reader,struct mon_agent *mon_params) {
    const xmlChar *name, *value;

	if (xmlTextReaderNodeType(reader)==1)
	{    name = xmlTextReaderConstName(reader);
		if (name == NULL)
		name = BAD_CAST "--";

		value = xmlTextReaderConstValue(reader);
		if(strcmp((char*)name,"mon-id")==0)
		{
			mon_params->mon_id = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"mon-msg-type")==0)
		{
			mon_params->mon_msg_type = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"mon-type")==0)
		{

			//Let us have some fun, try to figure out which is what.use the enum in monitor-automata.h
			char *mon_type = (char*)xmlTextReaderReadString(reader); // Values 1 through 6. // Should fix this.
		#ifdef BST
			if(strcmp(mon_type,"BST")==0)
			{
				mon_params -> mon_type = BST;  //BST only.
			}
			else if(strcmp(mon_type,"BST_FP")==0) // Field processor statistics only.
			{
				mon_params -> mon_type = BST_FP;
			}
			else if(strcmp(mon_type,"BST_FP_STATS")==0) //all the three
			{
				mon_params -> mon_type = BST_FP_STATS;
			}
			else if(strcmp(mon_type,"BST_STATS")==0) //BST and Port statistics only.
			{
				mon_params -> mon_type = BST_STATS;
			}
		#endif
			if(strcmp(mon_type,"STATS")==0)	//Port statistics only
			{
				mon_params -> mon_type = STATS;
			}
			else if(strcmp(mon_type,"FP")==0)  //Flow statistics from the Field processor.
			{
				mon_params -> mon_type = FP;
			}
			else if(strcmp(mon_type,"FP_STATS")==0) // Field processor statistics only.
			{
				mon_params -> mon_type = FP_STATS;
			}
			else if(strcmp(mon_type,"P_STATS")==0) // HHH port statistics only.
			{
				mon_params -> mon_type = P_STATS;
			}
		}
		else if(strcmp((char*)name,"poll-time")==0)
		{
			mon_params -> poll_time = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"number-of-poll-intervals")==0)
		{
			mon_params -> tot_time_intervals = atoi((char*)xmlTextReaderReadString(reader)); // Used to define the window for event reporting. Also, to receive notification after N intervals.
		}
		else if(strcmp((char*)name,"port-index")==0)
		{
			dzlog_info("Contents of the node %s is :%s \n",name,xmlTextReaderReadString(reader));
			mon_params->port_index = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"link-util-threshold")==0)
		{
			dzlog_info("Contents of the node %s is :%s \n",name,xmlTextReaderReadString(reader));
			mon_params->link_utilization_threshold = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"TotalStates")==0)
		{
			dzlog_info("Contents of the node %s is :%s \n",name,xmlTextReaderReadString(reader));
			mon_params->total_states = atoi((char*)xmlTextReaderReadString(reader));
		}
		else if(strcmp((char*)name,"state-table-row-entries")==0)
		{
			//printf("Contents of the node %s is :%s \n",name,xmlTextReaderReadString(reader));
			//allocate so much memory so as to accommodate enough state table row entries.
			//struct table *temp_table; // Temporary state-table.
			if(mon_params->state_table_rows == NULL)
			{
			  mon_params->state_table_rows = (struct table *)malloc(sizeof(struct table) * mon_params->total_states);
			}
		}
		if(strcmp((char*)name,"state")==0)
		{
			dzlog_info("cur_row_count while extracting #state#:%d \n",mon_params->current_row_count);
			struct table *temp_table = (mon_params->state_table_rows);
			temp_table =  temp_table + mon_params->current_row_count;
			temp_table->state = atoi((char*)xmlTextReaderReadString(reader));
			dzlog_info("State is:%d \n",temp_table->state);
		}
		else if(strcmp((char*)name,"input-events")==0)
		{
			while(1)
			{
				xmlTextReaderRead(reader);
				if(xmlTextReaderNodeType(reader) == 1)
				{
					name = xmlTextReaderConstName(reader);

					#ifdef DEBUG
					 dzlog_info("Inside input-events, reading: %s \n",name);
					 dzlog_info("Its value is: %s \n",(char*)xmlTextReaderReadString(reader));
					#endif

					struct table *temp_table = (mon_params->state_table_rows);					//Get the pointer to current row of the state table.
					temp_table =  temp_table + mon_params->current_row_count;

					//Here follows the XML parsing for all possible input events defined.
					if(strcmp((char*)name,"num_of_row_evnts" )==0)
					{
					  dzlog_info("cur_row_count while extracting #num_of_row_evnts#:%d \n",mon_params->current_row_count);
					  temp_table -> num_of_row_evnts = atoi((char*)xmlTextReaderReadString(reader));
					  temp_table -> event = (input_params_t *)malloc(sizeof(input_params_t)*(temp_table -> num_of_row_evnts));
					}
			#ifdef BST
					if(strcmp((char*)name,"opennslBstStatIdUcast")==0)
					{
						strcpy(temp_table->event->name,"opennslBstStatIdUcast");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->bst_threshold_exists = true;
					}
					if(strcmp((char*)name,"opennslBstStatIdMcast")==0)
					{
						strcpy(temp_table->event->name,"opennslBstStatIdMcast");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->bst_threshold_exists = true;
					}
					//APPEND other events here.
					if(strcmp((char*)name,"opennslBstStatIdPriGroupShared")==0)
					{
						strcpy(temp_table->event->name,"opennslBstStatIdPriGroupShared");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->bst_threshold_exists = true;
					}
					if(strcmp((char*)name,"opennslBstStatIdPriGroupHeadroom")==0)
					{
						strcpy(temp_table->event->name,"opennslBstStatIdPriGroupHeadroom");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->bst_threshold_exists = true;
					}
			#endif
					//APPENDING other events of type port statistics and Flow statistics here.
					if(strcmp((char*)name,"IfInUcastPkts")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfInUcastPkts");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfInNUcastPkts")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfInNUcastPkts");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfOutUcastPkts")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfOutUcastPkts");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfOutNUcastPkts")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfOutNUcastPkts");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfInOctets")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfInOctets");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfOutOctets")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfOutOctets");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfInErrors")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfInErrors");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfOutErrors")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfOutErrors");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfInDiscards")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfInDiscards");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"IfOutDiscards")==0)
					{
						strcpy(temp_table->event->name,"opennsl_spl_snmpIfOutDiscards");
						temp_table->event->value = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->event++;
						temp_table->port_threshold_exists = true;
					}
					if(strcmp((char*)name,"statPktsThreshold" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table->statPktsThreshold = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->packet_threshold_exists = true;
					}
					if(strcmp((char*)name,"statBytesThreshold" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table->statBytesThreshold = atoi((char*)xmlTextReaderReadString(reader));
						temp_table->packet_threshold_exists = true;
					}
					if(strcmp((char*)name,"flow_to_install" )==0) //Check if there is a flow to install and set the flag to true.
					{
						dzlog_info("setting the flow exists boolean value to true\n");
						temp_table -> flow_exists = true;
					}

					if(strcmp((char*)name,"priority" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->flow.priority = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"src_ip" )==0) //Fetch the source IP string made available by the controller.
					{
						//HHH MSG
						 dzlog_info("Contents of the node %s is :%s \n",name,xmlTextReaderReadString(reader));
						 mon_params->source_ip = atoi((char*)xmlTextReaderReadString(reader));
						 
						 unsigned int source_ip;
						 opennsl_ip_parse((char*)xmlTextReaderReadString(reader), &source_ip);
						 temp_table ->flow.src_ip = source_ip;
						 dzlog_info("IP mask Decoded as %x\n",temp_table ->flow.src_ip);
					}
					if(strcmp((char*)name,"dst_ip" )==0) //Fetch the source IP string made available by the controller.
					{
						 unsigned int dest_ip;
						 opennsl_ip_parse((char*)xmlTextReaderReadString(reader), &dest_ip);
						 temp_table ->flow.dst_ip = dest_ip;
						 dzlog_info("IP mask Decoded as %x\n",temp_table ->flow.dst_ip);
					}

					if(strcmp((char*)name,"src_ip_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						 unsigned int source_ip_mask;
						 opennsl_ip_parse((char*)xmlTextReaderReadString(reader), &source_ip_mask);
						 temp_table ->flow.src_ip_mask = source_ip_mask;
						 dzlog_info("IP mask Decoded as %x\n",temp_table ->flow.src_ip_mask);
					}
					if(strcmp((char*)name,"dst_ip_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						 unsigned int dest_ip_mask;
						 opennsl_ip_parse((char*)xmlTextReaderReadString(reader), &dest_ip_mask); //fetch the IP mask string and convert into integer.
						 temp_table ->flow.dst_ip_mask = dest_ip_mask;
						 dzlog_info("IP mask Decoded as %x\n",temp_table ->flow.dst_ip_mask);
					}
					if(strcmp((char*)name,"src_mac" )==0) //Fetch the source IP string made available by the controller.
					{
						opennsl_mac_t source_mac;
						opennsl_mac_parse((char*)xmlTextReaderReadString(reader),source_mac);
						dzlog_info(temp_table -> flow.src_mac,source_mac,sizeof(opennsl_mac_t));
					}
					if(strcmp((char*)name,"dst_mac" )==0) //Fetch the source IP string made available by the controller.
					{
						opennsl_mac_t dest_mac;
						opennsl_mac_parse((char*)xmlTextReaderReadString(reader),dest_mac);
						memcpy(temp_table -> flow.dst_mac,dest_mac,sizeof(opennsl_mac_t));
					}
					if(strcmp((char*)name,"src_mac_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						opennsl_mac_t source_mac_mask;
						opennsl_mac_parse((char*)xmlTextReaderReadString(reader),source_mac_mask);
						memcpy(temp_table -> flow.src_mac,source_mac_mask,sizeof(opennsl_mac_t));
					}
					if(strcmp((char*)name,"dst_mac_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						opennsl_mac_t dest_mac_mask;
						opennsl_mac_parse((char*)xmlTextReaderReadString(reader),dest_mac_mask);
						memcpy(temp_table -> flow.dst_mac,dest_mac_mask,sizeof(opennsl_mac_t));
					}
					if(strcmp((char*)name,"src_port" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->flow.src_port = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"dst_port" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->flow.dst_port = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"src_port_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->flow.src_port_mask = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"dst_port_mask" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->flow.dst_port_mask = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"statPktsThreshold" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table ->statPktsThreshold = atoi((char*)xmlTextReaderReadString(reader));
					}
					if(strcmp((char*)name,"statBytesThreshold" )==0) //Fetch the source IP string made available by the controller.
					{
						temp_table -> statBytesThreshold= atoi((char*)xmlTextReaderReadString(reader));
					}
					//Append actions here.
					if(strcmp((char*)name,"num_of_actions" )==0)
					{
					  dzlog_info("Reading cur_row_count while extracting #num_of_actions#:%d \n",mon_params->current_row_count);
					  temp_table -> num_of_actions = atoi((char*)xmlTextReaderReadString(reader));
					  temp_table -> action = (int *)malloc(sizeof(int)*temp_table -> num_of_actions);
					}
					if(strcmp((char*)name,"action")==0)
					{
						int action_count = 0;
						while(1)
						{
							xmlTextReaderRead(reader);
							if(xmlTextReaderNodeType(reader) == 1)
							{
								name = xmlTextReaderConstName(reader);
								dzlog_info("Inside actions, reading: %s \n",name);
								dzlog_info("Its value is: %s \n",(char*)xmlTextReaderReadString(reader));
								//Here follows the XML parsing for all possible input events defined.
								char action_string[3];
								int *temp_action = temp_table -> action;
								temp_action = temp_action + action_count;
								sprintf(action_string,"A%d",++action_count);
								if(strcmp((char*)name,action_string)==0)
								{
									int ret_val = convert_action_string_to_int((char*)xmlTextReaderReadString(reader));
									*temp_action = ret_val;
									 if(ret_val == NOTIFY_LINK_UTILIZATION)
									 {
										mon_params->notify_link_utilization = true;
										dzlog_info("NOTIFY_LINK_UTILIZATION set to true \n");
									 }
									 else if(ret_val == NOTIFY_CNTLR)
									 {
											mon_params->notify_cntrl = true;
											dzlog_info("NOTIFY_CNTLR set to true \n");
									 }
									 else if(ret_val == NOTIFY_CNTLR_AFTER_N_INTERVALS)
									 {
											mon_params->notify_cntrl_after_interval = true;
											dzlog_info("NOTIFY_CNTLR_AFTER_N_INTERVALS set to true \n");
									 }
									 /*//HHH
									else if(ret_val == NOTIFY_PORT_STATS)
									 {
											mon_params->notify_port_stats = true;
											dzlog_info("NOTIFY_PORT_STATS set to true \n");
									 }*/
									 dzlog_info("Action decoded as: %d \n",*temp_action);
									 if(action_count == temp_table -> num_of_actions)
									 {
										 dzlog_info("Break from actions loop: %d action(s) count read\n",action_count);
										 break;
									 }
								}
							}
						 }
					}
					if(strcmp((char*)name,"next-state")==0)
					{
						//if(strcmp((char*)xmlTextReaderReadString(reader),"THIS_STATE")==0)
						//{
							dzlog_info("parsing next state tag \n");
							temp_table -> next_state = atoi((char*)xmlTextReaderReadString(reader));
						//}
						mon_params -> current_row_count++;
						//increment the number of rows in mon_param structure
					}
					//Input events extracted, now extract any possible statistics packets information present in the automata.
					if(strcmp((char*)name, "stat-packets")==0)
					{
						char *pEnd;
						dzlog_info("parsing stat-Packets \n");
						mon_params -> init_state.statPackets = strtoull((char*)xmlTextReaderReadString(reader),pEnd,10);
						dzlog_info("State information for stat-Bytes:%llu \n",mon_params -> init_state.statPackets);
					}
					if(strcmp((char*)name, "stat-bytes")==0)
					{
						char *pEnd;
						dzlog_info("parsing stat-Bytes\n");
						mon_params -> init_state.statBytes = strtoull((char*)xmlTextReaderReadString(reader),pEnd,10);
						dzlog_info("State information for stat-Bytes:%llu \n",mon_params -> init_state.statBytes);
					}

				}

				if(xmlTextReaderNodeType(reader) == 15 && (strcmp(xmlTextReaderConstName(reader),"state-table-row-entries")==0))
				{
					dzlog_info("break as \"state-table-row-entries\" end XML Tag element found\n");
					break;
				}
			}

		}
	}
}

/***********************************************************************************************
 * streamFile:This function is adopted from example implementations provided by libxml library.
 * @filename: the file name to parse
 ***********************************************************************************************/
static void
streamFile(const char *filename, const struct mon_agent *mon_params) {
    xmlTextReaderPtr reader;
    int ret;
    reader = xmlReaderForFile(filename, NULL, 0);
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            processNode(reader,mon_params);
            ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
        	dzlog_error("%s : failed to parse\n", filename);
        }
    } else {
    	dzlog_error( "Unable to open %s\n", filename);
    }
}

/*static void s_signal_handler (int signum)
{
	s_interrupted = true;
	exit(1);
}

static void s_catch_signals ()
{

    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}*/

int init_open_nsl()
{
	#ifdef opennsl
		opennsl_error_t rc;
		int unit = DEFAULT_UNIT;
		dzlog_info("Initializing the system...\r\n");
		rc = opennsl_driver_init((opennsl_init_t *) NULL);
		if (rc != OPENNSL_E_NONE )
		{
			dzlog_error("Failed to initialize the system at %d %d, reason: %d (%s)\r\n", __FILE__,__LINE__,rc,opennsl_errmsg(rc));
			return false;
		}
	#endif
	return true;
}

#ifdef BST
/*****************************************************************//**
 * \brief Callback function to capture BST triggers
 *
 * \param unit      [IN]    Unit number
 * \param event     [IN]    BST event
 * \param bid       [IN]    BST ID
 * \param port      [IN]    Port number
 * \param cosq      [IN]    COS queue
 * \param cookie    [IN]    To hold the data from register function
 *
 * \return None
 ********************************************************************/
void bst_stats_callback (int unit, opennsl_switch_event_t event,
    uint32 bid, uint32 port, uint32 cosq, void *cookie)
{
  dzlog_info("BST callback: event %d port %d bid %d cosq %d\n", event, port, bid, cosq);
  // Try to set the flag for this port indicating that the port received a  call back for exceeding BST threshold.
  return;
}

/********************************************************************************************************************************************
 * Register HW trigger callback
 *
 * Below function is copied from ops-bufmon.c file implementation distributed under APACHE License for opennsl-plugin(OpenSwitch OCP project)
 ********************************************************************************************************************************************/
void bst_switch_event_register(int enable)
{
    int hw_unit = 0;
    static int event_registered = false;

    for (hw_unit = 0; hw_unit <= MAX_SWITCH_UNIT_ID; hw_unit++)
    {
        if (enable) {
            opennsl_switch_event_register(hw_unit,
                        (opennsl_switch_event_cb_t)bst_stats_callback,
                         NULL);
            event_registered = true;
        } else if (!enable && event_registered) {
            opennsl_switch_event_unregister(hw_unit,
                        (opennsl_switch_event_cb_t)bst_stats_callback,
                        NULL);
            event_registered = false;
        }
    }
}

/**********************************************************************************************************
 * NOTICE: bst_default_profile_set function implementation is copied from example_bst.c file that
 * illustrates the use of Open-NSL APIs for sampling BST Statistics.
 **********************************************************************************************************/
/**************************************************************************
 * \brief To set the default BST profiles
 *
 * \param unit      [IN]    Unit number
 *
 * \return None
 **************************************************************************/
int bst_default_profile_set (int unit)
{
  int rc;
  int bid;
  int depth;
  int index;
  int max_threshold;
  int gport;
  int port;
  opennsl_info_t info;
  opennsl_cosq_bst_profile_t profile;
  opennsl_port_config_t pcfg;

  /* Get port configuration */
  rc = opennsl_port_config_get(unit, &pcfg);
  if (rc != OPENNSL_E_NONE)
  {
    printf("Failed to get port configuration, rc = %d (%s).\n",
        rc, opennsl_errmsg(rc));
    return rc;
  }

  if((rc = opennsl_info_get(unit, &info)) != OPENNSL_E_NONE)
  {
    printf("\r\nFailed to get switch device details, rc = %d (%s).\r\n",
        rc, opennsl_errmsg(rc));
    return rc;
  }

  for (bid = 0; bid < opennslBstStatIdMaxCount; bid++)
  {
    /* Find out the maximum threshold for a bid */
	    if(_BRCM_IS_CHIP_TH(info)) /* TomaHawk */
	    {	      
	      dzlog_info("\n TOMAHAWK switch  identified\n");

	      if (bid == _BST_STAT_ID_UCAST ||
	          bid == _BST_STAT_ID_UCAST_GROUP)
	      {
	        max_threshold = 0xFFF * 208;
	      }
	      else
	      {
	        max_threshold = 0x7FFF * 208;
	      }
	    }
	    else /* Trident 2 */
	    {
	      dzlog_info("AS5712 switch \n");	
	      /* Find out the maximum threshold for a bid */
	      if (bid == _BST_STAT_ID_UCAST                 ||
	          bid == _BST_STAT_ID_EGR_UCAST_PORT_SHARED ||
	          bid == _BST_STAT_ID_UCAST_GROUP)
	      {
	        max_threshold =  0x3FFF * 208;
	      }
	      else if (bid == _BST_STAT_ID_PRI_GROUP_HEADROOM)
	      {
	        max_threshold =  0xFFF * 208;
	      }
	      else
	      {
	        max_threshold =  0x1FFFF * 208;
	      }
	    }

    profile.byte = max_threshold;

    if (bid == _BST_STAT_ID_EGR_POOL              ||
        bid == _BST_STAT_ID_EGR_MCAST_POOL        ||
        bid == _BST_STAT_ID_EGR_UCAST_PORT_SHARED ||
        bid == _BST_STAT_ID_EGR_PORT_SHARED       ||
        bid == _BST_STAT_ID_UCAST_GROUP           ||
        bid == _BST_STAT_ID_PORT_POOL             ||
        bid == _BST_STAT_ID_ING_POOL)
    {
      depth = 4;
    }
    else if (bid == _BST_STAT_ID_PRI_GROUP_SHARED ||
        bid == _BST_STAT_ID_PRI_GROUP_HEADROOM    ||
        bid == _BST_STAT_ID_UCAST                 ||
        bid == _BST_STAT_ID_MCAST)
    {
      depth = 8;
    }
    else if (bid == _BST_STAT_ID_RQE_QUEUE)
    {
      depth = 11;
    }
    else
    {
      depth = 1;
    }

    /* Set default BST profiles */
    if (bid == _BST_STAT_ID_ING_POOL       ||
        bid == _BST_STAT_ID_EGR_MCAST_POOL ||
        bid == _BST_STAT_ID_EGR_POOL       ||
        bid == _BST_STAT_ID_RQE_QUEUE      ||
        bid == _BST_STAT_ID_UCAST_GROUP    ||
        bid == _BST_STAT_ID_DEVICE)
    {
       gport = 0;
       for (index = 0; index < depth; index++)
       {
         rc = opennsl_cosq_bst_profile_set (unit, gport, index, bid, &profile);
         if (rc != OPENNSL_E_NONE)
         {
           dzlog_error("\r\nFailed to set the BST profile for gport %d bid %d index %d, rc = %d (%s).\r\n",
               gport, bid, index, rc, opennsl_errmsg(rc));
           return rc;
         }
       }
    }
    else if (bid == _BST_STAT_ID_PRI_GROUP_SHARED      ||
             bid == _BST_STAT_ID_PRI_GROUP_HEADROOM    ||
             bid == _BST_STAT_ID_UCAST                 ||
             bid == _BST_STAT_ID_MCAST                 ||
             bid == _BST_STAT_ID_PORT_POOL             ||
             bid == _BST_STAT_ID_EGR_PORT_SHARED       ||
             bid == _BST_STAT_ID_EGR_UCAST_PORT_SHARED )
    {
         OPENNSL_PBMP_ITER(pcfg.e, port)
         {
			/* Get GPORT*/
			rc = opennsl_port_gport_get (unit, port, &gport);
			if (rc != OPENNSL_E_NONE)
			{
			   dzlog_error("\r\nFailed to get gport for port %d, rc = %d (%s).\r\n",
				   port, rc, opennsl_errmsg(rc));
			   return rc;
			}

			for (index = 0; index < depth; index++)
			{
			  rc = opennsl_cosq_bst_profile_set (unit, gport, index, bid, &profile);
			  if (rc != OPENNSL_E_NONE)
			  {
				dzlog_error("\r\nFailed to set the BST profile for gport %d bid %d index %d, rc = %d (%s).\r\n",
					gport, bid, index, rc, opennsl_errmsg(rc));
				return rc;
			  }
			} /* Iter ... index */
         } /* Iter ... port */
    }

  } /* Iter ... bid */

  return 0;
}
#endif


int bcm_application_init()
{
	int success= false;
	int unit = DEFAULT_UNIT;

#ifdef opennsl
   opennsl_error_t rc;

   #ifdef BST
		  //--> Below code was used keeping BST in perspective.
		dzlog_info("Adding ports to default vlan.\r\n");
		rc = example_switch_default_vlan_config(unit);
		if (rc != OPENNSL_E_NONE)
		{
		   dzlog_error("\r\nFailed to add default ports, rc = %d (%s).\r\n",rc, opennsl_errmsg(rc));
		   return rc;
		}

		rc = example_port_default_config(unit);
		if (rc != OPENNSL_E_NONE)
		{
		 dzlog_error("\r\nFailed to apply default config on ports, rc = %d (%s).\r\n",rc, opennsl_errmsg(rc));
		 return rc;
		}

		bst_default_profile_set(unit); //BST Profile SET.

		int enable = true;
		bst_switch_event_register(enable);

		rc = opennsl_switch_control_set(unit, opennslSwitchBstEnable, 1);
		if (rc != OPENNSL_E_NONE) {
		dzlog_error("\r\nFailed to Enable bst, rc = %d (%s).\r\n",
			rc, opennsl_errmsg(rc));
		return 0;
		}
		dzlog_info("BST feature is enabled.\r\n");
   #endif

	rc = opennsl_stat_init(unit);	  /* Initializing the port statistics module. */
	if (rc != OPENNSL_E_NONE) {
	  dzlog_error("\r\nFailed to opennsl_stat_init, rc = %d (%s).\r\n",
		rc, opennsl_errmsg(rc));
	return 0;
	}

	rc = configure_test_environment();
	if(!rc)
	  dzlog_info("\n Default route added successfully \n");
 #endif

	return success;
}

#ifdef CHECK_POLLING_ACCURACY
int s_polling_accuracy_dump_timer(zloop_t *loop, int timer_id, void *arg)
{
	size_t hmap_size = hmap_count(&agent_hmap);
	dzlog_info("\nTotal Monitoring agents: %d\n",hmap_size);

    struct mon_agent *m;
	HMAP_FOR_EACH (m,hmap_node,&agent_hmap) {
		if (m != NULL) {
			#ifdef CHECK_POLLING_ACCURACY
				dzlog_info("Monitoring agent is %d: min-time:%llu max-time:%llu delay 0:%llu, delay 1:%llu, delay -1:%llu\n",m->mon_id,m->min_delay,m->max_delay,m-> delay_0,m-> delay_1,m-> delay_minus_1);
			#endif
		}
	}
	dzlog_info("total_poll_intervals:%llu \n",total_poll_intervals);
	return 0;
}
#endif

/******************************************************************************************
 *Used to end the TCAM threshold check timer.
 *A fresh timer is created every time when s_fp_group_status_check_timer_event is invoked.
 * ****************************************************************************************/

int s_fp_notify_timer_event(zloop_t *loop, int timer_id, void *arg)
{
  send_switch_notification = true;
  zloop_timer_end (loop,timer_id);
  return 0;
}

/**************************************************************************************
 * Loop event timer that expires periodically to check the TCAM memory availability on
 * the device.
 ************************************************************~*************************/
int s_fp_group_status_check_timer_event(zloop_t *loop, int timer_id, void *arg)
{
	char *mon_switch_data;
	opennsl_field_group_status_t  status;
	int hmap_size = hmap_count(&agent_hmap); //use hmap_size instead of status.entry_count because it gives the number of monitoring agents configured.
	dzlog_info("FP group status check timer expired,%d\n",hmap_size);


#ifdef DEBUG
   char *time_str = zclock_timestr ();
   dzlog_info("time expired at %s\n",time_str);
   free(time_str);
#endif

#ifdef opennsl
	status = get_field_process_group_status(DEFAULT_UNIT);
#else
	status.entries_free = 4090;
	status.entry_count = hmap_size;
#endif

	if(hmap_size > tcam_thresholds.max_allowed_tcam_entries && status.entries_free < tcam_thresholds.min_free_entries_per_device && send_switch_notification == true) //total_entries_fp -> Threshold set for monitoring fp group
	{
		// Number of free entries on the device has dropped below a minimum threshold(entries could be used for higher priority tasks) to be maintained.
		// TO DO: Trigger MON_SWITCH so that some MON_AGENTs could be freed to free the TCAM entries and also the counters.
		send_mon_switch_notification(publisher, status);
		int fp_timer_id = zloop_timer(loop, tcam_timer_interval, 0, s_fp_notify_timer_event, NULL);
		send_switch_notification = false;
	}
	return 0;
}


/*******************************************************************************
 *Below function shall handle the Timer events.
 *A particular automata will execute when a corresponding timer thread expires.
 *******************************************************************************/

int s_timer_event (zloop_t *loop, int timer_id, void *mon_id)
{
	int rc,index;
	int unit = DEFAULT_UNIT;
	int gport;
	int port;
	int options = 0;
	int port_stats_val[MAX_STAT_COUNTERS];	//Per port statistics.
	int current_state;
	struct bst_val_counters bst_stats;
	int row_num;
	int ret_status;
	int number_of_actions;
	uint8 notification;

	#ifdef CHECK_POLLING_ACCURACY	
		int64 execution_start_time = zclock_usecs ();
		int64 time_taken;
		int64 delay;
		volatile int64 curr_time = zclock_mono (); // This is the instrumentation used to check the accuracy of polling.
		total_poll_intervals = total_poll_intervals + 1;
	#endif

	//TO DO: Get the mon_params from the HashMap and then pass it to the run_state_machine function.
	//#ifdef DEBUG
		dzlog_info("Timer expired for %d \n",(int*)mon_id);
	//#endif

    int mon_agent_id = (int*)mon_id;
    struct mon_agent *m = monitoring_agent_mapping_find(mon_agent_id);

	struct mon_agent *mon_param;
	HMAP_FOR_EACH (mon_param,hmap_node,&agent_hmap) {
		if (m != NULL && mon_param->poll_time == m->poll_time)
		{
		    if(mon_param->tot_time_intervals != 0) //Keep this count only if required. i.e. number of polling intervals is pre-defined by the controller.
		    	mon_param->curr_interval_count = (mon_param->curr_interval_count + 1)%mon_param->tot_time_intervals;

				#ifdef CHECK_POLLING_ACCURACY     // --> Below instrumentation is for finding the accuracy of polling.
		    	 if(mon_param->mon_id == m->mon_id)
		    	 {
						dzlog_info("delay will be incremented for mon_id %d\n",mon_param->mon_id);
						int64 time_elapsed = curr_time - mon_param ->start_time; //start_time of the monitoring agent ID that has timer object is considered.
						//mon_param ->start_time = curr_time;
						delay = (time_elapsed - mon_param->poll_time);
						if(mon_param->first_interval)
						{
							if(delay > 0)
							{
								mon_param ->max_delay = delay;
								mon_param ->min_delay = delay;
							}
							mon_param->first_interval = false;
							#ifdef DEBUG
								dzlog_info("MonId:%d Time delays during first interval Max-delay:%d Min-delay:%d Calculated-delay: %d Time elapsed %d start time %llu currtime: %llu\n",mon_param->mon_id,mon_param ->max_delay,mon_param ->min_delay,delay,time_elapsed,mon_param ->start_time,curr_time);
							#endif
						}
						else
						{
							if(delay > mon_param ->max_delay && delay < 100)
							{
							  mon_param ->max_delay = delay;
							}
							if(delay < mon_param ->min_delay && delay > 0 ) //delay could be negative, if time_elapsed is less than the poll-time.In some cases it's been noticed that timer expiry happens 1~2 ms earlier than actual timer duration.
							{
							  mon_param ->min_delay = delay;
							}
							//#ifdef DEBUG
								dzlog_info("MonId:%d Time delays during other intervals cal-delay:%d Max-delay:%d Min-delay:%d Time elapsed %d start time %llu currtime: %llu\n",mon_param->mon_id,delay,mon_param ->max_delay,mon_param ->min_delay,time_elapsed,mon_param ->start_time,curr_time);
							//#endif
							if(delay > 100)
							{
							   char *time_str = zclock_timestr ();
							   dzlog_info("Delay crossed the 100 limit at %s\n",time_str);
							   free(time_str);
							}
						}

						if(delay == 0)
						{
							mon_param->delay_0 += 1;
						}
						else if(delay == 1)
						{
							mon_param->delay_1 += 1;
						}
						else if(delay == -1)
						{
							mon_param->delay_minus_1 += 1;
						}
						mon_param ->start_time = curr_time;  // update the previous start_time.
		    	 }
				#endif    // --> Above instrumentation is for finding the accuracy of polling.
					//NOTE: Set the state of the state-machine to sequence number starting from 1.
					row_num = (mon_param -> current_state - 1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
					//dzlog_info("ROW identified as %d  \n",row_num);
					struct table* table_row = get_row_of_state_machine(row_num,mon_param);
					port = get_port_from_mon_params(mon_param);// PORT index is required to get BST and PORT level statistics.//HHH

					#ifdef DEBUG
					 dzlog_info("Port extracted from mon_param is %d \n",port);
					#endif

					// T -> Start of the time interval.
					// T+ delta T -> End of a time interval, where time interval is the poll-time: Values collected at (T + delta T) are generally the latest values of the counters in sync with the hardware.

					//Copy the counter values collected at end of the previous interval (i.e. at (T + delta T) ) to data structures that hold values of counters at the start of an interval.
					//They serve as the values that were collected at the beginning of the current polling interval.
					//HHH
					if(mon_param -> mon_type == STATS || mon_param -> mon_type == FP_STATS || mon_param -> mon_type == P_STATS || mon_param -> mon_type == BST_STATS || mon_param -> mon_type == BST_FP_STATS)
					{
						memcpy((mon_param -> mon_state_info.port_stats_val),(mon_param -> mon_state_info.port_stats_val_delta),(sizeof(int)*MAX_STAT_COUNTERS));
						memset(mon_param -> mon_state_info.port_stats_val_delta,0,sizeof(int)*MAX_STAT_COUNTERS);

						struct port_stats_val_t port_stats = per_port_pkt_stats(DEFAULT_UNIT,port); //call to function in monitoring_utilities.c
						memcpy((mon_param -> mon_state_info.port_stats_val_delta),port_stats.val,(sizeof(int)*MAX_STAT_COUNTERS)); //Get the current port statistics
						#ifdef DEBUG
						 dzlog_info("PORT related statistics pegging \n");
						#endif
					}
				//BST stats
				#ifdef BST
					if(mon_param -> mon_type == BST_STATS  || mon_param -> mon_type ==  BST_FP_STATS || mon_param -> mon_type == BST ||  mon_param -> mon_type == BST_FP )
					{
						memcpy((&mon_param -> mon_state_info.bst_stats),(&mon_param -> mon_state_info.bst_stats_delta),sizeof (struct bst_val_counters));
						memset(&mon_param-> mon_state_info.bst_stats_delta,0,sizeof(struct bst_val_counters));
						mon_param-> mon_state_info.bst_stats_delta = bst_stats_port_level(DEFAULT_UNIT,port); // Get the BST counters to the data structures meant to hold the current value of the counters.
						dzlog_info("BST related statistics pegging\n");
					}
				#endif
				//flowstats
					if((mon_param -> mon_type == FP || mon_param -> mon_type == FP_STATS)  && table_row->flow_exists)
					{
						memcpy((&mon_param -> mon_state_info.flow_stats_val),(&mon_param -> mon_state_info.flow_stats_val_delta),sizeof(struct fp_flow_stats_t));
						memset(&mon_param -> mon_state_info.flow_stats_val_delta,0,sizeof(struct fp_flow_stats_t));
						#ifdef DEBUG
							dzlog_info("Flow statistics from delta values  is %d  %d\n",mon_param -> mon_state_info.flow_stats_val_delta.statPackets,mon_param -> mon_state_info.flow_stats_val_delta.statBytes);
							dzlog_info("Flow statistics from delta values  is %d  %d\n",mon_param -> mon_state_info.flow_stats_val.statPackets,mon_param -> mon_state_info.flow_stats_val.statBytes);
							dzlog_info("statistics object attached to the flow entry is %d \n",mon_param->stat_id);
						#endif

							//call to function in monitoring_utilities.c to get the stats.
							//Get the statistics attached to this flow entry.
							mon_param->mon_state_info.flow_stats_val_delta = get_stats_attached_to_flow_enty(DEFAULT_UNIT,mon_param->stat_id); 

						#ifdef DEBUG
							dzlog_info("Flow statistics collected during this interval is %llu  %llu\n",mon_param -> mon_state_info.flow_stats_val_delta.statPackets,mon_param -> mon_state_info.flow_stats_val_delta.statBytes);
						#endif
						calculate_current_interval_flow_stats(mon_param);// Calculate the flow statistics for this interval.
						notification = check_events_flow_stats(row_num,mon_param); // Check if there is an event that needs to be notified to the controller.
					}

					//TO DO: Get the difference between the counter values at time 'T' and '(T' + delta 't'),where (delta T) is the counter values polled after each timer expiry equal to the poll-time; pass that as an argument to the run_state_machine function.
					//TO DO: To find out if there exist BST/ PORT Statistics threshold defined in the statemachine's current state. If so, call the below function to check if any of the thresholds is breached
					//for this interval.

					// 1 => Flow monitoring only, 2 => Port Statistics, 3 => BST Statistics. So, value 4 => FP+PORT, 5 => Flow + BST 6=> Port +BST.
					if( mon_param -> mon_type == FP_STATS ||  mon_param -> mon_type == STATS )  
					{
						dzlog_info("\n Checking events for PORT type \n");
						calculate_the_current_interval_values(mon_param);
						notification = check_events_bst_port_stats(port,port_stats_val,bst_stats,current_state,mon_param);
					}

					//HHH MSG - checking to see if the port threshold has been exceeded. 
					uint64 Port_Data_Bytes = mon_param -> mon_state_info.flow_stats_val.statBytes;
					if (Port_Data_Bytes > port_stats.max_port_data)
					{
						dzlog_info("\n Port threshold has been exceeded.\n");
						send_port_stats_notification(mon_param,publisher,port_stats.port_number);
					}




					//PRO-ACTIVE-POLL: New Feature to proactively send the statistics at the end of every polling interval.
					//dzlog_info("\n send_stats_notification_immediate being triggered \n");
					//call_count++;
					//send_stats_notification_immediate(mon_param,publisher,call_count);

					 //dzlog_info("Current Interval count is %d \n",mon_param->curr_interval_count);
					 //TO DO: Check the apply actions section of the state-machine at this point and notify the controller if need be.
					 number_of_actions = get_num_of_actions(table_row);
					 int *actions = get_actions(table_row);
					 if(number_of_actions)
					 {
						 for(int act = 0;act < number_of_actions;act++,actions++)
						 {
							 //make sure that number of polling interval is not predefined by the controller.
							 if((*actions == NOTIFY_CNTLR || mon_param->notify_cntrl == true) && mon_param->tot_time_intervals == 0) 
							 {
								 dzlog_debug("\n No, I did not do this action 1 and the value of the flag is %d \n",mon_param->notify_cntrl);
								 //This means to notify the controller.
								 if(notification == true || table_row->flow_exists)
								 {
									 send_stats_notification(mon_param,publisher);
								 }
								 else
								 {

									#ifdef DEBUG
									 dzlog_debug("No events detected to send notifications to the controller \n");
									#endif
								 }
							 }
							 else if( (*actions == NOTIFY_CNTLR_AFTER_N_INTERVALS || mon_param->notify_cntrl_after_interval == true) && mon_param->curr_interval_count == 0) //If configured number of polling intervals have occurred then prepare to send the notification.
							 {
								 dzlog_debug("\n No, I did not do this action 2\ and the value of NOTIFY AFTER N INTERVAL Flag is %d \n",mon_param->notify_cntrl_after_interval);
								 dzlog_debug("Sending notification after 'N' intervals \n");
								 //This means to notify the controller.
								 if(notification == true || table_row->flow_exists)
								 {
									 send_stats_notification(mon_param,publisher);
								 }
								 else
								 {
									#ifdef DEBUG
									 dzlog_debug("No events detected to send notifications to the controller \n");
									#endif
								 }
							 }
							 else if(*actions == NOTIFY_LINK_UTILIZATION || mon_param->notify_link_utilization == true)
							 {
								 dzlog_debug("\n No, I did not do this action 3 and the value of the flag Util flag is %d\n",mon_param->notify_link_utilization);								 /*Calculate link utilization here*/
								 dzlog_debug("Value of port inreval is %d \n",mon_param->mon_state_info.port_stats_val_interval[0]);	
								 if(mon_param -> mon_type == FP_STATS && mon_param->mon_state_info.port_stats_val_interval[0] != 0  && mon_param->notify_link_utilization == true)
								 {
									 uint64 statCounter = mon_param->mon_state_info.flow_stats_val_interval.statPackets;
									 uint64 uniCastPackets = mon_param->mon_state_info.port_stats_val_interval[0];
									 uint64 nonUniCastPackets = mon_param->mon_state_info.port_stats_val_interval[1];
									 float link_util = (float) statCounter /(uniCastPackets + nonUniCastPackets ) * 100;
									 dzlog_info("link util calculated as %f \n",link_util);
									 if(link_util >= mon_param->link_utilization_threshold && mon_param->link_utilization_threshold!=0)
									 {
										 send_link_util_notif(mon_param ,publisher);
									 }

								 }
							 }
							 else if(*actions == GOTO_NXT_STATE && table_row -> next_state != 0xFF) //255 ==> Ignore this action.It implies that there could be at max 254 flows in a mon-agent request.
							 {
								 dzlog_info("\n No, I did not do this action 4\n");
								 //If this action is specified, it means the next-state variable will contain a valid value.
								 //If this action is not specified, the monitoring-agent will continue to monitor the same flow.
								 //The actions implies that there exist a new flow entry to be monitored. So, fetch the flow parameters and install the new flow into the hardware table.
								 //Remove the flow entry rule that is being monitored now.
								 //The idea is to zoom-in the monitoring from a wild-carded mask to a more specific IP address. For instance, 192.168.*.* to 192.168.x.* where X is some children of the previously
								 //specified IP address. For more info, read OpenWatch paper.

								//Un-install the current flow that was being monitored by the agent.
								 rc = mon_fp_stats_feature_entry_destroy(mon_param->flow_entry_id,DEFAULT_UNIT,2, mon_param->stat_id);
								 if( rc != OPENNSL_E_NONE)
								 {
									 dzlog_error("Flow rule could not be destroyed for some reason \n");

								 }
								 // Change the current_state to the state indicated by the next-state variable.
								 mon_param->current_state = table_row -> next_state; 
								//Remove this later. This is only for testing purposes.
								 dzlog_info("GOTO_NEXT_STATE_ACTION Decoded and the current state is %d \n",mon_param->current_state); 

								 row_num = (mon_param->current_state-1);
								 //Installs the required flow and initializes the flow statistics to zero inside this function.
								 rc = start_flow_stats_state_machine(mon_param,row_num);	
								 if(rc == -1)
								 {
									//free the mon_params as we could not install the flow.
									//TO DO: Send notification to the controller that certain flow-rule could not be installed. --> Future work.
									dzlog_error("freeing the mon_params as we could not install the flow\n");
									monitoring_agent_mapping_destroy(mon_param);
									free(mon_param);
									return 0;

								 }
								 //Update the statistics for the new flow here.
								 memset(&(mon_param->mon_state_info),0,sizeof(struct monitoring_state_t)); //Initialize the statistics to zero again.
								 if(mon_param->flow_priority)
								 {
									 //Invoke the function to set the priority here.
									 mon_fp_feature_flow_entry_set_priority(mon_param->flow_entry_id,DEFAULT_UNIT,row_num,mon_param);
								 }


								 int port = get_port_from_mon_params(mon_param); // Read the port-index from the monitoring object. + hhh

								 //Get the current value of the BST and port counters and save in the corresponding monitoring agent data-structure.
								 #ifdef BST
									 if(mon_params -> mon_type == BST_STATS  || mon_params -> mon_type ==  BST_FP_STATS || mon_params -> mon_type == BST ||  mon_params -> mon_type == BST_FP )
									 {
									  bst_stats = bst_stats_port_level(DEFAULT_UNIT,port);
									  memcpy(&(mon_params->mon_state_info.bst_stats),&bst_stats,sizeof(struct bst_val_counters));
									  dzlog_info("Fetching BST_FP_STATS before starting the monitoring agent \n");
									 }
								 #endif

								 struct port_stats_val_t port_stats;
								 port_stats = per_port_pkt_stats(DEFAULT_UNIT,port); //+ HHH - getting port stats - function to monitoring_utilities.c
								 //Copy the buffer statistics and Port statistics at the beginning of this interval.
								 memcpy(mon_param->mon_state_info.port_stats_val_delta,port_stats.val,(sizeof(int)*MAX_STAT_COUNTERS));		     
								//Get the flow statistics just in case if it has already been running before the flow is installed.
								 mon_param->mon_state_info.flow_stats_val_delta = get_stats_attached_to_flow_enty(DEFAULT_UNIT,mon_param->stat_id);  

								#ifdef DEBUG
								 dzlog_debug("ROW changed due to GOTO_NXT_STATE Action as %d  \n",current_state);
								#endif
							 }
						 }
						}
		}//End of if Loop for Hashmap iteration.
	}


#ifdef CHECK_POLLING_ACCURACY
		int64 execution_end_time = zclock_usecs ();
		time_taken = (execution_end_time-execution_start_time);
		dzlog_info("Total time spent during this interval:%llu  delay in scheduling the timer:%d\n",time_taken,delay);
		if(time_taken > (m->poll_time*1000))
			dzlog_info(" Execution time exceed poll interval time\n");

#endif

    return 0;
}


/***************************************************************************************************************************************
 *Handle timer events triggered by the main thread. Timers are set per monitoring agent.
 *NOTE: https://github.com/zeromq/czmq/commit/b448f8a7a79b1df0d139640ddb380aaef07eac46 --> Performance testing w.r.t timers scalability.
 *4000 timers take about  => 24761 msecs to insert/reset the timers. This is fine from monitoring automata perspective.
 ***************************************************************************************************************************************/

int s_netconf_socket_event (zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{

	 char *msg = zstr_recv (rpc_repservice); //string is what that supposedly contains our XML Configuration.
	 if(msg != NULL)
	 {
		 int ret_status,val_result;

		 ret_status = check_msg_type(msg);
		 if(ret_status)
		 {
			 struct mon_agent *mon_params = NULL;
			 int msg_type;
			 size_t delay;
			 int timer_id;
			 int row_num;

			 mon_params = (struct mon_agent*)malloc(sizeof(struct mon_agent));
			 if(mon_params == NULL)
			 {
				 dzlog_error("memory allocation failure\n");
				 zstr_send (rpc_repservice, "NOK");
				 return 0;
			 }
			 memset(mon_params,0,sizeof(struct mon_agent)); //initialize the memory to all zeroes.
			 mon_params ->state_table_rows =  NULL;

			 msg_type = validate_mon_activity_params(msg,mon_params);

			 //TO DO: Create new threads if required for monitoring and also a ZEROMQ based PAIR socket shared with the new thread.
			 if(msg_type == MON_HELLO)
			 {
				 #ifdef DEBUG
					 dzlog_debug("MON_HELLO received for the agent %d \n",mon_params->mon_id);
				 #endif
				 size_t hmap_size = hmap_count(&agent_hmap);

				 int current_state;
				 struct bst_val_counters bst_stats;
				 mon_params->current_state = 1;  //Should always begin from 1.
				 row_num = (mon_params->current_state-1); // 0, if not configured. Updated at the end of each interval, if GOTO_NEXT_STATE action is specified.
				 int rc = start_flow_stats_state_machine(mon_params,row_num);	//Installs the required flow and initializes the flow statistics to zero inside this function.
				 if(rc == -1)
				 {
					//free the mon_params as we could not install the flow.
					dzlog_error("freeing the mon_params as we could not install the flow\n");
					free(mon_params);
					zstr_send (rpc_repservice, "Flow Entry Creation failed");
					return 0;

				 }

				 //check if there is a priority defined for the flow. If so, set the priority. NOTE: The priority is system-wide
				 //which implies, it should be unique across all the TCAM tables. It would be appropriate to define a priority range for this table.
				 //For instance, use 0-256 for the FP group that we use for monitoring.

				 if(mon_params->flow_priority)
				 {
					 //Invoke the function to set the priority here.
					 mon_fp_feature_flow_entry_set_priority(mon_params->flow_entry_id,DEFAULT_UNIT,row_num,mon_params);
				 }

				 int port = get_port_from_mon_params(mon_params); // Read the port-index from the monitoring object.

				 //Get the current value of the BST and port counters and save in the corresponding monitoring agent data-structure.
				 #ifdef BST
					 if(mon_params -> mon_type == BST_STATS  || mon_params -> mon_type ==  BST_FP_STATS || mon_params -> mon_type == BST ||  mon_params -> mon_type == BST_FP )
					 {
					  bst_stats = bst_stats_port_level(DEFAULT_UNIT,port);
					  memcpy(&(mon_params->mon_state_info.bst_stats),&bst_stats,sizeof(struct bst_val_counters));
					  dzlog_info("Fetching BST_FP_STATS before starting the monitoring agent \n");
					 }
				 #endif

				 struct port_stats_val_t port_stats;
				 port_stats = per_port_pkt_stats(DEFAULT_UNIT,port);
				 //Copy the buffer statistics and Port statistics at the beginning of this interval.
				 memcpy(mon_params->mon_state_info.port_stats_val_delta,port_stats.val,(sizeof(int)*MAX_STAT_COUNTERS));		     
				//Get the flow statistics just in case if it has already been running before the flow is installed.
				 mon_params->mon_state_info.flow_stats_val_delta = get_stats_attached_to_flow_enty(DEFAULT_UNIT,mon_params->stat_id);  

				 //Check if there is already any monitoring agent with specified poll interval, if so group this agent together.so that they all can  be polled for statistics in one timer expiry.
				 delay = mon_params->poll_time;	//Set the timer according to the poll_time obtained from the controller.
				 struct mon_agent *m;
				 uint8 timer_exist = false;
				 HMAP_FOR_EACH (m,hmap_node,&agent_hmap){
					if (m != NULL && m->poll_time == delay && m->timer_id != 0) // if the poll-time is same as the current monitoring agent's poll-time and timer_id is a valid value
					{
						timer_exist = true;
						break;
					}
				 }
				  // Use hash-maps to maintain timer_id to monitoring_id mapping so that information can be retrieved as fast as possible upon timer expiration.
				 hmap_insert(&agent_hmap, &mon_params->hmap_node, hash_int(mon_params->mon_id, 0));     
				 if(!timer_exist) // if there is no timer already for this polling interval, start polling the stuff.
				 {
					 timer_id = zloop_timer(loop, delay, 0, s_timer_event, mon_params->mon_id); //0 ==> indicates that the timer should run indefinite number of times until it is canceled by MON_STOP.
				 	 mon_params->timer_id = timer_id; //Save this timer_id with Monitoring agent parameters for later retrieval.
				 }
				 else
				 {
					 dzlog_info("Timer already exists with some other monitoring agent with id %d\n",m->mon_id);
				 }

				 #ifdef DEBUG
				  hmap_size = hmap_count(&agent_hmap);
				  dzlog_debug("Present size of the hash-map after inserting a new agent is %d \n",hmap_size);
				 #endif

				 zstr_send (rpc_repservice, "OK");// Send an immediate REPLY OK to NETCONF Server at this point.

				 #ifdef	CHECK_POLLING_ACCURACY
					 // --> Instrumentation to find out the accuracy of polling.
					 mon_params ->start_time = zclock_mono ();  // Get the current time ticks since system start up in milliseconds.This is only an instrumentation to find out the accuracy of polling.
					 mon_params ->max_delay = 0;
					 mon_params ->min_delay = 0;
					 mon_params->first_interval = true;
				#endif // --> Instrumentation to find out the accuracy of polling.
			 }
			 else if(msg_type == MON_STOP)
			 {
				 char *token;
				 uint8 loop_count = 0;
				 token = strtok(msg," ");
				 int mon_id;

				 while( token != NULL)
				 {
					 if(loop_count == 1) //MSG_TYPE is followed by MON_ID
					 {
						 mon_id = atoi(token);
						 dzlog_info("\n stop time for mon_id %d \n",mon_id);
					 }
					  token = strtok(NULL," ");
					  loop_count++;
				 }
				 dzlog_debug("Received message MON_STOP !!!:%s \n",msg);
				 //TO DO: Get the timer ID associated with the Monitoring Agent & CANCEL THE CORRESPONDING TIMER.
				 struct mon_agent *agent_obj = monitoring_agent_mapping_find(mon_id);
				 int timer_id = agent_obj->timer_id;
				 if(timer_id != 0)
				 {
					 dzlog_debug("Destroying timer_id %d \n",timer_id);
				 	 zloop_timer_end (loop,timer_id);
					 //Check if there are other agents with this polling requirement, if so, create a new timer for one of them.
					 struct mon_agent *m;
					 HMAP_FOR_EACH (m,hmap_node,&agent_hmap){
						if (m != NULL && m->poll_time == agent_obj->poll_time && m->timer_id == 0 && agent_obj->mon_id != m->mon_id) // if the poll-time is same as the current monitoring agent's poll-time and timer_id is a valid value
						{
							 timer_id = zloop_timer(loop, m->poll_time, 0, s_timer_event, m->mon_id); //0 ==> indicates that the timer should run indefinite number of times until it is canceled by MON_STOP.
							 m->timer_id = timer_id;			 										//Save this timer_id with Monitoring agent parameters for later retrieval.
							break;
						}
					 }
				 }


				 mon_fp_stats_feature_entry_destroy(agent_obj->flow_entry_id,DEFAULT_UNIT,2, agent_obj->stat_id);
				 zstr_send (rpc_repservice, "OK");//TO DO: remove this if the new solution does not work. Do an immediate REPLY OK to NETCONF Server at this point.
				 //TO DO: Remove the monitoring agent from the HASHMAP. Fetch & free the monitoring agent parameters data-structure.This is different from the mon_params that is being free below.
				 monitoring_agent_mapping_destroy(agent_obj);
				 free(mon_params); //This was allocated to decode the Monitoring message from the controller.
			 }
			 else if(msg_type == MON_PARAM_CHANGE)
			 {
				 //TO DO: Get the new poll timer value and cancel the existing timer for the
				 //Monitoring agent/ reset the timer to expire after new delay interval.
				 char *token;
				 uint8_t loop_count = 0;
				 token = strtok(msg," ");
				 int mon_id;
				 int new_poll_time;
				 struct mon_agent *m;
				 dzlog_debug("Received message is :%s %d\n",msg,strlen(msg));
				 while( token != NULL )
				 {
					 if(loop_count == 1) //MSG_TYPE is followed by MON_ID
					 {
						 mon_id = atoi(token);
					 }
					 else if(loop_count == 2)
					 {
						 new_poll_time = atoi(token);
					 }
					 dzlog_info("\n should fetch sock for mon_id %d \n",mon_id);
					 token = strtok(NULL," ");
					 loop_count++;
				 }

				 struct mon_agent *agent_obj = monitoring_agent_mapping_find(mon_id); 			// Get the timer ID associated with the Monitoring Agent & CANCEL THE CORRESPONDING TIMER.
				 int timer_id = agent_obj->timer_id;
				 if(timer_id != 0)
				 {
					 dzlog_debug("Destroying timer_id %d \n",timer_id);
				 	 zloop_timer_end (loop,timer_id);

					 //Check if there are other agents with this polling requirement, if so, create a new timer for one of them.
					 HMAP_FOR_EACH (m,hmap_node,&agent_hmap){
						if (m != NULL && m->poll_time == agent_obj->poll_time && m->timer_id == 0 && agent_obj->mon_id != m->mon_id) // if the poll-time is same as the current monitoring agent's poll-time and timer_id is a valid value
						{
							 timer_id = zloop_timer(loop, m->poll_time, 0, s_timer_event, m->mon_id); //0 ==> indicates that the timer should run indefinite number of times until it is canceled by MON_STOP.
							 m->timer_id = timer_id;			 										//Save this timer_id with Monitoring agent parameters for later retrieval.
							break;
						}
					 }
				 }
				 agent_obj->timer_id = 0; // assign '0' to timer_id.

				 uint8 timer_exist = false;
				 HMAP_FOR_EACH (m,hmap_node,&agent_hmap){
					if (m != NULL && m->poll_time == new_poll_time && m->timer_id != 0) // if the poll-time is same as the current monitoring agent's poll-time and timer_id is a valid value
					{
						timer_exist = true;
						break;
					}
				 }

				 if(!timer_exist) // if there is no timer already for this polling interval, start polling the stuff.
				 {
					 timer_id = zloop_timer(loop, delay, 0, s_timer_event, agent_obj->mon_id); //0 ==> indicates that the timer should run indefinite number of times until it is canceled by MON_STOP.
					 agent_obj->timer_id = timer_id;			 										//Save this timer_id with Monitoring agent parameters for later retrieval.
					 dzlog_info("Updated the polling interval timer id:%d\n",timer_id);
				 }
				 else
				 {
					 dzlog_info("Timer already exists with some other monitoring agent with id %d\n",m->mon_id);
				 }

				 agent_obj->poll_time = new_poll_time;  //Update the new poll time in the agent object.

				 zstr_send (rpc_repservice, "OK");//TO DO: remove this if the new solution does not work. Do an immediate REPLY OK to NETCONF Server at this point.
			 }
			 else
			 {
				if(mon_params != NULL)
					free(mon_params);
			 }
		 }
	 	 zstr_free (&msg);// TO DO: Move this statement before the closing braces, if the new solution does not work. because zstr_recv is used, free the memory at this point.
	}

    return 0;
}

/**********************************************************************************
 * Check if the message is meant for device configuration.
 * If yes,handle it in the NETCONF thread itself. Do not forward it to main thread.
 * *********************************************************************************/

int  check_msg_type(char* msg)
{
	int ret_status = false;

	if(strstr(msg,"configure-device-id")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to extract device-id is:%s \n", (char *)buffer->content);

	  xmlChar* key_content = parseXml(buffer,node,"switch-identification");
	  int device_id = atoi((char*)key_content);
	  set_device_identification_number(device_id);
	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	if(strstr(msg,"configure-tcam-timer")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to extract tcam timer is:%s \n", (char *)buffer->content);

	  xmlChar* key_content = parseXml(buffer,node,"tcam-timer");
	  tcam_timer_interval = atoi((char*)key_content);
	  //set_device_identification_number(timer);

	  zloop_timer_end(loop, tcam_timer_id);
	  tcam_timer_id = zloop_timer(loop, tcam_timer_interval, 0, s_fp_group_status_check_timer_event,NULL);
	  dzlog_info("New TCAM timer interval set \n");
	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	if(strstr(msg,"enable-disable-action")!=NULL)
	{
           //Typically "NETCONF merge" (similar to MON_PARAM_CHANGE) operation should be able to change the actions associated with an agent. But, the implementation on the
		   //NETCONF server side to detect the changes done is compilcated, hence this workaround custom RPC operation.This should let the user to enable a
		   //certain action even after the parameter is configured.
		  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
		  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
		  xmlBufferPtr buffer = xmlBufferCreate();
		  int size = xmlNodeDump(buffer,NULL,node,0,0);
		  dzlog_info("\n Contents of the xml node to Get MON-STATUS is:%s \n", (char *)buffer->content);

		  xmlChar* key_content = parseXml(buffer,node,"mon-id");
		  xmlChar* action = parseXml(buffer,node,"action");
		  int mon_id = atoi((char*)key_content);
		  dzlog_info("\n Monitoring ID extracted is %d and action extracted is %s\n",mon_id,(char*)action);
		  struct mon_agent* mon_param =  monitoring_agent_mapping_find(mon_id);
		  if(mon_param != NULL)
		  {
			  //By default, these bits are set to 0. which implies that the actions should come explicitly from the controller.
			  //Otherwise a enable-disable-action could be used at any point in time.
			  int row_num = mon_param -> current_state;
			  int ret = convert_action_string_to_int(action);
			  int toggle_bit=0;
			  if(ret == NOTIFY_CNTLR)
			  {
				  mon_param->notify_cntrl ^= 1 << toggle_bit;   // Toggle the first bit to indicate change in action
			  }
			  else if(ret == NOTIFY_CNTLR_AFTER_N_INTERVALS)
			  {
				  mon_param->notify_cntrl_after_interval ^= 1 << toggle_bit;   // Toggle the first bit to indicate change in action
			  }
			  else if(ret == NOTIFY_LINK_UTILIZATION)
			  {
				  dzlog_info("value before link utilizazion flag is reset:%d \n",mon_param->notify_link_utilization);
				  mon_param->notify_link_utilization ^= 1 << toggle_bit;   // Toggle the first bit to indicate change in action
				  dzlog_info("value after link utilizazion flag is reset:%d \n",mon_param->notify_link_utilization);
			  }
		  }
		  xmlBufferFree(buffer);
		  xmlFreeDoc(doc);
		  zstr_send (rpc_repservice, "OK");
		  return false;
	}
	if(strstr(msg,"modify-event-thresholds")!=NULL)
	{
           //Typically "NETCONF merge" (similar to MON_PARAM_CHANGE) operation should be able to change the actions associated with an agent. But, the implementation on the
		   //NETCONF server side to detect the changes done is compilcated, hence this workaround custom RPC operation.This should let the user to enable a
		   //certain action even after the parameter is configured.
		  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
		  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
		  xmlBufferPtr buffer = xmlBufferCreate();
		  int size = xmlNodeDump(buffer,NULL,node,0,0);
		  dzlog_info("\n Contents of the xml node to Get MON-STATUS is:%s \n", (char *)buffer->content);

		  xmlChar* key_content = parseXml(buffer,node,"mon-id");
		  xmlChar* packets_thresh = parseXml(buffer,node,"statPackets");
		  xmlChar* bytes_thresh = parseXml(buffer,node,"statBytes");
		  int mon_id = atoi((char*)key_content);
		  int staPacketsThresh = atoi((char*)packets_thresh);
		  int bytesThresh = atoi((char*)bytes_thresh);

		  dzlog_info("\n Monitoring ID extracted is %d and threshold extracted is %d %d\n",mon_id,staPacketsThresh,bytesThresh);
		  struct mon_agent* mon_param =  monitoring_agent_mapping_find(mon_id);
		  if(mon_param != NULL)
		  {
			  //By default, these bits are set to 0. which implies that the actions should come explicitly from the controller.
			  //Otherwise a enable-disable-action could be used at any point in time.
			  int row_num = mon_param -> current_state-1;
			  struct table* table_row = get_row_of_state_machine(row_num,mon_param);
			  table_row->statPktsThreshold = staPacketsThresh;
			  table_row->statBytesThreshold = bytesThresh;
			  table_row->packet_threshold_exists = true;
		  }
		  xmlBufferFree(buffer);
		  xmlFreeDoc(doc);
		  zstr_send (rpc_repservice, "OK");
		  return false;
	}
	else if(strstr(msg,"fp-entry-threshold")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to extract field processor limits is:%s \n", (char *) buffer->content);

	  xmlChar* key_content = parseXml(buffer,node,"total-entries-threshold");
	  int total_entries_threshold = atoi((char*)key_content);

	  key_content = parseXml(buffer,node,"min-free-entries-per-device");
	  int min_free_entries_per_device = atoi((char*)key_content);

	  tcam_thresholds.max_allowed_tcam_entries = total_entries_threshold; //Set the Global variables used for TCAM limit imposition here.
	  tcam_thresholds.min_free_entries_per_device = min_free_entries_per_device;

	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	//HHH MSG
	else if(strstr(msg,"port-threshold")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to get port threshold is:%s \n", (char *) buffer->content);

	  xmlChar* key_content = parseXml(buffer,node,"port-number");
	  int port_no = atoi((char*)key_content);

	  key_content = parseXml(buffer,node,"port-data-threshold");
	  int max_data_threshold = atoi((char*)key_content);

	  dzlog_info("\n Port threshold extracted for the port number %d is %d\n",port_no, max_data_threshold);
	  
	  //Set the Global variables value for the struct.
	  port_stats.port_number = port_no;
	  port_stats.max_port_data = max_data_threshold; 
	  

	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	//HHH 1D MSG
	else if(strstr(msg,"get-1d-hhh")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to get port stats is:%s \n", (char *) buffer->content);

	  xmlChar* key_content1 = parseXml(buffer,node,"port-number");
	  int port_no = atoi((char*)key_content1);
       xmlChar* key_content = parseXml(buffer,node,"mon-id");
	   int mon_id = atoi((char*)key_content);
	   dzlog_info("\n Monitoring ID extracted is %d\n",mon_id);
	  struct mon_agent* mon_param =  monitoring_agent_mapping_find(mon_id);
 	  if(mon_param != NULL)
	 { 
	  uint64 statBytes = mon_param -> mon_state_info.flow_stats_val.statBytes;
	  dzlog_info("\n Data asked for Port number %d n",port_no);
      send_1D_HHH_port_notification(statBytes,publisher,port_no,mon_id);
	 }

	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	//HHH 2D MSG
	else if(strstr(msg,"get-2d-hhh")!=NULL)
	{
	  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
	  xmlNodePtr node = doc-> children;
	  xmlBufferPtr buffer = xmlBufferCreate();
	  int size = xmlNodeDump(buffer,NULL,node,0,0);
	  dzlog_debug("\n Contents of the xml node to get port threshold is:%s \n", (char *) buffer->content);
	
	  xmlChar* key_content1 = parseXml(buffer,node,"port-number");
	  int port_no = atoi((char*)key_content1);
	  
       xmlChar* key_content = parseXml(buffer,node,"mon-id");
	   int mon_id = atoi((char*)key_content);
	   dzlog_info("\n Monitoring ID extracted is %d\n",mon_id);
	  struct mon_agent* mon_param =  monitoring_agent_mapping_find(mon_id);
 	  if(mon_param != NULL)
	 { 
	  uint64 statBytes = mon_param -> mon_state_info.flow_stats_val.statBytes;
	  uint64 source_ip_addr = mon_param->source_ip;
	  dzlog_info("\n Data asked for Port number %d n",port_no);
      send_2D_HHH_port_notification(statBytes,publisher,port_no,source_ip_addr,mon_id);
	 }
	  xmlBufferFree(buffer);
	  zstr_send (rpc_repservice, "OK");
	  return false;
	}
	else if(strstr(msg,"mon_status")!=NULL)
	{
		  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
		  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
		  xmlBufferPtr buffer = xmlBufferCreate();
		  int size = xmlNodeDump(buffer,NULL,node,0,0);
		  dzlog_debug("\n Contents of the xml node to Get MON-STATUS is:%s \n", (char *)buffer->content);

		  xmlChar* key_content = parseXml(buffer,node,"mon-id");
		  int mon_id = atoi((char*)key_content);
		  dzlog_info("\n Monitoring ID extracted is %d\n",mon_id);
		  struct mon_agent* mon_param =  monitoring_agent_mapping_find(mon_id);
		  if(mon_param != NULL)
		  {
			  int row_num = mon_param -> current_state-1;
			  uint64 statPackets = mon_param -> mon_state_info.flow_stats_val.statPackets; //contains the values collected at the end of the last interval.
			  uint64 statBytes = mon_param -> mon_state_info.flow_stats_val.statBytes;

			  dzlog_debug("MON_STATUS being generated with values %d %d \n",statPackets,statBytes);
			  int device_id = get_device_identification_number();
			  char *notification_content;
			  struct ds mon_status; //struct ds defined in Open-Vswitch Library.
			  ds_init(&mon_status);
			  ds_put_format(&mon_status, "<mon_status><mon-id>%d</mon-id><device-id>%d</device-id><stat-packets>%llu</stat-packets><stat-bytes>%llu</stat-bytes></mon_status>"
										  ,mon_id,device_id,statPackets,statBytes);

			  char *notif_content = ds_steal_cstr(&mon_status); // Convert the notification content into a normal C-style string.
			  zstr_send(rpc_repservice,notif_content); //Send mon_status to the controller.

			  //send_mon_status_ack_notification(rpc_repservice,notif_content); //send the notification.
			  xmlBufferFree(buffer);
			  xmlFreeDoc(doc);
			  free(notif_content);
		  }
		  else
		  {
			  zstr_send(rpc_repservice,"OK"); //Just return OK.
		  }
		  return false;
	}
	else if(strstr(msg,"port-statistics-get")!=NULL)
	{
		  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
		  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
		  xmlBufferPtr buffer = xmlBufferCreate();
		  int size = xmlNodeDump(buffer,NULL,node,0,0);
		  dzlog_debug("\n Contents of the XML node to GET Port Statistics is:%s \n", (char *)buffer->content);

		  xmlChar* key_content = parseXml(buffer,node,"port-index");
		  int port_index = atoi((char*)key_content);
		  dzlog_info("\n PORT INDEX extracted is %d\n",port_index);

		  struct port_stats_val_t port_stats = per_port_pkt_stats(DEFAULT_UNIT,port_index);
		  send_port_statistics_reply(rpc_repservice,port_stats);
		  xmlBufferFree(buffer);
		  xmlFreeDoc(doc);
		  return false;
	}
	else if(strstr(msg,"clear-port-stats")!=NULL)
	{
		  xmlDocPtr doc = xmlReadMemory(msg,strlen(msg),NULL,NULL,0);
		  xmlNodePtr node = doc-> children;   //Get the node pointer to the xml message.
		  xmlBufferPtr buffer = xmlBufferCreate();
		  int size = xmlNodeDump(buffer,NULL,node,0,0);
		  dzlog_debug("\n Contents of the XML node to clear Port Statistics is:%s \n", (char *)buffer->content);

		  xmlChar* key_content = parseXml(buffer,node,"port-index");
		  int port_index = atoi((char*)key_content);
		  dzlog_info("\n Monitoring ID extracted is %d\n",port_index);

		  port_statistics_clear(port_index); //Clear the statistics for a port. NOTE NACK is not handled.It is assumed that the operation is invoked on valid ports at the moment.
		  xmlBufferFree(buffer);
		  xmlFreeDoc(doc);
		  zstr_send(rpc_repservice,"OK");
		  return false;
	}
	else if(strstr(msg,"get_field_processor_group_status")!=NULL)
	{
		send_fp_group_status_info(rpc_repservice,DEFAULT_UNIT);
		return false;
	}
	dzlog_info("\n It is a monitoring message\n");
	return true; // It is a monitoring message. Send this to the main thread.
}


/*********************************************************************************************************************************************
 *  validate_mon_activity_params: This function extracts the monitoring parameters from the XML message sent by NETCONF server on the switch.*
 *  char *msg-string  --> IN: Fetch the message Type and file Name from this parameter. 														 *
 *  void *mon_params -->  IN/OUT parameter: Fill the structure if it is MON_HELLO/MON_PARAM message.											 *
 ********************************************************************************************************************************************/

int validate_mon_activity_params(char* msg_string, struct mon_agent * mon_params)
{
	int msg_type = 0xFF;
	char *xml_filename;
	char msg[100];
	strcpy(msg,msg_string);
	char *token;
	int loop_count = 0;
	token = strtok(msg," ");
	msg_type = atoi(token);

	if(msg_type == MON_HELLO)
	{
		while( token != NULL)
		{
		  token = strtok(NULL," ");
		  dzlog_debug("token extracted :%s %d\n",token,strlen(token));
		  if(strstr(token,"xml")!=NULL)
		  {
			 xml_filename = (char*)malloc(strlen(token)+1);
			 strcpy(xml_filename,token);
			 dzlog_debug("FileName extracted %s \n",xml_filename);
		  }
		  token = strtok(NULL, msg);
		  loop_count++;
		}

		FILE *fp = NULL;   		// Implies that a corresponding file name is available within the MSG.
		fp = fopen(xml_filename,"r");
		if(fp != NULL)
		{
		    FILE *fp2 = fopen("/tmp/temp_file.txt","w+");
		    int p;
		    while((p=getc(fp))!=EOF)
		    {
		        fputc(p,fp2);
		        if (p==32)// since the ascii code for a blank space is 32
		        {
		            while((p=getc(fp))==32){}
		            fputc(p,fp2);
		        }
		    }
		    fclose(fp);
		    fclose(fp2);
			//file exists at the location specified in the MSG_STRING, use libxml2 based APIs to fetch the monitoring ID and monitoring Type.
			//To DO: release the memory inside remove_AgentNode function.
			 streamFile("/tmp/temp_file.txt",mon_params);
			 int ret = remove("/tmp/temp_file.txt"); // Remove the file as everything has been already copied onto mon_params.
			 ret = remove(xml_filename); //Remove the original file also.
			 if(ret != 0)
				 dzlog_debug("%s File could not be deleted \n",xml_filename);
		}
		free(xml_filename);
	}
	else if(msg_type == MON_PARAM_CHANGE)
	{
		return (msg_type);  //MON_PARAM_CHANGE shall have the format MSG_TYPE MON_ID POLLTIME, each parameter separated by a SPACE.
 	}
	else if(msg_type == MON_STOP)
	{
	   //TO DO:Extract the MON_ID from mon_params, that is all required. MSG_TYPE is already extracted.
	   //TASK for tomorrow along with the FP, 1st January 2017.
	   return(msg_type);
	}
	return(msg_type);
}

//This is not required any more, integrated this functionality into the main thread.
/*************************************************************************************************
 * This is the receiver thread that interacts with the NETCONF server.
 * There are two sockets basically.
 * 1. REQ-REPLY socket that receives monitoring requests.
 * 2. PUB-SUB socket that is used to send notifications to the controller via NETCONF server.
 *************************************************************************************************/
//void* recv_netconf_thread(void * data)
//{
//    //  Socket to talk to clients
//	int ret_status,rc;
//    publisher = zsock_new (ZMQ_PUB);
//    if(publisher == NULL)
//    {
//    	dzlog_error("Invalid \n");
//    	return NULL;
//    }
//    zsock_bind (publisher, "tcp://*:5556"); // use  ipc://publisher.ipc" at a later point, for some reason ipc sockets are not working at the moment.
//
//    main_thread_pair = zsock_new (ZMQ_PAIR);// Receiver socket for inter-thread signals.
//    zsock_connect (main_thread_pair, "inproc://zmq_netcof_thread_pair");
//
//    dzlog_info("Waiting for messages to appear on PAIR/REPLY Sockets\n");
//    zmq_pollitem_t items = {
//					/*{ zsock_resolve(repservice),0, ZMQ_POLLIN, 0 },*/
//					{ zsock_resolve(main_thread_pair),0, ZMQ_POLLIN, 0 }};
//    while (1)/*!s_interrupted*/
//    {
//		zmq_poll (&items, 1, 0); // Do not poll indefinitely.
//		if (items.revents & ZMQ_POLLIN)
//		{
//			char *string = zstr_recv (main_thread_pair); //string contains the NOTIFICATION to be sent to the subscriber thread on NETCONF Server.
//			if (string != NULL)
//			{
//				//send the Notification to the SUBSCRIBER via PUBLISHER SOCKET String just contains the notification. Pass it ON as it is to the TRANS-API.
//				rc = zstr_sendm(publisher,"mon_event_notification"); //This is the Envelope of the message.
//				zstr_send (publisher, string);// string is expected to contain the notification FILE name which contains ready to send XML Content.
//				zstr_free (&string);
//				dzlog_debug("Notification sent to the Controller via NETCONF server %s \n");
//				string = NULL;
//			}
//			else
//			{
//				break;
//			}
//		 }
//    }
//    dzlog_info("Closing sockets \n");
//    zsock_destroy (&publisher);
//    zsock_destroy (&main_thread_pair);
//    return NULL;
//}

/**********************************************************************************************************
 * This is the main function of the process that interacts with ASIC by invoking appropriate OpenNSL APIs.
 *
 *
 **********************************************************************************************************/

int main (void)
{
    pthread_t recv_netconf_id;
	int major,minor,patch;
	

	int rc = dzlog_init("/etc/zlog.conf","my_cat");
	if (rc) {
		printf("log init failed\n");
		return -1;
	}

    if (daemon(0, 0) != 0) 	//Daemonize the process so that it runs in the background
    {
        printf("Going to background failed (%s)", strerror(errno));
        return -1;
    }

	zmq_version(&major,&minor,&patch);
	dzlog_info("0MQ version in use %d %d %d \n",major,minor,patch);

    rc = init_open_nsl();    //Initialize OpenNSL here:
    if(!rc)
    {
    	//NSL Driver initialization failed. No point in running this service.
    	dzlog_error("Exiting the application as NSL driver could not be initialized.\r\n");
    	return -1;
    }
    bcm_application_init(); //Call all SWITCH control APIs.

#ifdef opennsl
    rc = mon_fp_stats_group_create(DEFAULT_UNIT); //Create the FP statistics group.
#endif
    if(!rc)
    {
    	//NSL Driver initialization failed. No point in running this service.
    	dzlog_error("Exiting the application as FP statistics group could not be created.\r\n");
    	return -1;
    }


    //signal registration follows here.
    s_interrupted = false; // No Interrupts such as SIGTERM, ctrl-c, If so, the flag is set to true.
    zsys_handler_set (NULL); //CZMQ does set up its own signal handling to trap SIGINT and SIGTERM.Disable it, such that it can be handled by custom signal handlers w/ this application.
	//s_catch_signals ();

    //Create a ZEROMQ_PAIR Socket so that main can speak to communication main_thread.This socket shall be used to PUBLISH notifications as well as to receive MONITORING Requests.

    /*inter_thread_pair = zsock_new(ZMQ_PAIR);
	if(inter_thread_pair != NULL)
    	zsock_bind (inter_thread_pair, "inproc://zmq_netcof_thread_pair"); // Changed the API from ZMQ to CZMQ.

    int ret = pthread_create(&recv_netconf_id,NULL,recv_netconf_thread,NULL);     //CREATE the recv_netconf_thread that communicates with the NETCONF Server.
    pthread_detach(recv_netconf_id);
    if(ret != 0)
    {
      dzlog_error("Exiting the application as NETCONF communication could not be initiated.\r\n");
      return 0;
    }*/


    publisher = zsock_new (ZMQ_PUB);
    if(publisher == NULL)
    {
    	dzlog_error("Coulnd not create publisher thread.\n");
    	return NULL;
    }
    zsock_bind (publisher,"tcp://*:5556"); // use  ipc://publisher.ipc" at a later point, for some reason ipc sockets are not working at the moment.


    // Create an RPC reply thread that interacts directly with the Monitoring application.
    rpc_repservice = zsock_new(ZMQ_REP);
	rc= zsock_bind (rpc_repservice, "tcp://*:5000"); // Changed the API from ZMQ to CZMQ.
    if(rc == -1)
    	dzlog_error("Error in binding the rpc_repservice socket \n");

    loop = zloop_new();
	zmq_pollitem_t item = { zsock_resolve(rpc_repservice),0, ZMQ_POLLIN, 0 };

    zloop_poller (loop, &item, s_netconf_socket_event,NULL);

    // TCAM status check timer expires every 1 minute. TO DO: make the timer configurable from the controller.
     tcam_timer_id = zloop_timer(loop, tcam_timer_interval, 0, s_fp_group_status_check_timer_event,NULL);

#ifdef CHECK_POLLING_ACCURACY
    int poll_accuracy_timer = zloop_timer(loop,60000,0,s_polling_accuracy_dump_timer,NULL);
#endif

    zloop_start(loop);

    zloop_destroy (&loop);
    //zsock_destroy(&inter_thread_pair); //This point is reached only if there is an interrupt.
    zsock_destroy (&publisher);
    zsock_destroy(&rpc_repservice);

    zlog_fini(); //Close the Zlog instance.
    return 0;
}
