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

fdIN :: FromDump($trace, STOP false, TIMING true)

tdIN :: ToDPDKDevice($txport, BLOCKING true, BURST $bout, VERBOSE $txverbose, IQUEUE $bout, NDESC 0, TCO 1)

fdIN
  -> replay :: ReplayUnqueue(STOP $replay_count, QUICK_CLONE $quick, VERBOSE $txverbose, ACTIVE true, LIMIT $max_packets_in_queue)
  -> unqueue :: BandwidthRatedUnqueue($RATE, LINK_RATE true, ACTIVE true)
  -> tdIN; StaticThreadSched(fdIN 0/1, unqueue 0/1);


