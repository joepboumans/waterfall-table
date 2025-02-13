# import numpy as math
import math
import zlib
import struct

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


# Cardinality with adaptive spacing for saving TCAM, using the sensitivity of LC estimator. 
# This technique makes an additional error at most 0.3 %
def lc_cardinality(m_occupied, m):
    return m * math.log(m / float(m-m_occupied))

def lc_delta_m0(m_occupied, m, epsilon):
    return (m - m_occupied) * math.log(m / float(m - m_occupied)) * epsilon


def fcm_crc32_mpeg2(msg="192.168.1.1"):
    crc = int(0xFFFFFFFF)
    msb = int(0)
    msg_arr = msg.split(".") # convert IP address to 8-bit values
    
    for i in range(len(msg_arr)):
        # xor next byte to upper bits of crc
        crc ^= int(int(msg_arr[i]) << 24)
        for j in range(8):
            msb = int(crc >> 31)
            crc = int(crc << 1)
            crc = int(crc ^ (int(0 - msb) & int(0x04C11DB7)));
    return crc

def do_crc(s):
    n = zlib.crc32(s)
    return n + (1<<32) if n < 0 else n

# Caclulates the hash for CRC32 algorithm of Tofino
def fcm_crc32(msg="192.168.1.1"):
    msg_arr = msg.split(".")
    msg_val = b''
    for i in range(len(msg_arr)):
        msg_val = msg_val + struct.pack("B", int(msg_arr[i]))
    print(msg_val.hex())
    # msg_val = b'\xc0\xa8\x01\x01'
    n = zlib.crc32(msg_val)
    print(n)
    print(n.to_bytes(4, 'big').hex())
    return n + (1<<32) if n < 0 else n

# Caclulates the hash for CRC32 algorithm of Tofino
# Inital values is reversed order compared to pipeline init_val
def fcm_crc32_init_val(msg="192.168.1.1", init_val=0):
    msg_arr = msg.split(".")
    msg_val = b''
    for i in range(len(msg_arr)):
        msg_val = msg_val + struct.pack("B", int(msg_arr[i]))
    # msg_val = b'\xc0\xa8\x01\x01'
    n = zlib.crc32(msg_val, init_val)
    print(msg_val.hex())
    print(n)
    print(n.to_bytes(4, 'big').hex())
    return n + (1<<32) if n < 0 else n

# print("util directory - test : %f" % lc_cardinality(10000, 524288))
## debugging
# print(fcm_crc32("1.2.3.4.5.6.7.8.9.10.11.12.13") % 524288 ) # 408597
# print(fcm_crc32_mpeg2("1.2.3.4.5.6.7.8.9.10.11.12.13") % 524288 ) # 408597
# print(fcm_crc32("2.3.4.5.6.7.8.9.10.11.12.13.14") % 524288 ) # 408597
print(fcm_crc32_mpeg2("183.84.57.210.134.215.113.73.20.117.20.35.6") % 524288 ) # 408597
# print(fcm_crc32_mpeg2() % 524288 ) # 465664
