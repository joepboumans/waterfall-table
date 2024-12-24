/*
 * Multi-Threaded packet generator with memory preload. Uses DPDK.
 *
 * This version does not compute latency or receive packets back on another interface.
 * It uses 4 threads to replay packets, after they're preloaded in memory.
 *
 * Example usage: bin/click --dpdk -l 0-15 -- conf/pktgen/pktgen-l2-mt-replaypcap.click trace=/tmp/trace.pcap
 */

define($trace /home/jboumans/smaller_test.pcap)
/* define($bandwidth 2147483647) //Number of bytes per second, max is 32bit max value */
define($bandwidth 20Gbps)

d :: DPDKInfo(NB_SOCKET_MBUF  2040960) //Should be a bit more than 4 times the limit

/* Can be whatever */
define($INsrcmac b8:3f:d2:9f:2e:9b)
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INdstmac b8:3f:d2:b0:d7:79)

define($bout 32)
define($ignore 0)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($txport 0)
define($quick true)
define($txverbose 99)


fdIN :: FromDump($trace, STOP false, TIMING true)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)


elementclass NoNumberise { $magic |
    input
    -> Strip(14) 
    -> check :: MarkIPHeader
    -> Unstrip(14) 
    -> output
}

fdIN
    -> rr :: PathSpinlock;

elementclass Generator { $magic |
    input
      -> EnsureDPDKBuffer
      -> rwIN :: EtherRewrite($INsrcmac,$INdstmac)
      -> Pad()
      -> NoNumberise($magic)
      -> replay :: BandwidthRatedUnqueue($bandwidth)

      -> avgSIN :: AverageCounter(IGNORE $ignore)
      -> { input[0] -> MarkIPHeader(OFFSET 14) -> ipc :: IPClassifier(tcp or udp, -) ->  ResetIPChecksum(L4 true) -> [0]output; ipc[1] -> [0]output; }
      -> output;
}

rr -> gen0 :: Generator(\<5601>) -> tdIN;StaticThreadSched(gen0/replay 0);
rr -> gen1 :: Generator(\<5602>) -> tdIN;StaticThreadSched(gen1/replay 1);
rr -> gen2 :: Generator(\<5603>) -> tdIN;StaticThreadSched(gen2/replay 2);
rr -> gen3 :: Generator(\<5604>) -> tdIN;StaticThreadSched(gen3/replay 3);

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
	    	         write gen0/avgSIN.reset, write gen1/avgSIN.reset, write gen2/avgSIN.reset, write gen3/avgSIN.reset,
                     wait 1,
                     set diff $(sub $(now) $time),
                     print "Diff $diff",
                     set time $(sub $(now) $stime),
                     set sent $(avgSIN.add count),
        		     print "IGEN-$time-RESULT-ICOUNT $received",
                     print "IGEN-$time-RESULT-IDROPPED $(sub $sent $received)",
                     print "IGEN-$time-RESULT-IDROPPEDPS $(div $(sub $sent $received) $diff)",
                     print "IGEN-$time-RESULT-ITHROUGHPUT $rx",

                     print "IGEN-$time-RESULT-ITX $tx",
                     print "IGEN-$time-RESULT-ILOSS $(sub $rx $tx)",
                     goto g);



avgSIN :: HandlerAggregate( ELEMENT gen0/avgSIN,ELEMENT gen1/avgSIN,ELEMENT gen2/avgSIN,ELEMENT gen3/avgSIN );


link_initialized :: Script(TYPE PASSIVE,
    print "Link initialized !",
    wait 1s,
    print "Starting replay...",
    write gen0/avgSIN.reset, write gen1/avgSIN.reset, write gen2/avgSIN.reset, write gen3/avgSIN.reset,
    write gen0/replay.timing $bandwidth, write gen1/replay.rate $bandwidth, write gen2/replay.rate $bandwidth, write gen3/replay.rate $bandwidth,
    write run_test.run 1,
    print "Time is $(now)",
);

DriverManager(
                print "Waiting for preload...",
                pause, pause, pause, pause,
                wait 2s,
                label start,
                write link_initialized.run,
                label waitagain,
                set starttime $(now),
                pause,
                set stoptime $(now),
                set sent $(avgSIN.add count),
                print "RESULT-TESTTIME $(sub $stoptime $starttime)",
                print "RESULT-SENT $sent",
                print "RESULT-TX $(avgSIN.add link_rate)",
                print "RESULT-TXPPS $(avgSIN.add rate)",
                label end,
                print "EVENT GEN_DONE",
                stop);
