/**********************************************
 * monitoring_utilities.c
 *
 *  Created on: Dec 10, 2016
 *  Author: Shrikanth
 *********************************************/

#include "monitor-automata.h"

extern opennsl_field_stat_t lu_stat_ifp[2];
extern opennsl_stat_val_t stat_arr[MAX_STAT_COUNTERS];
extern bst_counter_t id_list[MAX_BST_COUNTERS];
extern int device_identification_nr;
extern struct tcam_thresholds_t tcam_thresholds;

#ifdef BST
struct bst_val_counters bst_stats_port_level(int unit, int port)
{
	opennsl_error_t rc;

	int gport;
	uint32 options;
	int index;
	int cosq;
	struct bst_val_counters bst_stats;
	memset(&bst_stats,0,sizeof(struct bst_val_counters));
#ifdef opennsl
	 dzlog_info("Fetching information for  port %d",port);

	//Check if the port is a Generic port configured on the switch.Sync all the counters from ASIC to software module 
	  of this unit (logical switch device defined by OpenNSL device_init).
	rc = opennsl_port_gport_get (unit, port, &gport);
	if (rc != OPENNSL_E_NONE)
	{
		return bst_stats;
	}

	//NOTE: From Vendor API documentation: API to sync the Hardware stats for all or given BST resources to the Software copy. 
	  During this sync, BST status will be disabled, in order to maintain consistency of the stats to a defined time and Post sync, 
	  the BST status will be restored.
    //This is required to be used before calling bst_stat_get() to get latest or updated stats value.
	/*for (index = 0; index < MAX_COUNTERS; index++) // This is required. But, the polling should be done independently for these counters.
	{
		rc = opennsl_cosq_bst_stat_sync(unit, id_list[index].bid); //defined id_list in monitor-automata.h
		if (rc != OPENNSL_E_NONE) {
			 dzlog_error("\r\nFailed to sync the state of port, rc = %d (%s).\r\n",
		   rc, opennsl_errmsg(rc));
			break;
		}
	}*/

	//Get the values for each counter BID and its COSQ now.
	for (index = 0; index < MAX_COUNTERS; index++)
	{
	  for (cosq = 0; cosq < MAX_COSQ_COUNT; cosq++)
	  {
		rc = opennsl_cosq_bst_stat_get(unit, gport, cosq,
		id_list[index].bid, options, &(bst_stats.val[index][cosq])); //need to define VAL array.
		if (rc != OPENNSL_E_NONE) {
		    dzlog_error("\r\nFailed to get the bst stats, rc = %d (%s).\r\n",
		    		rc, opennsl_errmsg(rc));
		  break;
		}
		dzlog_debug("BST Counter: %s for COS queue: %d is : %llu\n",
				id_list[index].name, cosq, bst_stats.val[index][cosq]);
	  }
	  dzlog_info("\n");
	}
#endif
	return bst_stats;
}

int get_bst_val_counter_index(char* name)
{
	int index = 0xFF;
	//get its corresponding index in the id_list Global array.
	for(index=0;index<MAX_COUNTERS;index++)
	{
		if(streq(name, id_list[index].name))
		{
			//use this index to fetch the value from the bst_stats.val array.
			return index;
		}
	}
	return index;
}

int validate_bst_input_thresholds(int threshold_value, int index, struct  bst_val_counters bst_stats)
{
		int ret = false;
		for(int cosq=0;cosq<MAX_COSQ_COUNT;cosq++ )
		{
			if(bst_stats.val[index][cosq] < threshold_value)
			{
				// Values are well within the threshold, need not take any actions. // just return false. // also return the class of service queue for which the threshold exceeded
				return true;
			}
		}
		return ret;
}
#endif



/*****************************************************************************************
 *This funcion is used to get the FP group status information.
 *SDN controller uses this information to place the monitoring agents.
 *****************************************************************************************/
int send_fp_group_status_info(zsock_t *sock,int unit)
{
	opennsl_field_group_status_t  status = get_field_process_group_status(unit);
	struct ds fp_grp_status_info;
	ds_init(&fp_grp_status_info);

	ds_put_format(&fp_grp_status_info,"<get_field_processor_group_status><tot_free_counters>%llu</tot_free_counters><tot_sys_counters>%llu</tot_sys_counters><used_grp_entries>%llu</used_grp_entries><tot_free_entries>%llu</tot_free_entries><tot_sys_entries>%llu</tot_sys_entries></get_field_processor_group_status>",
				 status.counters_free,status.counters_total,status.entry_count,status.entries_free,status.entries_total);
	char *notif_content = ds_steal_cstr(&fp_grp_status_info);
	zstr_send(sock,notif_content);
    free(notif_content);
    return 0;
}


/******************************************************************************************
 * Below function is used to send RPC reply for PORT statistics request
 * ****************************************************************************************/
int send_port_statistics_reply(zsock_t *sock, struct port_stats_val_t port_stats)
{

	struct ds port_stats_in_data, port_stats_out_data, port_stats_discarded_data; //ds defined in Open-Vswitch Library.
	int device_id = get_device_identification_number();
	//xmlDocPtr d;

    ds_init(&port_stats_in_data);
    ds_init(&port_stats_out_data);
    ds_init(&port_stats_discarded_data);

    ds_put_format(&port_stats_in_data, "<port_statistics><IfInUcastPkts>%llu</IfInUcastPkts><IfInNUcastPkts>%llu</IfInNUcastPkts>",port_stats.val[0],port_stats.val[1]);
    ds_put_format(&port_stats_out_data,"<IfOutUcastPkts>%llu</IfOutUcastPkts><IfOutNUcastPkts>%llu</IfOutNUcastPkts>",port_stats.val[2],port_stats.val[3]);
    ds_put_format(&port_stats_discarded_data,"<IfInDiscards>%llu</IfInDiscards><IfOutDiscards>%llu</IfOutDiscards></port_statistics>",port_stats.val[8],port_stats.val[9]);
    //
    char *notif_content_1 = ds_steal_cstr(&port_stats_in_data); // Convert the notification content into a normal C-style string.
    char *notif_content_2 =  ds_steal_cstr(&port_stats_out_data);
    char *notif_content_3 =  ds_steal_cstr(&port_stats_discarded_data);
    zstr_sendm(sock,notif_content_1); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
    zstr_sendm(sock,notif_content_2);
    zstr_send(sock,notif_content_3);

    free(notif_content_1);
    free(notif_content_2);
    free(notif_content_3);
	return 0;
}


struct port_stats_val_t per_port_pkt_stats(int unit, int port)
{
	opennsl_error_t rc;
	int port_stats_val[MAX_STAT_COUNTERS];
	struct port_stats_val_t new_array;
	memset(&new_array,0,sizeof(struct port_stats_val_t));
#ifdef opennsl
	int nstat = (sizeof(stat_arr) / sizeof(opennsl_stat_val_t));

    rc = opennsl_stat_multi_get(unit, port, nstat, stat_arr, port_stats_val);
    if(rc != OPENNSL_E_NONE) {
      dzlog_info("\r\nFailed to get the port stats, rc = %d (%s).\r\n",
             rc, opennsl_errmsg(rc));
      return new_array;
    }
	else
	{
		memcpy(&new_array.val,port_stats_val,(sizeof(int)*MAX_STAT_COUNTERS));
	}

	#ifdef DEBUG
		for(int index = 0;index < nstat;index++)
		{
		   dzlog_info("\n Port stats collected for the port %d and value is %llu \n",port,port_stats_val[index]);
		}

	#endif


#endif
    return new_array;
}


int port_statistics_clear(int port)
{
	int unit=DEFAULT_UNIT;
	opennsl_error_t rc;
#ifdef opennsl
    rc = opennsl_stat_clear(unit, port);
    if(rc != OPENNSL_E_NONE) {
      dzlog_error("\r\nFailed to clear the port stats, rc = %d (%s).\r\n",
             rc, opennsl_errmsg(rc));
      return rc;
    }
#endif

    return 0;
}

#ifdef opennsl
int mon_fp_stats_group_create(int unit)
{   
    opennsl_error_t rc = OPENNSL_E_NONE;
    
#ifdef PRESEL
    int maxPorts = 512; 
    opennsl_pbmp_t InPbmpMask, InPbmp;
    int  presel_id;  
    opennsl_field_presel_set_t  psset; 

    opennsl_port_config_t  port_cfg;
    int num_ports = 0, num_front_panel_ports = 0;

    rc = opennsl_port_config_get (unit, &port_cfg);
    if (OPENNSL_E_NONE != rc) {
        
        dzlog_info("Failed to create preselector for the FP group Unit=%d, rc=%s",
                    unit, opennsl_errmsg(rc));
        return rc;
    }
    
    OPENNSL_PBMP_COUNT(port_cfg.ge, num_ports);
    num_front_panel_ports = num_ports;

    OPENNSL_PBMP_COUNT(port_cfg.xe, num_ports);
    num_front_panel_ports += num_ports;    
    
    maxPorts = num_front_panel_ports;   
    dzlog_info("Maximum ports detected =%d \n",maxPorts);	
    OPENNSL_PBMP_CLEAR(InPbmpMask);	
    OPENNSL_PBMP_CLEAR(InPbmp);			
    



    for(int port=0; port<maxPorts; port++)
    {
     OPENNSL_PBMP_PORT_ADD(InPbmpMask, port);
     OPENNSL_PBMP_PORT_ADD(InPbmp, port);
    }
#endif    

    OPENNSL_FIELD_QSET_INIT(fp_mon_stats_qset);
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifyStageIngress);

#ifdef QUALIFY_ALL
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifySrcMac);
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifyDstMac);
#endif

    // Qualify the TCAM Table only using source and destination IP.
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifySrcIp);
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifyDstIp);


#ifdef QUALIFY_ALL 
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifyL4SrcPort);
    OPENNSL_FIELD_QSET_ADD (fp_mon_stats_qset, opennslFieldQualifyL4DstPort); // This is to figure out the port on which the packets for a particular flow arrive.
#endif
    /* Initialize FP Groups if this is the first time */

    //create a pre-selector -->
#ifdef PRESEL
    rc = opennsl_field_presel_create(DEFAULT_UNIT,&presel_id);
    if( rc != OPENNSL_E_NONE)
    {
        dzlog_info("Failed to create preselector for the FP group Unit=%d, rc=%s",
                    unit, opennsl_errmsg(rc));		
    }		
    rc = opennsl_field_qualify_Stage(DEFAULT_UNIT,presel_id | OPENNSL_FIELD_QUALIFY_PRESEL,opennslFieldStageIngress);
    if( rc != OPENNSL_E_NONE )
    {
      dzlog_info("Failed to qualify stage of  pre-selector for the FP group Unit=%d, rc=%s",
                    unit, opennsl_errmsg(rc));
      return rc;	
    }
    
    rc = opennsl_field_qualify_InPorts(DEFAULT_UNIT,presel_id | OPENNSL_FIELD_QUALIFY_PRESEL, InPbmp,InPbmpMask);	
    if( rc!= OPENNSL_E_NONE )
    {
     dzlog_info("Failed to qualify InPorts Unit=%d rc=%s",unit, opennsl_errmsg(rc));
     return rc;
    }
    
    OPENNSL_FIELD_PRESEL_INIT(psset);
    OPENNSL_FIELD_PRESEL_ADD(psset, presel_id);
    // --> Preselector related code here.
#endif 

//It was observed that action set adding fails on AS7712 with reason Feature unavailable.
#ifndef AS7712  
    OPENNSL_FIELD_ASET_INIT(aset);
    OPENNSL_FIELD_ASET_ADD(aset, opennslFieldActionStat);
#endif
    if(fp_mon_stats_grps == -1)
    {
        rc = opennsl_field_group_create(unit, fp_mon_stats_qset,
                                        0,
                                        &fp_mon_stats_grps);
        if (rc != OPENNSL_E_NONE) {
           dzlog_info("Failed to create FP group Unit=%d, rc=%s",
                    unit, opennsl_errmsg(rc));
        	//printf("%d: opennsl_field_group_create failed %s %d \n",__FUNCTION__,__FILE__,__LINE__);
            return rc;
        }
        dzlog_info("%s, Created FP Group = %d for unit = %d",
                 __FUNCTION__,fp_mon_stats_grps, unit);
    }
#ifdef PRESEL    
    //set the preselector to the group.
    rc = opennsl_field_group_presel_set(DEFAULT_UNIT,fp_mon_stats_grps,&psset);
    if( rc!= OPENNSL_E_NONE)
    {
        dzlog_info("Failed to set preselector to  FP group Unit=%d, rc=%s",
                 unit, opennsl_errmsg(rc));
	return rc;
    }
#endif
#ifndef AS7712
    rc = opennsl_field_group_action_set(unit, fp_mon_stats_grps, aset);
    if( rc != OPENNSL_E_NONE)
    {
        dzlog_info("Failed to add action set to FP group Unit=%d, rc=%s",
                 unit, opennsl_errmsg(rc));
     	//printf("%d: opennsl_field_group_create failed %s %d \n",__FUNCTION__,__FILE__,__LINE__);
         return rc;
    }
#endif
    return true;
}

int mon_fp_stats_feature_entry_create(int unit, struct fp_mon_qset_attributes_t qset_attr)
{
    int entry = -1;
    opennsl_error_t rc = OPENNSL_E_NONE;
    //int hw_unit = DEFAULT_UNIT; //parameterize this while integrating with OpenSwitch.

    rc = opennsl_field_entry_create(unit, fp_mon_stats_grps, &entry);
    if ( rc!= OPENNSL_E_NONE)
    {
	dzlog_info("Failed opennsl_field_entry_create \
                unit=%d entry=%d rc=%s",
                unit, entry, opennsl_errmsg(rc));
        return rc;
    }	
    rc = opennsl_field_qualify_SrcIp(unit, entry, qset_attr.src_ip, qset_attr.src_ip_mask);
    if (rc != OPENNSL_E_NONE) {
        dzlog_info("Failed opennsl_field_qualify_SrcIp \
                unit=%d entry=%d rc=%s",
                unit, entry, opennsl_errmsg(rc));
        return rc;
    }
    rc = opennsl_field_qualify_DstIp(unit, entry, qset_attr.dst_ip, qset_attr.dst_ip_mask);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_info("Failed opennsl_field_qualify_DstIp \
                unit=%d entry=%d rc=%s",
                unit, entry, opennsl_errmsg(rc));
        return rc;
    }
#ifdef QUALIFY_ALL
    rc = opennsl_field_qualify_SrcMac(unit,entry,qset_attr.src_mac, qset_attr.src_mac_mask);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_info("Failed opennsl_field_qualify_SrcMac \
                     unit=%d entry=%d rc=%s",
                     unit, entry, opennsl_errmsg(rc));
        return rc;
    }
    rc = opennsl_field_qualify_DstMac(unit,entry,qset_attr.dst_mac, qset_attr.dst_mac_mask);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_info("Failed opennsl_field_qualify_DstMac \
                     unit=%d entry=%d rc=%s",
                     unit, entry, opennsl_errmsg(rc));
        return rc;
    }
    rc = opennsl_field_qualify_L4SrcPort(unit,entry,qset_attr.src_port, qset_attr.src_port_mask);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_info("Failed opennsl_field_qualify_L4SrcPort \
                    unit=%d entry=%d rc=%s",
                     unit, entry, opennsl_errmsg(rc));
        return rc;
    }
    rc = opennsl_field_qualify_L4DstPort(unit,entry,qset_attr.dst_port, qset_attr.dst_port_mask);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_info("Failed opennsl_field_qualify_L4DstPort \
                    unit=%d entry=%d rc=%s",
                    unit, entry, opennsl_errmsg(rc));
        return rc;
    }
#endif
    dzlog_info("Flow entry created %d \n",entry);
    return entry;
}
//send unit as DEFAULT unit, OpenSwitch also supports only one unit at the moment.
int mon_fp_feature_stat_attach_entry_install(int entry,int unit,int nstats, opennsl_field_stat_t *lu_stat_ifp) 
{
	opennsl_error_t rc = OPENNSL_E_NONE;
	int stat_id = -1;

    rc = opennsl_field_stat_create(unit,
    				   fp_mon_stats_grps,
                                   2, lu_stat_ifp, &stat_id); //use the value supplied by "nstats" in place of number 2.
    if (rc != OPENNSL_E_NONE) {
        dzlog_error("Failed to create stat-id for FP ingress statistics entry \
    	        Unit=%d  rc=%s",unit,opennsl_errmsg(rc));
        return -1;
    }

    rc = opennsl_field_entry_stat_attach(unit, entry, stat_id);
    if (rc != OPENNSL_E_NONE) {
    	dzlog_error("Failed to attach stat to FP Ingress statistics entry \
                Unit=%d rc=%s",unit,opennsl_errmsg(rc));
        return -1;
    }
    rc = opennsl_field_entry_install(unit, entry);   
    if (rc != OPENNSL_E_NONE) {
    	dzlog_error("Failed to install stat to FP Ingress statistics entry \
                Unit=%d  rc=%s",unit,opennsl_errmsg(rc));
        return -1;
    }

    /*for(int i=0;i<2;i++)  // Initialize the counter value to 0.
    {
        rc = opennsl_field_stat_set(unit,stat_id,lu_stat_ifp[i],0);
        if (OPENNSL_FAILURE(rc))
        {
        	dzlog_error("Failed to attach set stat value \
                      Unit=%d stat_id=%d rc=%s",
                      unit, stat_id, opennsl_errmsg(rc));
              return -1;
         }
    }*/

    dzlog_info("Statistics entry installed and statistics attached, Statistics Object ID is: %d \n",stat_id);
    return stat_id;
}



int mon_fp_stats_feature_entry_get_stat_id(int unit, int entry)
{
	int stat_id = -1;

	int rc =  opennsl_field_entry_stat_get(unit,entry,&stat_id);

    if (OPENNSL_FAILURE(rc)) {
    	dzlog_error("Failed opennsl_field_entry_stat_get \
                  unit=%d entry=%d rc=%s",
                  unit,opennsl_errmsg(rc));
        return rc;
    }
	return stat_id;
}

/***********************************************************************************************
*This function should be called to destroy the flow entry when the monitoring agent is stopped
*by the controller.
*This function will destroy the statistics object attached to the flow entry as well.
************************************************************************************************/
#endif


int mon_fp_feature_flow_entry_set_priority(opennsl_field_entry_t entry,int unit,int row_num,struct mon_agent *mon_params)
{
	struct table* table_row = get_row_of_state_machine(row_num,mon_params);
#ifdef opennsl
	int rc = opennsl_field_entry_prio_set (unit,entry,table_row->flow.priority);
	if( rc != OPENNSL_E_NONE )
	{
		dzlog_error("\n Set priority operation failed with reason=%d %s for entry = %d prio = %d\n",rc,opennsl_errmsg(rc),entry,table_row->flow.priority);

	}
#endif
	return 0;
}


int mon_fp_feature_flow_entry_get_priority (int unit, opennsl_field_entry_t entry)
{
    int prio;
#ifdef opennsl
	int rc = opennsl_field_entry_prio_get(unit,entry,&prio);
	if( rc != OPENNSL_E_NONE )
	{
		dzlog_error("\n Get priority operation failed with reason=%d %s for entry = %d\n",rc,opennsl_errmsg(rc),entry);

	}
#endif
	return prio;
}



int mon_fp_stats_feature_entry_destroy(int entry,int unit,int nstats, int stat_id)
{
    opennsl_error_t rc = OPENNSL_E_NONE;
    int hw_unit = unit;

#ifdef opennsl
    //Detach the field entry statistics here.
    rc = opennsl_field_entry_stat_detach(hw_unit, entry, stat_id); 
   if (OPENNSL_FAILURE(rc)) {
    	dzlog_error("Error while detaching FP stat id %d for \
                Unit=%d rc=%s",
                stat_id, hw_unit,opennsl_errmsg(rc));
        return rc;
    }

    //Destroy the field statistics here.
    rc = opennsl_field_stat_destroy(hw_unit, stat_id);
   if (OPENNSL_FAILURE(rc)) {
    	dzlog_info("Error while destroying FP stats \
                Unit=%d rc=%s",
                hw_unit,opennsl_errmsg(rc));
        return rc;
    }

    //Destroy field entry itself now.
    rc = opennsl_field_entry_destroy(hw_unit, entry);
    if (OPENNSL_FAILURE(rc)) {
    	dzlog_info("Error while destroying FP stats \
                Unit=%d entry=%d rc=%s",
                hw_unit,entry, opennsl_errmsg(rc));
        return rc;
    }
#endif

    return OPENNSL_E_NONE;
}



opennsl_field_group_status_t  get_field_process_group_status(int unit)
{
	opennsl_field_group_status_t status;
	memset(&status,0,sizeof(opennsl_field_group_status_t));
 #ifdef opennsl
	opennsl_error_t rc = OPENNSL_E_NONE;
	rc = opennsl_field_group_status_get(unit,fp_mon_stats_grps,&status);  // Get the status of the field group that we created for Monitoring purposes.
    if (OPENNSL_FAILURE(rc)) {
        dzlog_error("Failed to get the group status information \
                Unit=%d fp_mon_stats_grps=%d rc=%s",
                unit, fp_mon_stats_grps, opennsl_errmsg(rc));
    }
 #endif

#ifdef DEBUG
    dzlog_info("counter_count:%d\n",status.counter_count);
    dzlog_info("counters_free:%d\n",status.counters_free);
    dzlog_info("counters_total:%d\n",status.counters_total);
    dzlog_info("entries_free:%d\n",status.entries_free);
    dzlog_info("entries_total:%d\n",status.entries_total);
    dzlog_info("entry_count:%d\n",status.entry_count);
    dzlog_info("meter_count:%d\n",status.meter_count);
    dzlog_info("meters_free:%d\n",status.meters_free);
    dzlog_info("meters_total:%d\n",status.meters_total);
    dzlog_info("prio_max:%d\n",status.prio_max);
    dzlog_info("prio_min:%d\n",status.prio_min);
#endif
    return status;
}




/******************************************************
 * Used to assign an unique identifier to the switch
 *
 *****************************************************/
void set_device_identification_number(int id)
{
	device_identification_nr = id;
	return;
}

/******************************************************
 * Used to assign an unique identifier to the switch
 *
 *****************************************************/
int  get_device_identification_number()
{
	return device_identification_nr;
}


/******************************************************
 * Send MON_SWITCH notification to the SDN Controller
 * when device is low on TCAM memory.
 ******************************************************/
void send_mon_switch_notification(zsock_t *sock, opennsl_field_group_status_t  status)
{
	struct ds mon_switch_data; //DS defined in Open-Vswitch's OVSLIB Library.
	int device_id = get_device_identification_number();

    ds_init(&mon_switch_data);
    ds_put_format(&mon_switch_data, "<MON_SWITCH xmlns=\"http://monitoring-automata.net/sdn-mon-automata\"><device-Id>%d</device-Id><TCAM-Count>%d</TCAM-Count><min-Free-Entry-Count>%d</min-Free-Entry-Count><TCAM-threshold-set>%d</TCAM-threshold-set></MON_SWITCH>"
    								,device_id,status.entry_count,tcam_thresholds.min_free_entries_per_device,tcam_thresholds.max_allowed_tcam_entries);

    //status.entry_count = number of entries present in TCAM.

    char *notif_content = ds_steal_cstr(&mon_switch_data); // Convert the notification content into a normal C-style string.
    zstr_sendm(sock,"mon_event_notification");
    zstr_send(sock,notif_content); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
    free(notif_content);  // It might just lead to a crash here. Remember.
	return;
}


/*SDN Monitoring specific custom functions*/
/******************************************************************************************************
 * This function is used to fetch an XML Element's content from the XML tree.
 * IN doc - Pass the XML tree as a doc pointer
 * IN cur - Node pointer pointing to the root of the tree.
 * IN findElement - This is to find an element whose content is returned from with in the tree.
 * OUT content of the specified XML element.
 * NOTE: This will fetch only the elements that are direct children to the root element of the XML tree.
 * *****************************************************************************************************/
xmlChar*
parseXml (xmlDocPtr doc, xmlNodePtr cur, char* findElement) {

	xmlChar *key_content;
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
	    if ((!xmlStrcmp(cur->name, (const xmlChar *)findElement))) {
	    	key_content = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
		    dzlog_info("\n Content of the keyword %s found is : %s\n",findElement,key_content);
		    return(key_content);
 	    }
	    cur = cur->next;
	}
    return NULL;
}

int send_notification(zsock_t *sock, char *notification_content)
{
    char *record = NULL;
    //For testing only, remove it later.
    if (notification_content == NULL) {
		return -1;
	}
	int len = (int32_t) asprintf(&record,"%s",notification_content);
	zstr_send(sock,record);
 	return 0;
}

int get_port_from_mon_params(struct mon_agent *mon_param)
{
	int port = mon_param -> port_index;
	return port;
}

int get_polltime_from_mon_params(struct mon_agent *mon_param)
{
	int poll_time = mon_param -> poll_time;
	return poll_time;
}

int get_number_of_states(struct mon_agent *mon_param)
{
	int num_of_states =	mon_param->total_states;
	return num_of_states;
}

/*Row indexing starts from 0th index*/
struct table* get_row_of_state_machine(int row_num, struct mon_agent *mon_param)
{
  #ifdef DEBUG
	dzlog_info(" get_row_of_state_machine state machine table");
  #endif
	struct table *table_row = mon_param->state_table_rows;
	if(row_num < mon_param->total_states)
	{
		table_row = table_row + row_num;		
	}
#ifdef DEBUG
	dzlog_info("Returning from state machine table");
#endif
	return table_row;
}

int get_num_of_row_events(int row, struct mon_agent *mon_param)
{
	int num_of_row_events;
	struct table *table_row = get_row_of_state_machine(row,mon_param);
	return(table_row->num_of_row_evnts); 
}

input_params_t* get_events(struct table *table_row)
{
	return(table_row->event);	
}


int resize_actions(struct table *table_row)
{
	table_row->action = (int*) realloc(table_row->action, (table_row->num_of_actions + 1) * sizeof(int *));
	return 0;
}

int* get_actions(struct table *table_row)
{
	return(table_row->action);
}

input_params_t* get_num_of_actions(struct table *table_row)
{
	return(table_row->num_of_actions);
}
#ifdef BST
int get_bst_val_counter_index(char* name)
{
	int index = 0xFF;
	//get its corresponding index in the id_list Global array.
	for(index=0;index<MAX_COUNTERS;index++)
	{
		if(streq(name, id_list[index].name))
		{
			//use this index to fetch the value from the bst_stats.val array.
			return index;
		}
	}	
	return index;
}

int validate_bst_input_thresholds(int threshold_value, int index, struct  bst_val_counters bst_stats)
{
		int ret = false;
		for(int cosq=0;cosq<MAX_COSQ_COUNT;cosq++ )
		{
			if(bst_stats.val[index][cosq] < threshold_value)
			{
				// Values are well within the threshold, need not take any actions. // just return false. // also return the class of service queue for which the threshold exceeded
				return true;
			}
		}
		return ret;
}
#endif
struct port_stats_validate_t validate_port_input_event_thresholds(char* event_name, int threshold_value, int port_stats_val[])
{

		struct port_stats_validate_t ret_obj;
		ret_obj.ret_value = false;
		ret_obj.index = 0xFF;
		int index=0xFF;
		if(streq(event_name,"opennsl_spl_snmpIfInUcastPkts"))
		{
			index=0;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfInNUcastPkts"))
		{
			index=1;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfOutUcastPkts"))
		{
			index=2;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfOutNUcastPkts"))
		{
			index=3;
		}
		else if( streq(event_name,"opennsl_spl_snmpIfInOctets"))
		{
			index=4;
		}
		else if( streq(event_name,"opennsl_spl_snmpIfOutOctets"))
		{
			index=5;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfInErrors"))
		{
			index=6;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfOutErrors"))
		{
			index=7;
		}
		else if(streq(event_name,"opennsl_spl_snmpIfInDiscards"))
		{
			index=8;
		}
		else if( streq(event_name,"opennsl_spl_snmpIfOutDiscards"))
		{
			index=9;
		}
		if( port_stats_val[index] < threshold_value )
		{
			ret_obj.ret_value = true;
			ret_obj.index = index;
			return ret_obj;
		}
		else
		{
			ret_obj.index = index;
		}
		return ret_obj;
}


/**************************************************************************************
 * This function shall start the state-machine by installing appropriate flow entry
 * and attaching statistics meter that could be fetched periodically to monitor the flow.
 * Initializes the current statistics to zero.
 ***************************************************************************************/
int start_flow_stats_state_machine(struct mon_agent *mon_params,int row_num)
{
	int num_of_states = get_number_of_states(mon_params);
	if( num_of_states != 0 )
	{
		int row = 0;
		struct fp_mon_qset_attributes_t flow;
		int entry = -1;
		int stat_id = -1;
		struct table* table_row = get_row_of_state_machine(row_num,mon_params);
		//Check and set if the flow priority exist. This is required to SET the priority to the flow-rule entry.
		if(table_row->flow.priority)
		{
			mon_params->flow_priority = true;

		}

		dzlog_info("Installed flow is %d\n",table_row->flow.src_ip); // Remove this later.
		if(table_row->flow_exists)
		{
#ifdef opennsl
			flow = table_row -> flow;
			entry = mon_fp_stats_feature_entry_create(DEFAULT_UNIT,flow);
			if(entry != -1)
			{
			  mon_params -> flow_entry_id = entry; // This will be fetched later to un-install the flow.
			  stat_id = mon_fp_feature_stat_attach_entry_install(entry,DEFAULT_UNIT,2,lu_stat_ifp); //2 -> indicates number of statistics entries.
			  if(stat_id != -1)
			  {
				 dzlog_info("\n stat-id created %d \n",stat_id);
				 mon_params->stat_id = stat_id;
				 mon_params->mon_state_info.flow_stats_val.statPackets = 0;  //Initialize the current value of the statistics to zero.
				 mon_params->mon_state_info.flow_stats_val.statBytes = 0;
			  }
			}
			else
			{
				return -1;
				dzlog_error("Could not initiate the flow statistics collection for this flow.");
			}
#endif
		}
	}
	dzlog_info("flow statistics state_machine running now \n");
	return OPENNSL_E_NONE;
}

/**************************************************************************
 * This function is used to peg the counters attached to a flow entry.
 * Number of Packets received
 * Number of Bytes received counters are fetched from Open-NSL ASIC.
 **************************************************************************/

struct fp_flow_stats_t get_stats_attached_to_flow_enty(int unit, int stat_id)
{

	struct fp_flow_stats_t val;
	memset(&val,0,sizeof(struct fp_flow_stats_t));
	uint64 value[NUM_FP_FLOW_STATS] = {0};
#ifdef opennsl
	for(int i=0;i<NUM_FP_FLOW_STATS;i++)
	{
		int  rc = opennsl_field_stat_get(unit, stat_id, lu_stat_ifp[i], &value[i]);
		if (OPENNSL_FAILURE(rc))
		{
			dzlog_error("Failed opennsl_field_stat_get \ unit=%d stat_id=%d rc=%s",
					unit, stat_id, opennsl_errmsg(rc));
			return val;
		}
	}
	val.statPackets = value[0];
	val.statBytes = value[1];
#endif
#ifdef DEBUG
	dzlog_info("\nValues returned from the flow counters for stat_id %d %d & %d\n",stat_id, val.statPackets,val.statBytes);
#endif
	return val;
}

/****************************************************************************************************
 * Calculate the current interval counter. This is used to check if the values crossed any threshold.
 * the event will be reported according to the actions specified by the controller.
 ****************************************************************************************************/
void calculate_the_current_interval_values(struct mon_agent *mon_params)
{
	int index;
	int cosq;
 #ifdef BST
	if(mon_params -> mon_type == BST || mon_params -> mon_type == BST_FP || mon_params -> mon_type == BST_STATS || mon_params -> mon_type == BST_FP_STATS )
	{
		for( index = 0; index < MAX_COUNTERS; index ++)
		{
			for( cosq = 0; cosq < MAX_COSQ_COUNT; cosq++ )
			{
				//Current Interval Counter values = (Counter values captured now - Counter values captured at the beginning of the polling interval)
				mon_params->mon_state_info.bst_stats_interval.val[index][cosq] = ((mon_params->mon_state_info.bst_stats_delta.val[index][cosq])
																				 -(mon_params->mon_state_info.bst_stats.val[index][cosq]));
			}
		}
	}
 #endif

	if(mon_params -> mon_type == STATS || mon_params -> mon_type == FP_STATS || mon_params -> mon_type == BST_STATS || mon_params -> mon_type == BST_FP_STATS)
	{
		for(index = 0; index < MAX_STAT_COUNTERS ; index ++)
		{
			//Current Interval Counter values = (Counter values capture now - Counter values captured at the beginning of the polling interval)
			mon_params->mon_state_info.port_stats_val_interval[index] = ((mon_params->mon_state_info.port_stats_val_delta[index])
																			 -(mon_params->mon_state_info.port_stats_val[index]));
		}
	}

	return;
}

/*********************************************************************************************************************
 * This function is used to check the counter values defined against the number of Packets and number of Bytes       *
 * threshold from the controller.                                                                                    *
 *********************************************************************************************************************/

void calculate_current_interval_flow_stats(struct mon_agent *mon_params)
{
	//Current Interval Counter values = (Counter values captured now) - (Counter values captured at the beginning of the polling interval)
	mon_params->mon_state_info.flow_stats_val_interval.statPackets = ((mon_params->mon_state_info.flow_stats_val_delta.statPackets)
																			 -(mon_params->mon_state_info.flow_stats_val.statPackets));


	//mon_params->mon_state_info.flow_stats_val_interval.statPackets = 501; //remove this later. This was only to locally test the notification sending if the threshold exceeded!!!
	mon_params->mon_state_info.flow_stats_val_interval.statBytes = ((mon_params->mon_state_info.flow_stats_val_delta.statBytes)
																		 -(mon_params->mon_state_info.flow_stats_val.statBytes));
#ifdef DEBUG
	dzlog_info("\n exiting from function calculate_current_interval_flow_stats %llu %llu: ",mon_params->mon_state_info.flow_stats_val_interval.statPackets,mon_params->mon_state_info.flow_stats_val_interval.statBytes);
#endif
	return;
}

/*********************************************************************
 * Checks if an event related to FLOW statistics occured.
 *
 *
 *********************************************************************/

int check_events_flow_stats(int row_num, struct mon_agent *mon_params)
{

	struct table *table_row = get_row_of_state_machine(row_num,mon_params);
	uint64 bytes_threshold = table_row -> statBytesThreshold;
	uint64 packets_threshold = table_row -> statPktsThreshold;

	uint8 packet_threshold_exceeded = false;
	uint8 bytes_threshold_exceeded = false;
    uint8 bit_to_set;

	bit_to_set = 0;
	//dzlog_info("Number of packets and threshold %llu %llu\n",mon_params->mon_state_info.flow_stats_val_interval.statPackets, packets_threshold);
	if (mon_params->mon_state_info.flow_stats_val_interval.statPackets > packets_threshold && packets_threshold !=0)
	{
		packet_threshold_exceeded = true;
		table_row ->flow_events_bitmap |= 1 << bit_to_set; //Set the bit indicating that the event occurred during this interval
		dzlog_info("Flow event detected for packets \n");
	}
	else
	{
		table_row ->flow_events_bitmap &= ~(1 << bit_to_set); //Reset the bit to '0' indicating that the event did not occur during this interval.
	}

	bit_to_set = 1;
	if (mon_params->mon_state_info.flow_stats_val_interval.statBytes > bytes_threshold && bytes_threshold!=0)
	{
		//dzlog_info("Flow event detected for bytes \n");
		bytes_threshold_exceeded = true;
		table_row ->flow_events_bitmap |= 1 << bit_to_set; //Set the bit indicating that statBytes events occurred.
	}
	else
	{
		table_row ->flow_events_bitmap &= ~(1 << bit_to_set); //Reset the bit to '0' indicating that statBytes events did not occur during this interval.
	}

	if(packet_threshold_exceeded || bytes_threshold_exceeded )
	{
		return true;
	}

	return false;
}


/**********************************************************************************
 *Checks if any PORT related events occurred and sets the appropriate bits.
 **********************************************************************************/

uint8 check_events_bst_port_stats(int port, uint64 port_stats_val[], struct  bst_val_counters bst_stats,int current_state,struct mon_agent *mon_param)
{
   int num_states  =  get_number_of_states(mon_param); 	
   uint8 notification = false;


   if(current_state < num_states)
   {

		int row_num = (current_state-1); //row-id starts from 0
		struct table* table_row = get_row_of_state_machine(row_num,mon_param);
		int number_of_events =   table_row -> num_of_row_evnts;
		int j;
		int bst_index;
		struct port_stats_validate_t ip_event_validate;
		struct notification_t notify[MAX_STAT_EVENTS];
		int number_of_actions;
		int *actions;
		int bst_event_validate;

		//Events should correlate to one of the statistics that we have gathered from the HW.
		input_params_t* event = get_events(table_row);
		char *notification_content = NULL;
		if(table_row->port_threshold_exists == true) //NOTE: This if condition check is added at the time of last code checkin. If things do not work, revert it back.
		{
		 for(int i=0;i< number_of_events;i++)
		 {
			#ifdef BST
			 if( mon_param -> mon_type == BST_STATS )  // Do this only for BST statistics. Right now BST is not supported.
			 {
				if(streq(event -> name,"opennslBstStatIdUcast") || streq(event -> name,"opennslBstStatIdMcast") || streq(event -> name,"opennslBstStatIdPriGroupShared")
						|| streq(event -> name,"opennslBstStatIdPriGroupHeadroom"))
				{
					bst_index = get_bst_val_counter_index(event->name); //Get the corresponding index from id_list which was used to fetch the counters from HW in to bst_stats data structure.
					if(bst_index != 0xFF)
					{
						bst_event_validate = validate_bst_input_thresholds(event->value,bst_index,bst_stats);//Now that we have the index, validate the input.
						if(!bst_event_validate)
						{
							memset(notify[event_index].name,'\0',EVENTS_NAME_SIZE);
							strcpy(notify[event_index].name,event->name);
							notify[event_index].value = bst_stats.val[bst_index][/*cosq*/0]; //TO DO: get the exact COSQ of the port on which the statistics exceeded.
							//TO DO: include the BST id and corresponding COSQ information for which the threshold exceeded.
							event_index++;
						}
						// TO DO: Check the validation result and see if the controller has to be notified.
						event++;
					}
				}
		 	 }
			#endif

			 if(streq(event -> name,"opennsl_spl_snmpIfInUcastPkts") || streq(event -> name,"opennsl_spl_snmpIfInNUcastPkts") || streq(event -> name,"opennsl_spl_snmpIfOutUcastPkts")
					|| streq(event -> name,"opennsl_spl_snmpIfOutNUcastPkts")|| streq(event -> name,"opennsl_spl_snmpIfInOctets")|| streq(event -> name,"opennsl_spl_snmpIfOutOctets")
					|| streq(event -> name,"opennsl_spl_snmpIfInErrors")|| streq(event -> name,"opennsl_spl_snmpIfOutErrors")|| streq(event -> name,"opennsl_spl_snmpIfInDiscards")
					|| streq(event -> name,"opennsl_spl_snmpIfOutDiscards"))
			 {
				ip_event_validate = validate_port_input_event_thresholds(event->name,event->value,port_stats_val);//Now that we have the index, validate the input.
				// TO DO: Check the validation result and see if the controller has to be notified.
				if(!ip_event_validate.ret_value)
				{
					table_row ->port_events_bitmap |= 1 << ip_event_validate.index; //Set the bit indicating that the event occurred.
				}
				else
				{
					table_row ->port_events_bitmap &= ~(1 << ip_event_validate.index);//Reset the bit to '0'.
				}
				event++;
			 }
		 }
	   }
	   if(table_row -> port_events_bitmap != 0)
	   {
		  notification = true;
	   }
	   else
	   {
		  notification = false;
	   }
   }

   return notification;
}


int send_link_util_notif(struct mon_agent *mon_param , zsock_t* sock)
{

	struct ds notif_data; //For more information on ds data-structure, refer to OVSLIB
	int device_id = get_device_identification_number();
    uint8 check_bit = 0;
    uint8 bit_to_reset;

	int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
	struct table* table_row = get_row_of_state_machine(row_num,mon_param);

    ds_init(&notif_data);

    ds_put_format(&notif_data,"<MON_LINK_UTILIZATION>");
    ds_put_format(&notif_data,"<mon-id>%d</mon-id>",mon_param->mon_id);
    ds_put_format(&notif_data,"<device-id>%d</device-id>",device_id);
    ds_put_format(&notif_data, "<IfInUcastPkts>%llu</IfInUcastPkts>",mon_param->mon_state_info.port_stats_val_interval[0]);
    ds_put_format(&notif_data, "<IfInNUcastPkts>%llu</IfInNUcastPkts>",mon_param->mon_state_info.port_stats_val_interval[1]);
    ds_put_format(&notif_data,"<stat-packets>%llu</stat-packets>",mon_param->mon_state_info.flow_stats_val_interval.statPackets);
    ds_put_format(&notif_data,"</MON_LINK_UTILIZATION>");

    char *notif_content = ds_steal_cstr(&notif_data); // Convert the notification content into a normal C-style string.

    zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
    zstr_send(sock,notif_content);

    free(notif_content);

    return 0;
}

int send_stats_notification(struct mon_agent *mon_param , zsock_t* sock)
{
	struct ds notif_data; //For more information on ds data-structure, refer to OVSLIB
	int device_id = get_device_identification_number();
	uint8 check_bit = 0;
        uint8 bit_to_reset;
	uint8 send_notif = true;

	int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
	struct table* table_row = get_row_of_state_machine(row_num,mon_param);

    ds_init(&notif_data);

    ds_put_format(&notif_data,"<MON_EVENT_NOTIFICATION>");
    ds_put_format(&notif_data,"<mon-id>%d</mon-id>",mon_param->mon_id);
    ds_put_format(&notif_data,"<device-id>%d</device-id>",device_id);
    if( table_row ->port_events_bitmap != 0 )
    {
    	ds_put_format(&notif_data,"<port-events>");
    	check_bit = (table_row ->port_events_bitmap >> 0) & 1;
    	if(check_bit)
    	{
    	   bit_to_reset = 0;
    	   ds_put_format(&notif_data, "<IfInUcastPkts>%llu</IfInUcastPkts>",mon_param->mon_state_info.port_stats_val_delta[0]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
    	}

    	check_bit = (table_row ->port_events_bitmap >> 1) & 1;
    	if(check_bit)
    	{
           bit_to_reset = 1;
     	   ds_put_format(&notif_data, "<IfInNUcastPkts>%llu</IfInNUcastPkts>",mon_param->mon_state_info.port_stats_val_delta[1]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
    	}
    	check_bit = (table_row ->port_events_bitmap >> 2) & 1;
    	if(check_bit)
    	{
           bit_to_reset = 2;
     	   ds_put_format(&notif_data, "<IfOutUcastPkts>%llu</IfOutUcastPkts>",mon_param->mon_state_info.port_stats_val_delta[2]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
    	}
    	check_bit = (table_row ->port_events_bitmap >> 3) & 1;
    	if(check_bit)
    	{
           bit_to_reset = 3;
     	   ds_put_format(&notif_data, "<IfOutNUcastPkts>%llu</IfOutNUcastPkts>",mon_param->mon_state_info.port_stats_val_delta[3]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
    	}
    	check_bit = (table_row ->port_events_bitmap >> 8) & 1;
    	if(check_bit)
    	{
           bit_to_reset = 8;
     	   ds_put_format(&notif_data, "<IfInDiscards>%llu</IfInDiscards>",mon_param->mon_state_info.port_stats_val_delta[8]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'
    	}
    	check_bit = (table_row ->port_events_bitmap >> 9) & 1;
    	if(check_bit)
    	{
           bit_to_reset = 9;
     	   ds_put_format(&notif_data, "<IfOutDiscards>%llu</IfOutDiscards>",mon_param->mon_state_info.port_stats_val_delta[9]);
    	   table_row ->port_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'
    	}
		ds_put_format(&notif_data,"</port-events>");
    }
    else
    {
	send_notif = false;
    	dzlog_debug("No Port events to notify \n");

    }

    if(table_row ->flow_events_bitmap != 0)
    {
	send_notif = true;
        ds_put_format(&notif_data,"<flow-events>");
        bit_to_reset = 0;
        if(table_row ->flow_events_bitmap == 1 || table_row ->flow_events_bitmap > 2) //check if the number of packets exceeded the threshold.
        {
        	ds_put_format(&notif_data,"<stat-packets>%llu</stat-packets>",mon_param->mon_state_info.flow_stats_val_delta.statPackets);
        	table_row ->flow_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
        }

        bit_to_reset = 1;
        if(table_row ->flow_events_bitmap >= 2)  //check if the amount of bytes transmitted exceeded the threshold.
        {
        	ds_put_format(&notif_data,"<stat-bytes>%llu</stat-bytes>",mon_param->mon_state_info.flow_stats_val_delta.statBytes);
        	table_row ->flow_events_bitmap &= ~(1 << bit_to_reset);//Reset the bit to '0'.
        }
        ds_put_format(&notif_data,"</flow-events>");
    }
    else
    {
	send_notif = false;
    	dzlog_info("No flow events to notify \n");

    }
    ds_put_format(&notif_data,"</MON_EVENT_NOTIFICATION>");
    //
    char *notif_content = ds_steal_cstr(&notif_data); // Convert the notification content into a normal C-style string.
    if(send_notif)
    {
       zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
       zstr_send(sock,notif_content);
    }

    free(notif_content);

    return 0;
}

#ifdef LINK_MONITORING
void link_change_callback(int unit, opennsl_port_t port,
                                         opennsl_port_info_t *info)
{
	int mode = FALSE;
	if(info->linkstatus) {
	    mode = TRUE;
	}

	//TO DO: Some how inform the link state change to the monitor thread.
	//there are 54 ports on Accton 5712. Use a Global Variable to set the bit mask.
	//Bit mask shall be checked by the monitoring thread to check if there is any defined action against this event.

}

int port_mode_enable_set(int unit, int port, int mode)
{
    if(opennsl_port_enable_set(unit, port, mode) != OPENNSL_E_NONE) {
      dzlog_info("Failed to %s port %2d.\n", (mode == TRUE)? "enable" : "disable",port);
    }
}


//Call this function to
int register_port_mode_enable_call_back(int unit, int mode)
{
	if (mode == LINK_MON_ON) {
	    /** register a function to be called whenever link change *
	     * happens on a port. */
	    OPENNSL_IF_ERROR_RETURN(opennsl_linkscan_register(unit,
	    		link_change_callback));
	} else {
	    /** unregister the function */
	    OPENNSL_IF_ERROR_RETURN(opennsl_linkscan_unregister(unit,
	    		link_change_callback));
	}
	return OPENNSL_E_NONE;
}

#endif


/**************************************************************************
 * PRO-ACTIVE-POLL: Send the notification immediately at the end of every polling interval.
 * Call this function immediately before ACTION is checked for any agent.
 **************************************************************************/

void send_stats_notification_immediate(struct mon_agent *mon_param , zsock_t* sock, int call_count)
{

	  int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
      struct table* table_row = get_row_of_state_machine(row_num,mon_param);

	  // Just verify once if you need to change flow_stats_val_delta --> flow_stats_val. Based on the observations during testing.
	  uint64 statPackets = mon_param -> mon_state_info.flow_stats_val_delta.statPackets; //contains the values collected at the end of the last interval.
	  uint64 statBytes = mon_param -> mon_state_info.flow_stats_val_delta.statBytes;

	  dzlog_debug("MON_STATUS being generated with values %d %d \n",statPackets,statBytes);
	  int device_id = get_device_identification_number();
	  struct ds mon_status; //struct ds defined in Open-Vswitch Library.
	  ds_init(&mon_status);
	 int packet_number = call_count;
      int64_t time_stamp = zclock_time(); // Get the current time ticks since system start up in milliseconds.
	  ds_put_format(&mon_status, "<MON_EVENT_NOTIFICATION><mon-id>%d</mon-id><device-id>%d</device-id><stat-packets>%llu</stat-packets><stat-bytes>%llu</stat-bytes><time-stamp>%llu</time-stamp><packet-number>%d</packet-number></MON_EVENT_NOTIFICATION>"
								  ,mon_param->mon_id,device_id,statPackets,statBytes,time_stamp,packet_number);

	  char *notif_content = ds_steal_cstr(&mon_status); // Convert the notification content into a normal C-style string.

      //This currently is an overhead that to send one notification to the controller, two messages have to be sent to the NETCONF server.
      //The first message is the envelope of the message indicating that a notification will be followed by the message and the second one is the actual notification
	  //to be sent to the controller.
      //If you want to avoid this overhead, read more on PUBLISHER sockets of ZEROMQ and figure out if there exist a way to achieve the same.

	  zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
      zstr_send(sock,notif_content);
	  free(notif_content);

}

/**************************************************************************
 * Sending the port stats when the threshold set gets exceeded.
 **************************************************************************/
int send_port_stats_notification(struct mon_agent *mon_param , zsock_t* sock, uint64 port_number)
{
	  int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
      struct table* table_row = get_row_of_state_machine(row_num,mon_param);

	  uint64 statBytes = mon_param -> mon_state_info.flow_stats_val_delta.statBytes; //contains the values collected at the end of the last interval.

	  dzlog_debug("HHH MSG being generated with values %d  \n",statBytes);
	  uint64 port_no = port_number;
	  
	  int device_id = get_device_identification_number();
	  struct ds notif_data; //struct ds defined in Open-Vswitch Library.
	  ds_init(&notif_data);

	  ds_put_format(&notif_data,"<MON_HHH_MSG>");
      ds_put_format(&notif_data,"<mon-id>%d</mon-id>",mon_param->mon_id);
   	  ds_put_format(&notif_data,"<device-id>%d</device-id>",device_id);
      ds_put_format(&notif_data,"<Port-Number>%llu</Port-Number>",port_no);
      ds_put_format(&notif_data,"<Port-Data>%llu</Port-Data>",statBytes);
      ds_put_format(&notif_data,"</MON_HHH_MSG>");

      char *notif_content = ds_steal_cstr(&notif_data); // Convert the notification content into a normal C-style string.

      zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
      zstr_send(sock,notif_content);

      free(notif_content);

      return 0;
}

/**************************************************************************
 * Sending the 1D HHH port stats .
 **************************************************************************/
void send_1D_HHH_port_notification(uint64 stat_Bytes, zsock_t* sock, uint64 port_number, int mon_id)
{
	  //int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
      //struct table* table_row = get_row_of_state_machine(row_num,mon_param);

	  //uint64 statBytes = mon_param -> mon_state_info.flow_stats_val_delta.statBytes; //contains the values collected at the end of the last interval.

	  dzlog_debug("HHH 1D MSG being generated with values %d \n",stat_Bytes);
	  uint64 port_no = port_number;
	  uint64 statBytes = stat_Bytes;
	  int monid = mon_id;
	  int device_id = get_device_identification_number();
	  struct ds notif_data; //struct ds defined in Open-Vswitch Library.
	  ds_init(&notif_data);

	  ds_put_format(&notif_data,"<MON_1D_HHH_MSG>");
      ds_put_format(&notif_data,"<mon-id>%d</mon-id>",monid);
   	  ds_put_format(&notif_data,"<device-id>%d</device-id>",device_id);
      ds_put_format(&notif_data,"<Port-Number>%llu</Port-Number>",port_no);
      ds_put_format(&notif_data,"<Port-Data>%llu</Port-Data>",statBytes);
      ds_put_format(&notif_data,"</MON_1D_HHH_MSG>");

      char *notif_content = ds_steal_cstr(&notif_data); // Convert the notification content into a normal C-style string.

      zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
      zstr_send(sock,notif_content);

      free(notif_content);
}

/**************************************************************************
 * Sending the 2D HHH port stats .
 **************************************************************************/
void send_2D_HHH_port_notification(uint64 stat_Bytes , zsock_t* sock, uint64 port_number, uint64 source_ip,int mon_id)
{
	  //int row_num = (mon_param -> current_state-1); // This will be the 0th row unless explicitly set to certain row of the state-machine due to transition.
      //struct table* table_row = get_row_of_state_machine(row_num,mon_param);

	  //uint64 statBytes = mon_param -> mon_state_info.flow_stats_val_delta.statBytes; //contains the values collected at the end of the last interval.

	  dzlog_debug("HHH 1D MSG being generated with values %d \n",stat_Bytes);
	  uint64 port_no = port_number;
	  uint64 src_ip = source_ip;
	  uint64 statBytes = stat_Bytes;
	  int monid = mon_id;
	  int device_id = get_device_identification_number();
	  struct ds notif_data; //struct ds defined in Open-Vswitch Library.
	  ds_init(&notif_data);

	  ds_put_format(&notif_data,"<MON_2D_HHH_MSG>");
      ds_put_format(&notif_data,"<mon-id>%d</mon-id>",monid);
   	  ds_put_format(&notif_data,"<device-id>%d</device-id>",device_id);
      ds_put_format(&notif_data,"<Port-Number>%llu</Port-Number>",port_no);
	  ds_put_format(&notif_data,"<Source-Ip>%llu</Source-Ip>",src_ip);
      ds_put_format(&notif_data,"<Port-Data>%llu</Port-Data>",statBytes);
      ds_put_format(&notif_data,"</MON_2D_HHH_MSG>");

      char *notif_content = ds_steal_cstr(&notif_data); // Convert the notification content into a normal C-style string.

      zstr_sendm(sock,"mon_event_notification"); // Ask the NETCONF thread to forward the notification to SDN controller via NETCONF server.
      zstr_send(sock,notif_content);

      free(notif_content);
}
