/*
 * Multi-Threaded packet generator with memory preload. Uses DPDK.
 *
 * This version does not compute latency or receive packets back on another interface.
 * It uses 4 threads to replay packets, after they're preloaded in memory.
 *
 * Example usage: bin/click --dpdk -l 0-15 -- trace=/src/dataset.pcap
 */

define($trace /data/users/jboumans/equinix-chicago.20160121-130000.UTC.pcap)
define($RATE 20Gbps)
define($max_packets_in_queue 500000)
define($replay_count -1)

//d :: DPDKInfo(NB_SOCKET_MBUF  1048575) //Should be a bit more than 4 times the limit
d :: DPDKInfo(NB_SOCKET_MBUF  2097151) //Should be a bit more than 4 times the limit

/* Can be whatever */
/* Hotpot: b8:3f:d2:b0:d7:79, Grill: b8:3f:d2:9f:2e:9b Onie: 70:b3:d5:cc:ff:3c */
define($INsrcmac b8:3f:d2:b0:d7:79)
define($INdstmac b8:3f:d2:9f:2e:9b)

define($ignore 0)
define($bout 32)

/* Melanox grill: 0000:0a:00.1, 0000:0a:00.0 */
define($txport 0)
define($quick true)
define($txverbose 99)

//switcharoo
fdIN :: FromDump($trace, STOP true, BURST 1, TIMING true, ACTIVE true)

//switcharoo
tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)

elementclass NoNumberise { $magic |
    input
    -> Strip(14) 
    -> check :: MarkIPHeader
    -> Unstrip(14) 
    -> output
}

elementclass GeneratorEtherRewrite { $magic |
    input
      -> EnsureDPDKBuffer
      -> rwIN :: EtherRewrite($INsrcmac,$INdstmac)
      -> Pad()
      -> NoNumberise($magic)
      -> replay :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true)
      -> avgSIN :: AverageCounter(IGNORE $ignore)
      -> { input[0] -> MarkIPHeader(OFFSET 14) -> ipc :: IPClassifier(tcp or udp, -) ->  ResetIPChecksum(L4 true) -> [0]output; ipc[1] -> [0]output; }
      -> output;
}

elementclass Generator { $magic |
    input
    -> MarkMACHeader
    -> EnsureDPDKBuffer
   // -> shaper :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true) // from Habib
    -> EtherEncap(0x08DD, 1:1:1:1:1:1, 2:2:2:2:2:2) // comment this if you want to use test.pcap
    -> doethRewrite :: { input[0] -> active::Switch(OUTPUT 0)[0] -> rwIN :: EtherRewrite($INsrcmac, $INdstmac) -> [0]output; active[1] -> [0]output }
    -> Pad
    -> cnt :: AverageCounter(IGNORE 0)
    -> output;
}

fdIN
-> gen0 :: Generator(\<5700>)
-> tdIN;
