import numpy as np
import math
import zlib
import struct

# Caclulates the hash for CRC32 algorithm of Tofino
# Inital values is reversed order compared to pipeline init_val
def crc32(src_addr, dst_addr, src_port, dst_port, protocol, init_val):
    src_val = b''
    for x in src_addr.split('.'):
        src_val += struct.pack("B", int(x))

    dst_val = b''
    for x in dst_addr.split('.'):
        dst_val += struct.pack("B", int(x))

    srcp_val = int(src_port).to_bytes(2, 'big')
    dstp_val = int(dst_port).to_bytes(2, 'big')
    protocol = int(protocol).to_bytes(1, 'big')
    bytes_string = src_val + dst_val + srcp_val + dstp_val + protocol
    print(f"src addrs : {src_val.hex()}")
    print(f"dst addrs : {dst_val.hex()}")
    print(f"srcp  : {srcp_val.hex()}")
    print(f"dstp  : {dstp_val.hex()}")
    print(f"protocol  : {protocol.hex()}")
    print(bytes_string.hex())
    
    init_val = 0xFFFFFFFF - init_val
    n = zlib.crc32(bytes_string, init_val)
    return n + (1<<32) if n < 0 else n

# Use the reversed init_val specified in the P4 Hash definition
def crc32_rehash(ip_hash, init_val):
    hash_bytes = ip_hash.to_bytes(4, byteorder='little')
    # Hash(idx + remain) + emtpy dst addr + emtpy srcp + emtpry dstp + empty protocol
    bytes_string = hash_bytes + (0).to_bytes(9, 'big')
    print(bytes_string.hex())
    
    init_val = 0xFFFFFFFF - init_val
    n = zlib.crc32(bytes_string, init_val)
    return n + (1<<32) if n < 0 else n
