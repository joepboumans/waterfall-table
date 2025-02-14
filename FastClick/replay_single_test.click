define($trace /data/users/jboumans/single_test.pcap)
define($RATE 1Gbps)
define($max_packets_in_queue 500000)
define($replay_count -1)

d :: DPDKInfo( NB_SOCKET_MBUF 655350 )
define($length 990, $rate 1)

/* Can be whatever */
define($INsrcmac b8:3f:d2:9f:2e:9b)
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INdstmac b8:3f:d2:b0:d7:79)

define($ignore 0)
define($bout 1)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($txport 0)
define($quick false)
define($txverbose 99)

fdIN :: FromDump($trace, STOP true, BURST 1, TIMING false, TIMING_FNT "100", ACTIVE true)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 0)

elementclass Generator { $magic |
    input
    -> MarkMACHeader
    -> EnsureDPDKBuffer
    //-> shaper :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true) // from Habib
    -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2) // comment this if you want to use test.pcap
    -> doethRewrite :: { input[0] -> active::Switch(OUTPUT 0)[0] -> rwIN :: EtherRewrite($INsrcmac, $INdstmac) -> [0]output; active[1] -> [0]output }
    -> Pad
    -> output;
}

fdIN
//-> replay0 :: ReplayUnqueue(STOP -1, ACTIVE true)
//-> unqueue0 :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true)
-> gen0 :: Generator(\<5700>)
-> tdIN; StaticThreadSched(fdIN 0/1);
