/***************************************************************************
 *  Copyright [2017] [TU Darmstadt]
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
#include "monitor-automata.h"

#ifdef opennsl


#ifdef ACCTON_7712

	#define INGRESS_VLAN    20
	#define EGRESS_VLAN     10
	#define HOST1           0x14010102         /* 20.1.1.2 */
	#define DEFAULT_SUBNET_IP   0x37000000     /* 55.0.0.0 */
	#define DEFAULT_SUBNET_MASK 0xff000000     /* /8 network */
	//#define MY_MAC          {0x00, 0x11, 0x22, 0x33, 0x99, 0x58}
	#define MY_MAC     {0x00, 0x00, 0x70, 0x5B, 0xC7, 0x34}
	#define NEXTHOP_MAC          {0x00, 0x00, 0x70, 0x5B, 0xC7, 0x35}

#else

	#define INGRESS_VLAN    10
	#define EGRESS_VLAN     20
	#define HOST1           0x14010102         /* 20.1.1.2 */
	#define DEFAULT_SUBNET_IP   0x37000000     /* 55.0.0.0 */
	#define DEFAULT_SUBNET_MASK 0xff000000     /* /8 network */
	#define MY_MAC          {0x00, 0x11, 0x22, 0x33, 0x99, 0x58}
	#define NEXTHOP_MAC     {0x00, 0x00, 0x70, 0x5B, 0xC7, 0x34}

#endif
/**************************************************************************//**
 * \brief To add a given MAC and VID to the L2 forwarding database.
 *
 *
 * \param    unit [IN]    Unit number.
 * \param    mac  [IN]    MAC address
 * \param    port [IN]    Port number
 * \param    vid  [IN]    VLAN identifier
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 *****************************************************************************/
int example_l2_addr_add(int unit, opennsl_mac_t mac, int port, int vid) {

  int rv;
  opennsl_l2_addr_t l2addr;

  opennsl_l2_addr_t_init(&l2addr, mac, vid);
  l2addr.port  = port;
  l2addr.flags = (OPENNSL_L2_L3LOOKUP | OPENNSL_L2_STATIC);

  rv = opennsl_l2_addr_add(unit, &l2addr);
  if (rv != OPENNSL_E_NONE) {
    return rv;
  }

  return OPENNSL_E_NONE;
}

/**************************************************************************//**
 * \brief To add a port to the VLAN
 *
 *
 * \param    unit       [IN]    Unit number.
 * \param    open_vlan  [IN]    if TRUE create given vlan, FALSE: vlan already
 *                              opened juts use it
 * \param    vlan       [IN]    VLAN identifier
 * \param    port       [IN]    Port number
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 *****************************************************************************/
int example_vlan_port_add(int unit, int open_vlan,
    opennsl_vlan_t vlan, int port)
{
  opennsl_pbmp_t  pbmp, upbmp;
  int rc = OPENNSL_E_NONE;

  if(open_vlan) {
    rc = opennsl_vlan_create(unit, vlan);
    if (rc != OPENNSL_E_NONE) {
      dzlog_info("failed to create vlan(%d). Return code %s",
          vlan, opennsl_errmsg(rc));
      return rc;
    }
  	//For Debugging only.
    dzlog_info("Created VLAN: %d\n", vlan);

  }

  /* Add the test ports to the vlan */
  OPENNSL_PBMP_CLEAR(pbmp);
  OPENNSL_PBMP_CLEAR(upbmp);
  OPENNSL_PBMP_PORT_ADD(pbmp, port);
  rc = opennsl_vlan_port_add(unit, vlan, pbmp, upbmp);
  if (rc != OPENNSL_E_NONE && rc != OPENNSL_E_EXISTS) {
    return rc;
  }
  return rc;
}

/**************************************************************************//**
 * \brief Creates Router interface
 *       - packets sent in from this interface identified by <port, vlan> with
 *         specificed MAC address is subject of routing
 *       - packets sent out through this interface will be encapsulated with
 *         <vlan, mac_addr>
 *
 *
 * \param    unit       [IN]    Unit number.
 * \param    open_vlan  [IN]    if TRUE create given vlan, FALSE: vlan already
 *                              opened juts use it
 * \param    port       [IN]    Port number
 * \param    vid        [IN]    router interface VLAN
 * \param    mac_addr   [IN]    my MAC address
 * \param    l3_id      [OUT]   returned handle of opened l3-interface
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 *****************************************************************************/
int example_create_l3_intf(int unit, int open_vlan, opennsl_gport_t port,
    opennsl_vlan_t vid, opennsl_mac_t mac_addr, int *l3_id) {

  int rc;
  opennsl_l3_intf_t l3_intf;

  /* Create VLAN and add port to the VLAN membership */
  rc = example_vlan_port_add(unit, open_vlan, vid, port);
  if (rc != OPENNSL_E_NONE && rc != OPENNSL_E_EXISTS) {
    dzlog_info("Fail to add port %d to vlan %d. Return Code: %s\n", port, vid, opennsl_errmsg(rc));
    return rc;
  }

    printf("Port %d is participating in VLAN: %d\n", port, vid);


  /* Create L3 interface */
  opennsl_l3_intf_t_init(&l3_intf);
  memcpy(l3_intf.l3a_mac_addr, mac_addr, 6);
  l3_intf.l3a_vid = vid;
  rc = opennsl_l3_intf_create(unit, &l3_intf);
  if (rc != OPENNSL_E_NONE) {
    dzlog_info("l3_setup: opennsl_l3_intf_create failed: %s\n", opennsl_errmsg(rc));
    return rc;
  }

  *l3_id = l3_intf.l3a_intf_id;

	//For Debugging only.
    dzlog_info("L3 interface is created with parameters: \n  VLAN %d \n", vid);
    l2_print_mac("  MAC Address: ", mac_addr);
    dzlog_info("  L3 Interface ID: %d\r\n", l3_intf.l3a_intf_id);


  return rc;
}

/**************************************************************************//**
 * \brief create l3 egress object
 *       object includes next hop information.
 *       - packets sent to this interface will be send through out_port over
 *         given l3_eg_intf with next next_hop_mac_addr VLAN. SA is driven from
 *         l3_eg_intf as configure in example_create_l3_intf().
 *
 *
 * \param    unit              [IN]    Unit number
 * \param    flags             [IN]    special controls set to zero
 * \param    out_port          [IN]    egress port
 * \param    vlan              [IN]    VLAN identifier
 * \param    l3_eg_intf        [IN]    egress router interface will
 *                                     derive (VLAN, SA)
 * \param    next_hop_mac_addr [IN]    next hop mac address
 * \param    *intf             [OUT]   returned interface ID
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 ******************************************************************************/
int example_create_l3_egress(int unit, unsigned int flags, int out_port, int vlan,
    int l3_eg_intf, opennsl_mac_t next_hop_mac_addr,
    int *intf) {
  int rc;
  opennsl_l3_egress_t l3eg;
  opennsl_if_t l3egid;
  int mod = 0;

  opennsl_l3_egress_t_init(&l3eg);
  l3eg.intf = l3_eg_intf;
  memcpy(l3eg.mac_addr, next_hop_mac_addr, 6);

  l3eg.vlan   = vlan;
  l3eg.module = mod;
  l3eg.port   = out_port;

  l3egid = *intf;

  rc = opennsl_l3_egress_create(unit, flags, &l3eg, &l3egid);
  if (rc != OPENNSL_E_NONE) {
    return rc;
  }

  *intf = l3egid;

  	//For Debugging only.
    dzlog_info("Created L3 egress ID %d for out_port: %d vlan: %d "
        "L3 egress intf: %d\n",
        *intf, out_port, vlan, l3_eg_intf);


  return rc;
}

/**************************************************************************//**
 * \brief Add host address to the routing table
 *
 * \param    unit [IN]    Unit number.
 * \param    addr [IN]    32 bit IP address value
 * \param    intf [IN]    egress object created using example_create_l3_egress
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 *****************************************************************************/
int example_add_host(int unit, unsigned int addr, int intf) {
  int rc;
  opennsl_l3_host_t l3host;
  opennsl_l3_host_t_init(&l3host);

  l3host.l3a_flags = 0;
  l3host.l3a_ip_addr = addr;
  l3host.l3a_intf = intf;
  l3host.l3a_port_tgid = 0;

  rc = opennsl_l3_host_add(unit, &l3host);
  if (rc != OPENNSL_E_NONE) {
    dzlog_error ("opennsl_l3_host_add failed. Return Code: %s \n",
        opennsl_errmsg(rc));
    return rc;
  }

    print_ip_addr("add host ", addr);  //For debugging only
    dzlog_info(" ---> egress-object = %d\n", intf);


  return rc;
}

/**************************************************************************//**
 * \brief Add default route to a subnet
 *
 * \param    unit      [IN] Unit number.
 * \param    subnet    [IN] 32 bit IP subnet
 * \param    mask      [IN] 32 bit value network mask
 * \param    intf      [IN] Egress object created using example_create_l3_egress
 * \param    trap_port [IN] Trap destination. if trap_port is valid trap then it
 *                         used for default destination. otherwise intf
 *                         (egress-object) is used
 *
 * \return      OPENNSL_E_xxx  OpenNSL API return code
 *****************************************************************************/
int example_set_default_route(int unit, int subnet, int mask,
    int intf, opennsl_gport_t trap_port) {
  int rc;
  opennsl_l3_route_t l3rt;

  opennsl_l3_route_t_init(&l3rt);

  l3rt.l3a_flags = 0;

  /* to indicate it's default route, set subnet to zero */
  l3rt.l3a_subnet = subnet;
  l3rt.l3a_ip_mask = mask;

  l3rt.l3a_intf = intf;
  l3rt.l3a_port_tgid = trap_port;

  rc = opennsl_l3_route_add(unit, &l3rt);
  if (rc != OPENNSL_E_NONE) {
    dzlog_error ("opennsl_l3_route_add failed. Return Code: %s \n",
        opennsl_errmsg(rc));
    return rc;
  }

  	//For debugging only
    print_ip_addr("add default route for subnet ", subnet);
    print_ip_addr(" with mask ", mask);
    dzlog_info(" ---> egress-object = %d\n", intf);

  return rc;
}


int configure_test_environment()
{

	  int rv = 0;
	  int unit = 0;
	  int choice;
	  int ing_intf_in = 0;
	  int ing_intf_out = 0;
	  int l3egid;
	  int flags = 0;
	  int open_vlan = 1;
	  
 #ifdef ACCTON_7712	  
	  int in_sysport = 54;
	  int out_sysport = 55; 
 #else
	  int in_sysport = 2;
	  int out_sysport = 6; 	  
 #endif 	  
	  int in_vlan  = INGRESS_VLAN;
	  int out_vlan = EGRESS_VLAN;
	  int host, subnet, mask;
	  opennsl_mac_t my_mac  = MY_MAC;  /* my-MAC */
	  opennsl_mac_t next_hop_mac  = NEXTHOP_MAC; /* next_hop_mac1 */
	  unsigned int warm_boot;


	 //This is already done. So, do not invoke this again.
//    rv = example_port_default_config(unit);
//    if (rv != OPENNSL_E_NONE) {
//      printf("\r\nFailed to apply default config on ports, rc = %d (%s).\r\n",
//             rv, opennsl_errmsg(rv));
//    }

    /* Set L3 Egress Mode */
	rv =  opennsl_switch_control_set(unit, opennslSwitchL3EgressMode, 1);
	if (rv != OPENNSL_E_NONE) {
	  return rv;
	}

	dzlog_info("\nL3 Egress mode is set succesfully\n");

    /*** create ingress router interface ***/ //Refer: create_l3_router_interface
    rv = example_create_l3_intf(unit, open_vlan, in_sysport, in_vlan,
        my_mac, &ing_intf_in);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, create ingress interface-1, in_sysport=%d. "
          "Return code %s \n", in_sysport, opennsl_errmsg(rv));
      return rv;
    }

    /*** create egress router interface ***/
    rv = example_create_l3_intf(unit, open_vlan, out_sysport, out_vlan,
        my_mac, &ing_intf_out);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, create egress interface-1, out_sysport=%d. "
          "Return code %s \n", out_sysport, opennsl_errmsg(rv));
      return rv;
    }


    /*** Make the address learn on a VLAN and port */
    rv = example_l2_addr_add(unit, my_mac, in_sysport, in_vlan);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Failed to add L2 address. Return Code: %s\n", opennsl_errmsg(rv));
      return rv;
    }

    rv = example_l2_addr_add(unit, my_mac, out_sysport, out_vlan);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Failed to add L2 address. Return Code: %s\n", opennsl_errmsg(rv));
      return rv;
    }

    /*** create egress object 1 ***/
    flags = 0;
    rv = example_create_l3_egress(unit, flags, out_sysport, out_vlan, ing_intf_out,
        next_hop_mac, &l3egid);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, create egress object, out_sysport=%d. Return code %s \n",
          out_sysport, opennsl_errmsg(rv));
      return rv;
    }

    /*** add host point ***/
    host = HOST1;
    rv = example_add_host(unit, host, l3egid);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, host add. Return code %s\n",
          opennsl_errmsg(rv));
      return rv;
    }


    /* With ALPM enabled, switch expects a default route to be
     installed before adding a LPM route */
    subnet = 0x00000000;
    mask   = 0x00000000;
    rv = example_set_default_route(unit, subnet, mask, l3egid, 0);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, default route add. Return code %s \n",
          opennsl_errmsg(rv));
      return rv;
    }

    /*** add default route ***/
    subnet = DEFAULT_SUBNET_IP;
    mask   = DEFAULT_SUBNET_MASK;
    rv = example_set_default_route(unit, subnet, mask, l3egid, 0);
    if (rv != OPENNSL_E_NONE) {
    	dzlog_error("Error, default route add. Return code %s\n",
          opennsl_errmsg(rv));
      return rv;
    }

    return 0;
}

#endif
