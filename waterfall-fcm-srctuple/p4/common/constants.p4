#ifndef _CONSTANTS_
#define _CONSTANTS_
// Update source code as well to force clean make when updating these

#define NUMBER_OF_PORTS (1 << PORT_ID_WIDTH)
#define NUMBER_OF_VLANS (0xFFF)
#define NUMBER_OF_MEMBERSET_PAIRS 100

#define NUMBER_OF_SLOPE_ENTRIES 101
#define NUMBER_OF_COST_ENTRIES 161600

#define NUMBER_OF_CBS_REGISTERS (NUMBER_OF_PORTS * 8)
// 5 for 10 Gbps TODO: change 
// 9 for 1 Gbps  TODO: change
// 9 for 10 Mbps
// 16 for 100 Kbps
// 28 for local model
#define TIMESTAMP_UNIT_SHIFT 8

#endif /* _CONSTANTS_ */
