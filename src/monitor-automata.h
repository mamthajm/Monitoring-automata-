/**
 * @file monitor-automata.h
 * @brief 
 * @date 2016/10/30
 *
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

#ifndef _MONITOR_AUTOMATA_H_
#define _MONITOR_AUTOMATA_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vector.h"
#include <zmq.h>
#include <czmq.h>
#include "opennsl/link.h"
#include "opennsl/init.h"
#include "opennsl/error.h"
#include "opennsl/cosq.h"
#include "opennsl/vlan.h"
#include "opennsl/switch.h"
#include "opennsl/stat.h"
#include "sal/driver.h"
#include "sal/types.h"
#include "examples/util.h"
#include "opennsl/field.h"
#include "opennsl/l2.h"
#include "opennsl/l3.h"
#include "opennsl/port.h"
#include "opennsl/fieldX.h"
#include "opennsl/types.h"

#include <libxml/xmlreader.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <hmap.h>
#include <stdio.h>
#include <hash.h>
#include<dynamic-string.h>
#include "zlog.h"

//#define CHECK_POLLING_ACCURACY
#define opennsl
//#define ACCTON_7712


#define boolean int
#define true 1
#define false 0

#define CHECK_BIT(var,pos)((var)&(1<<(pos)))

#define ZMQ_POLL_SOCK 50

#define MON_HELLO 10
#define MON_PARAM_CHANGE 20
#define MON_STOP 40
#define DEVICE_ID_CONFIGURATION 50
#define TCAM_LIMIT_CONFIGURATION 60

//Asynchronous messages from Server to Clients (SDN Controller)
#define MON_GEN_EVENT_NOTIFICATION 30 //Generic message type for notification to be sent.
#define MON_START_NOTIFICATION 31
#define MON_SWITCH_NOTIFICATION 32

//Symbolic constants for the BST statistics Monitoring.

#define DEFAULT_VLAN 1
#define MAX_SWITCH_UNIT_ID 1
#define MAX_COUNTERS 4 //ideally this should be 6 for PORT BASED, but excluding last two counters defined in id_list for time being.
#define MAX_BST_COUNTERS 11  //This is including the device level BST counters.
#define MAX_COSQ_COUNT 8
#define DEFAULT_UNIT 0
#define MX_DIGITS_IN_CHOICE 5
#define MAX_STAT_COUNTERS 18  //Port statistics
#define MAX_STAT_EVENTS (MAX_STAT_COUNTERS + MAX_COUNTERS*8)   //8 indicates the number of COSQ Count.
#define EVENTS_NAME_SIZE 50
#define NUM_FP_FLOW_STATS 2


#ifdef BST
	#define _BST_STAT_ID_DEVICE                    opennslBstStatIdDevice
	#define _BST_STAT_ID_EGR_POOL                  opennslBstStatIdEgrPool
	#define _BST_STAT_ID_EGR_MCAST_POOL            opennslBstStatIdEgrMCastPool
	#define _BST_STAT_ID_ING_POOL                  opennslBstStatIdIngPool
	#define _BST_STAT_ID_PORT_POOL                 opennslBstStatIdPortPool
	#define _BST_STAT_ID_PRI_GROUP_SHARED          opennslBstStatIdPriGroupShared
	#define _BST_STAT_ID_PRI_GROUP_HEADROOM        opennslBstStatIdPriGroupHeadroom
	#define _BST_STAT_ID_UCAST                     opennslBstStatIdUcast
	#define _BST_STAT_ID_MCAST                     opennslBstStatIdMcast
	#define _BST_STAT_ID_EGR_UCAST_PORT_SHARED     opennslBstStatIdEgrPortPoolSharedUcast
	#define _BST_STAT_ID_EGR_PORT_SHARED           opennslBstStatIdEgrPortPoolSharedMcast
	#define _BST_STAT_ID_RQE_QUEUE                 opennslBstStatIdRQEQueue
	#define _BST_STAT_ID_RQE_POOL                  opennslBstStatIdRQEPool
	#define _BST_STAT_ID_UCAST_GROUP               opennslBstStatIdUcastGroup
	#define _BST_STAT_ID_CPU                       opennslBstStatIdCpuQueue
	#define _BST_STAT_ID_MAX_COUNT                 opennslBstStatIdMaxCount
#endif

#define _BRCM_56960_DEVICE_ID                  0xb960

#define _BRCM_IS_CHIP_TH(_info)               \
                 (((_info).device == _BRCM_56960_DEVICE_ID))

//zlog_category_t *c;  //Zlog category

typedef struct {
  opennsl_bst_stat_id_t bid;
  char name[50];
} bst_counter_t;

extern bst_counter_t id_list[MAX_BST_COUNTERS];

extern int device_identification_nr;

struct bst_val_counters
{
	uint64 val[MAX_COUNTERS][MAX_COSQ_COUNT];  //uint64 --> refer to the API opennsl_cosq_bst_stat_get from OpenNSL documentation.
};

struct port_stats_val_t
{
	uint64 val[MAX_STAT_COUNTERS];
};

// This is not used anymore.
struct bst_mon_type_params
{
	int threshold_changed;
	int bid_threshold[MAX_COUNTERS][2]; //Each BID with its new threshold value as set by the user?
	int state_table_changed; //indicates if there were any changes made to the state-table.
};


enum monitoring_type{
	FP,
	STATS,
	FP_STATS,
	BST_STATS,
	BST_FP_STATS,
	BST,
	BST_FP,
	P_STATS
};

//Monitoring activity definitions:
enum mon_action {GOTO_NXT_STATE,
				 NOTIFY_CNTLR,
				 NOTIFY_PORT_STATS, //HHH
				 NOTIFY_CNTLR_AFTER_N_INTERVALS,
				 NOTIFY_LINK_UTILIZATION,
				 NO_ACTION,		/*TO REMAIN IN THE SAME STATE*/
				 LINK_STATE_ENABLE,
				 LINK_STATE_DISABLE,
				 NUM_OF_ACTIONS /*This shall be the last action*/};

typedef struct input_events
{
	char name[32]; //event name.
	int value; //Threshold value.

}input_params_t;

struct fp_flow_stats_t{
	uint64 statPackets;
	uint64 statBytes;
};

struct fp_mon_qset_attributes_t{
	unsigned int priority;
	unsigned int ingress_port_mask;
	opennsl_ip_t src_ip;
	opennsl_ip_t dst_ip;
	opennsl_mac_t src_mac;
	opennsl_mac_t dst_mac;
	opennsl_l4_port_t  src_port;
	opennsl_l4_port_t  dst_port;
	opennsl_ip_t src_ip_mask;
	opennsl_ip_t dst_ip_mask;
	opennsl_mac_t src_mac_mask;
	opennsl_mac_t dst_mac_mask;
	opennsl_l4_port_t  src_port_mask;
	opennsl_l4_port_t  dst_port_mask;
};

struct table    //TO DO: Each monitoring task shall have its own ENUM that defines symbolic constants.
{
	int state;
	int num_of_row_evnts;
	input_params_t *event;		 //Assign a value from ENUM created specifically for each monitoring task.
	uint8 flow_exists;			 //Set when the monitoring parameters are extracted from the MON_HELLO message and if there is a flow that needs to be monitored.
	uint8 bst_threshold_exists;  //Set when the monitoring parameters are extracted from the MON_HELLO message.
	uint8 port_threshold_exists; //Set when the monitoring parameters are extracted from the MON_HELLO message.
	uint8 packet_threshold_exists;
	struct fp_mon_qset_attributes_t flow;
	uint64 statPktsThreshold;
	uint64 statBytesThreshold;
	uint8 flow_events_bitmap;
	uint32 port_events_bitmap;
	int num_of_actions;
	int *action; //Assign values from ENUM mon_action to this array elements.
	int next_state; //NOTE: This should be a number starting with 1 and max possible number is 255 as one TCAM table can have up-to 256 entries only.
};

/*********************************************************************
 * This Data Structure holds the monitoring status information
 * that was transferred as STATE INFORMATION from some other switch.
 * ******************************************************************/
struct initial_state_rcvd_frm_cntrllr
{
	uint64 statPackets;
	uint64 statBytes;
};

struct monitoring_state_t
{
	struct fp_flow_stats_t flow_stats_val; 		//flow level statistics populated by the monitoring process at the beginning of each interval.
	struct fp_flow_stats_t flow_stats_val_delta;//Populated by the monitoring process at the end of each interval.
	struct fp_flow_stats_t flow_stats_val_interval; //flow level statistics populated for an interval by the monitoring process at the end of each interval.
	int port_stats_val[MAX_STAT_COUNTERS];		//port level statistics populated by the monitoring process at the beginning of each interval.
	int port_stats_val_delta[MAX_STAT_COUNTERS];//Port level statistics populated by the monitoring process at the end of each interval.
	int port_stats_val_interval[MAX_STAT_COUNTERS];// Difference between start & end time of an interval: Port level statistics for an interval populated by the monitoring process at the end of each interval.
#ifdef BST
	struct bst_val_counters bst_stats;			//BST statistics populated by the monitoring process at the beginning of each interval.
	struct bst_val_counters bst_stats_delta;	//BST statistics populated by the monitoring process at the end of each interval.
	struct bst_val_counters bst_stats_interval;	//BST statistics for an interval populated by the monitoring process at the end of each interval.
#endif
};

struct mon_agent
{

	struct hmap_node hmap_node;   /*hash map nodes in the HashMap "agents"*/
	int mon_id;		 			  //Obtained from the controller.
	int timer_id;    			  //Obtained from the controller.
	int mon_msg_type;             //HELLO,PARAM_CHANGE or STOP.
	int mon_type; 				  //BST, Port Statistics or flow monitoring. Value 1 -> Only Flow monitoring; Value = 2 -> Only Port statistics thresholds defined; Value 4 -> BST thresholds defined. Use the combination of values to set all the three monitoring types.
	int poll_time;  			  //Obtained from the controller.
	int tot_time_intervals; 	  //Total number of time intervals(measured in poll-time) after which controller should be notified of an event: configured from the controller.
	int curr_interval_count;      //Indicates how many poll-intervals have occurred so far.
	int port_index; 			  //0xFFFF-> invalid/system level monitoring.
	uint8 source_ip;			  //source ip
	int state_table_id;           //Obtained from the controller.
	int total_states;             //Obtained from the controller.
	int current_row_count;        //WARNING: DO NOT USE THE VARIABLE. Used in populating the state-machine table. Once the state-machine is populated from Controller messages,this variable becomes unused.
	int flow_entry_id;            //Created by the monitoring process based on the parameters received from the controller.There will be at the max one flow installed. i.e. 1 row -> 1 flow. ==> At any point in time, there can be only one flow.
	int stat_id;                  //Created by the monitoring process based on the parameters received from the controller.This may not be required after all.
	int link_utilization_threshold;
	uint8 flow_priority;		  //Flag to indicate if the flow to be installed has some explicit priority.
	uint8 current_state;		  //Indicates the current state row of the state table this monitoring agent is working on.Popuated by the monitoring agent.
	uint8 mon_state_available;    // Signifies if there is any monitoring data already available when the monitoring request arrived.
	struct monitoring_state_t mon_state_info; //Monitoring information such as flow, port and BST statistics collected by this agent.
	struct initial_state_rcvd_frm_cntrllr init_state;
	struct table *state_table_rows; // pointer to an array of rows that contains XFSM tuples (S,I,O,T).
	uint8 notify_cntrl;
	uint8 notify_cntrl_after_interval;
	uint8 notify_link_utilization;
#ifdef CHECK_POLLING_ACCURACY
	//below variables are only used for instrumentation.
	int64 start_time;  //start_time of the previous interval when the statistics were polled
	int64 max_delay;
	int64 min_delay;
	uint8 first_interval;
	uint64 delay_0;
	uint64 delay_1;
	uint64 delay_minus_1;
#endif
};

//This declaration is rather unused at the moment.
struct port_monitor_params_t
{
	int port_index;
	int link_monitor_flag;
	int bst_monitor_flag;
	int port_statistics_flag;
	/*To define a list of mon_agents per port, we can use something like struct AgentNode present in lists.h*/
	struct mon_agent *mon_params; // It should be possible to associate a list of state-machines to the port.For instance, One for BST; one for LT and One for Port Statistics.
};


//ZEROMQ-CZMQ based definitions follow here.

zsock_t *publisher;
zsock_t *repservice;
zsock_t *rpc_repservice;
zsock_t *main_thread_pair;
zsock_t *inter_thread_pair;
zloop_t *loop;
int s_interrupted;


static zmq_pollitem_t socket_items[ZMQ_POLL_SOCK];
static size_t sockItemsSize;

/*PORT statistics definitions*/

//Below definition is taken from ops-stats.c source file of OpenSwitch project.
//Source code is available under APACHE license 2.0
extern opennsl_stat_val_t stat_arr[MAX_STAT_COUNTERS];


//This is mostly unused at the moment!
typedef struct {
  char name[50];
} port_counter_names_t;


//Below two structure definitions are not required any more. -->
struct notification_t
{
	uint8_t valid;
	char name[EVENTS_NAME_SIZE];
	uint64 value;
};

struct encapsulate_notification
{
	struct notification_t notify[MAX_STAT_EVENTS];
	int num_of_events;
	uint8 end_of_interval; //Used to indicate that the end of time interval window is reached.
};
//Below two structure definitions are not required any more. <--

//Field processor related code here!
#define NUM_MON_FP_STATS   4
#define MAX_SWITCH_UNITS   1    // To make it compatible with OpenSwitch.
#define LINK_MON_ON 1


opennsl_field_qset_t fp_mon_stats_qset;
opennsl_field_aset_t aset;
extern opennsl_field_group_t fp_mon_stats_grps;

#define ipv4_mon_rx_packets     	   0
#define ipv4_mon_rx_bytes	    	   1
#define ipv4_mon_rx_dropped_packets    2
#define ipv4_mon_rx_dropped_bytes      3

//Below field statistics array is used for Link Utilization purpose. Since it is common for all, declaring it as a global variable. 
//Should this be a local variable?
extern opennsl_field_stat_t lu_stat_ifp[2];


struct port_stats_validate_t{
	int ret_value;
	int index;
};


struct tcam_thresholds_t
{
	uint64 max_allowed_tcam_entries; // This is the maximum allowed limit for the FP TCAM slice dedicated to monitoring.
	uint64 max_allowed_counters;	 // Maximum allowed counters that could be used for monitoring purposes.
	uint64 min_free_entries_per_device;	//This is the minimum number of free entries that should be available at any point in time on the device. => If the value drops, steal the entries from Monitoring.
};

//HHH MSG
struct port_data_stats{
	uint64 max_port_data;
	uint64 port_number;
};


/*Monitoring utility functions*/

void* monitoring_thread(void* data);
int validate_mon_activity_params(char* msg_string, struct mon_agent * mon_params);

int init_open_nsl();
int bcm_application_init();
int example_bst_default_profile_set (int unit);
struct table* get_row_of_state_machine(int row_num, struct mon_agent *mon_param);

struct bst_val_counters bst_stats_port_level(int unit, int port);
struct port_stats_val_t per_port_pkt_stats(int unit, int port);
int port_statistics_clear(int port);
struct port_stats_validate_t validate_port_input_event_thresholds(char* event_name, int value, int port_stats_val[]);
int mon_fp_stats_group_create(int unit);
int start_flow_stats_state_machine(struct mon_agent *mon_params,int row_num); // Invoked when MON_HELLO is received.
struct fp_flow_stats_t get_stats_attached_to_flow_enty(int unit, int stat_id);
opennsl_field_group_status_t get_field_process_group_status(int unit);
xmlChar* parseXml (xmlDocPtr doc, xmlNodePtr cur, char* findElement);
void set_device_identification_number(int id);
int  get_device_identification_number();
int check_events_flow_stats(int row_num, struct mon_agent *mon_params);
uint8 check_events_bst_port_stats(int port, uint64 port_stats_val[], struct  bst_val_counters bst_stats,int current_state,struct mon_agent *mon_param);
int configure_test_environment(); // This is used only for installing default route on the switch.
void send_stats_notification_immediate(struct mon_agent *mon_param , zsock_t* sock, int call_count); //PRO-ACTIVE-POLL Feature
int send_port_stats_notification(struct mon_agent *mon_param , zsock_t* sock, uint64 port_number); // HHH MSG port stats
void send_1D_HHH_port_notification(uint64 stat_Bytes , zsock_t* sock, uint64 port_number, int mon_id);//HHH 1D MSG port stats
void send_2D_HHH_port_notification(uint64 stat_Bytes , zsock_t* sock, uint64 port_number, uint64 source_ip, int mon_id);//HHH 2D MSG port stats

#endif /* _MONITOR_AUTOMATA_H_ */
