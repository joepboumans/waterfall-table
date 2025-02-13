import os
from pathlib import Path
from ctypes import *

import logging
logger = logging.getLogger("EM_ctype")

if not len(logger.handlers):
    sh = logging.StreamHandler()
    formatter = logging.Formatter('[%(levelname)s - %(name)s - %(funcName)s]: %(message)s')
    sh.setFormatter(formatter)
    sh.setLevel(logging.INFO)
    logger.addHandler(sh)

DEPTH = 2
NUM_STAGES = 3
SKETCH_W1 = 524288              # 8-bit, level 1
SKETCH_W2 = 65536               # 16-bit, level 2
SKETCH_W3 = 8192                # 32-bit, level 3
ADD_LEVEL1 = 255                # 2^8 -2 + 1 (actual count is 254)
ADD_LEVEL2 = 65789              # (2^8 - 2) + (2^16 - 2) + 1 (actual count is 65788)

Stage_szes = c_uint32 * NUM_STAGES
Stage1 = c_uint32 * SKETCH_W1
Stage2 = c_uint32 * SKETCH_W2
Stage3 = c_uint32 * SKETCH_W3
Tuple = c_uint8 * 14 # Tuple is not resized for different flow types (FiveTuple + sz)

class EM_WFCM(object):
    lib = cdll.LoadLibrary(f"{os.path.dirname(__file__)}/EM/lib/libEM_WFCM.so")
    lib.EM_WFCM_new.restype = c_void_p

    lib.get_ns.restype = c_void_p
    lib.get_ns.argtypes = [c_void_p]
    lib.vector_size.restype = c_size_t
    lib.vector_size.argtypes = [c_void_p]
    lib.vector_get.restype = c_double
    lib.vector_get.argtypes = [c_void_p, c_size_t]

    def __init__(self, s1, s2, s3, in_tuples):
        # logger.info(in_tuples)
        Tuples = Tuple * len(in_tuples)
        tuples = Tuples()
        in2Tuples = []
        for val in in_tuples:
            in2Tuples.append(Tuple(*list(val)))
        for i in range(len(in2Tuples)):
            tuples[i] = in2Tuples[i]

        # Needs separate list per depth as passing to c++ does not work well with c_uint32** 
        stage1_1 = Stage1()
        for i in range(len(stage1_1)):
            if s1[0][i] != 0:
                print(f"{i}:{s1[0][i]} ", end="")
            if i < len(s1[0]):
                stage1_1[i] = s1[0][i]
            else:
                stage1_1[i] = 0
        print("[Waterfall - py ctypes] S1_1 done")

        stage1_2 = Stage1()
        for i in range(len(stage1_2)):
            if s1[1][i] != 0:
                print(f"{i}:{s1[1][i]} ", end="")
            if i < len(s1[1]):
                stage1_2[i] = s1[1][i]
            else:
                stage1_2[i] = 0
        print("[Waterfall - py ctypes] S1_2 done")

        stage2_1 = Stage2()
        for i in range(len(stage2_1)):
            if i < len(s2[0]):
                stage2_1[i] = s2[0][i]
            else:
                stage2_1[i] = 0
        print("[Waterfall - py ctypes] S2_1 done")
        
        stage2_2 = Stage2()
        for i in range(len(stage2_2)):
            if i < len(s2[1]):
                stage2_2[i] = s2[1][i]
            else:
                stage2_2[i] = 0
        print("[Waterfall - py ctypes] S2_2 done")

        stage3_1 = Stage3()
        for i in range(len(stage3_1)):
            if i < len(s3[0]):
                stage3_1[i] = s3[0][i]
                if (s3[0][i] > 0):
                    print(s3[0][i])
            else:
                stage3_1[i] = 0
        print("[Waterfall - py ctypes] S3_1 done")
        
        stage3_2 = Stage3()
        for i in range(len(stage3_2)):
            if i < len(s3[1]):
                stage3_2[i] = s3[1][i]
            else:
                stage3_2[i] = 0
        print("[Waterfall - py ctypes] S3_2 done")

        self.obj = c_void_p(EM_WFCM.lib.EM_WFCM_new(stage1_1, stage1_2, stage2_1, stage2_2, stage3_1, stage3_2, tuples, len(tuples)))

    def next_epoch(self):
        EM_WFCM.lib.EM_WFCM_next_epoch(self.obj)

    def get_ns(self, ns):
        c_ns = EM_WFCM.lib.get_ns(self.obj)
        for i in range(EM_WFCM.lib.vector_size(c_ns)):
            x = EM_WFCM.lib.vector_get(c_ns, i)
            ns.append(x)
        return ns

    def run_em(self, iters):
        print(f"Run EM_FSD over {iters} iterations")
        for i in range(iters):
            self.next_epoch()

        print(f"Finished estimation")
        ns = self.get_ns([])
        # print(f"FSD is : ")
        # print(ns)
        return ns

if __name__ == "__main__":
    print("Creating EM_FSD")
    s1 = [ [ 255,2,3 ], [ 255,12,13 ] ]
    s2 = [ [ 12,13,14 ], [ 12,13,14 ] ]
    s3 = [ [ 103,104,105 ], [ 13,14,15 ] ]
    test_tuple = [ 1, 2, 3, 4]
    test_tuple2 = [ 12, 13, 14, 15]
    test_tuple3 = [ 1, 2, 3, 4,]
    tuples = [Tuple(*[i for i in range(1,5)]) for _ in range(1, 4)]
    t1 = Tuple(*test_tuple)
    t2 = Tuple(*test_tuple2)
    t3 = Tuple(*test_tuple3)
    tuples = [ t1, t2, t3]
    f = EM_WFCM(s1, s2, s3, tuples)
    f.run_em(5)
