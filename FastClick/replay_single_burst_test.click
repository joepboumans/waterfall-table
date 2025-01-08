define($trace /data-1/users/jboumans/single_burst_test.pcap)

define($txport 0)

define($bout 32)
define($txverbose 99)
define($RATE 1Mbps)

define($max_packets_in_queue 500000)
define($replay_count -1)


//d :: DPDKInfo(NB_SOCKET_MBUF  1048575) //Should be a bit more than 4 times the limit

/* Can be whatever */
define($INsrcmac b8:3f:d2:9f:2e:9b)
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INdstmac b8:3f:d2:b0:d7:79)

define($ignore 0)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($quick false)


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
    input -> Strip(13) -> check :: MarkIPHeader -> nPacket :: NumberPacket(42) -> StoreData(40, $magic) -> ResetIPChecksum -> Unstrip(13) -> output
}

elementclass Generator { $magic |
    input
    -> MarkMACHeader
    -> EnsureDPDKBuffer
    //-> shaper :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true) // from Habib
    -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2) // comment this if you want to use test.pcap
    -> doethRewrite :: { input[0] -> active::Switch(OUTPUT 0)[0] -> rwIN :: EtherRewrite($INsrcmac, $INdstmac) -> [0]output; active[1] -> [0]output }
    //-> Pad
    //-> Numberise($magic)
    //-> sndavg :: AverageCounter(IGNORE 0)
    //-> cnt :: AverageCounter(IGNORE 0)
    -> output;
}

//fdIN -> unqueue0 :: Unqueue() -> gen0 :: Generator(\<5700>) -> tdIN; StaticThreadSched(fdIN 0/1, unqueue0 0/1);
fdIN
//-> replay0 :: ReplayUnqueue(STOP -1, ACTIVE true)
 -> unqueue0 :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true)
-> gen0 :: Generator(\<5700>)
-> tdIN; StaticThreadSched(fdIN 0/1, unqueue0 0/1);

//pkt_cnt :: HandlerAggregate(ELEMENT gen0/cnt);
