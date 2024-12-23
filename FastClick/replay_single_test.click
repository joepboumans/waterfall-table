define($trace /data-1/users/jboumans/single_test.pcap)
define($RATE 1Mbps)
define($max_packets_in_queue 500000)
define($replay_count -1)

//d :: DPDKInfo(NB_SOCKET_MBUF  1048575) //Should be a bit more than 4 times the limit

/* Can be whatever */
define($INsrcmac b8:3f:d2:9f:2e:9b)
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INdstmac b8:3f:d2:b0:d7:79)

define($ignore 0)
define($bout 32)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($txport 0)
define($quick false)
define($txverbose 99)


//fdIN :: FromDump($trace, STOP false, BURST 100)

//switcharoo
fdIN :: FromDump($trace, STOP true, BURST 1, TIMING false, TIMING_FNT "100", ACTIVE true)

//tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)

//tdIN :: ToDPDKDevice($txport)

//switcharoo
tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 0)

//fdIN  -> tdIN;
//fdIN -> BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true) -> tdIN;

elementclass Numberise { $magic |
    input -> Strip(14) -> check :: MarkIPHeader -> nPacket :: NumberPacket(42) -> StoreData(40, $magic) -> ResetIPChecksum -> Unstrip(14) -> output
}

elementclass Generator { $magic |
    input
    -> MarkMACHeader
    -> EnsureDPDKBuffer
   // -> shaper :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true) // from Habib
    -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2) // comment this if you want to use test.pcap
    -> doethRewrite :: { input[0] -> active::Switch(OUTPUT 0)[0] -> rwIN :: EtherRewrite($INsrcmac, $INdstmac) -> [0]output; active[1] -> [0]output }
    -> Pad
    -> Numberise($magic)
    //-> sndavg :: AverageCounter(IGNORE 0)
    -> cnt :: AverageCounter(IGNORE 0)
    -> output;
}

//fdIN -> unqueue0 :: Unqueue() -> gen0 :: Generator(\<5700>) -> tdIN; StaticThreadSched(fdIN 0/1, unqueue0 0/1);
fdIN
//-> replay0 :: ReplayUnqueue(STOP -1, ACTIVE true)
 -> unqueue0 :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true)
-> gen0 :: Generator(\<5700>) -> tdIN; StaticThreadSched(fdIN 0/1, unqueue0 0/1);

pkt_cnt :: HandlerAggregate(ELEMENT gen0/cnt);


ig :: Script(TYPE ACTIVE,
    set s $(now),
    set lastcount 0,
    set lastbytes 0,
    set lastbytessent 0,
    set lastsent 0,
    set lastdrop 0,
    set last $s,
    set indexB 0,
    set indexC 0,
    set indexD 0,
    label loop,
    wait 0.5s,
    set n $(now),
    set t $(sub $n $s),
    set elapsed $(sub $n $last),
    set last $n,
    set count $(pkt_cnt.add count),
    print "SENT PKTS $count",
    //set count $(avg.add count),
    //set sent $(sndavg.add count),
    //set bytessent $(sndavg.add byte_count),
    //set bytes $(avg.add byte_count),
    //print "IG-$t-RESULT-IGCOUNT $(sub $count $lastcount)",
    //print "IG-$t-RESULT-IGSENT $(sub $sent $lastsent)",
    //print "IG-$t-RESULT-IGBYTESSENT $(sub $bytessent $lastbytessent)",
    //set drop $(sub $sent $count),
    //print "IG-$t-RESULT-IGDROPPED $(sub $drop $lastdrop)",
    //set lastdrop $drop,
    //print "IG-$t-RESULT-IGTHROUGHPUT $(div $(mul $(add $(mul $(sub $count $lastcount) 24) $(sub $bytes $lastbytes)) 8) $elapsed)",
    //set lastcount $count,
    //set lastsent $sent,
    //set lastbytes $bytes,
    //set lastbytessent $bytessent,
    goto loop
)

StaticThreadSched(ig 15);
