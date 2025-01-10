/*
 * Multi-Threaded packet generator with memory preload. Uses DPDK.
 *
 * This version does not compute latency or receive packets back on another interface.
 * It uses 4 threads to replay packets, after they're preloaded in memory.
 *
 * Example usage: bin/click --dpdk -l 0-15 -- conf/pktgen/pktgen-l2-mt-replaypcap.click trace=/tmp/trace.pcap
 */

//define($trace /data-1/users/jboumans/equinix-chicago.20160121-130000.UTC.pcap)
define($trace /data-1/users/jboumans/single_test.pcap)
define($max_packets_in_queue 500000)
define($replay_count -1)

d :: DPDKInfo(NB_SOCKET_MBUF  524288) //Should be a bit more than 4 times the limit

/* Can be whatever */
define($INsrcmac b8:3f:d2:9f:2e:9b)
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INdstmac b8:3f:d2:b0:d7:79)

define($bout 32)
define($ignore 0)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($txport 0)
define($quick false)
define($txverbose 99)


fdIN :: FromDump($trace, STOP false, TIMING true)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)

fdIN
    -> replay :: ReplayUnqueue(STOP $replay_count, QUICK_CLONE $quick, VERBOSE false, ACTIVE false, LIMIT $max_packets_in_queue)
    -> avgSIN :: AverageCounter
    -> tdIN;

run_test :: Script(TYPE PASSIVE,
            wait 0s,
            print "EVENT GEN_BEGIN",
            print "Starting bandwidth computation !",
            print "$GEN_PRINT_START",
            label end);


//To display stats every seconds, change PASSIVE by ACTIVE
display_th :: Script(TYPE PASSIVE,
                    print "Starting iterative...",

                     set stime $(now),
                     label g,
	    	         write avgSIN.reset,
                     wait 1,
                     set diff $(sub $(now) $time),
                     print "Diff $diff",
                     set time $(sub $(now) $stime),
                     set sent $(avgSIN.count),
        		     print "IGEN-$time-RESULT-ICOUNT $received",
                     print "IGEN-$time-RESULT-IDROPPED $(sub $sent $received)",
                     print "IGEN-$time-RESULT-IDROPPEDPS $(div $(sub $sent $received) $diff)",
                     print "IGEN-$time-RESULT-ITHROUGHPUT $rx",

                     print "IGEN-$time-RESULT-ITX $tx",
                     print "IGEN-$time-RESULT-ILOSS $(sub $rx $tx)",
                     goto g);


link_initialized :: Script(TYPE PASSIVE,
    print "Link initialized !",
    wait 1s,
    print "Starting replay...",
    write avgSIN.reset,
    write replay.stop $replay_count, write replay.active true,
    print "Time is $(now)",
);

DriverManager(
                print "Waiting for preload...",
//                pause, pause, pause, pause,
//                wait 3s,
                label start,
                write link_initialized.run,
                label waitagain,
                set starttime $(now),
                pause,
                set stoptime $(now),
                set sent $(avgSIN.count),
                print "RESULT-TESTTIME $(sub $stoptime $starttime)",
                print "RESULT-SENT $sent",
                print "RESULT-TX $(avgSIN.link_rate)",
                print "RESULT-TXPPS $(avgSIN.rate)",
                label end,
                print "EVENT GEN_DONE",
                stop);
