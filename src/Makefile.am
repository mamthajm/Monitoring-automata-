AM_CFLAGS = -I$(top_srcdir)/src -Wall -O3
AM_CFLAGS += $(ZMQ_CFLAGS)
AM_CFLAGS += $(CZMQ_CFLAGS)
AM_CFLAGS += -I/usr/include/libxml2
AM_CFLAGS += -I$(top_srcdir)/src/include -Wall -O3
AM_CFLAGS += -I/mnt/onl/data/libraries/openvswitch-2.3.1/lib/ 
AM_CFLAGS += -I/mnt/onl/data/libraries/openvswitch-2.3.1/include/ 
AM_CFLAGS += -I/mnt/onl/data/libraries/openvswitch-2.3.1/include/openvswitch/

bin_PROGRAMS = Monitor-automata-v2
Monitor_automata_v2_SOURCES = monitor-automata.c monitor-automata.h util.c monitoring_utilities.c l3-interface-config.c zhelpers.h vector.c vector.h lists.c lists.h
Monitor_automata_v2_LDADD = -lzmq -lczmq -lpthread -lxml2 -lopenvswitch -lssl -lcrypto -latomic -lrt -lm -lzlog -lopennsl
