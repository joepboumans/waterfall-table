import os
from ctypes import *

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
FiveTuple = c_uint8 * 13

print(os.getcwd())
lib = cdll.LoadLibrary(f"{os.getcwd()}/ptf/EM/lib/libEM_FSD.so")
# lib.EMFSD_new.argtypes = [POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint32), POINTER(POINTER(c_uint8)), c_uint32]
lib.EMFSD_new.restype = None

class EM_FSD(object):
    def __init__(self, s1, s2, s3, in_tuples):

        Tuples = FiveTuple * len(in_tuples)
        tuples = Tuples()
        for i in range(len(in_tuples)):
            tuples[i] = in_tuples[i]

        stage1 = Stage1()
        for i in range(len(stage1)):
            if i < len(s1):
                stage1[i] = s1[i]
            else:
                stage1[i] = 0
        print("S1 done")

        stage2 = Stage2()
        for i in range(len(stage2)):
            if i < len(s2):
                stage2[i] = s2[i]
            else:
                stage2[i] = 0
        print("S2 done")

        stage3 = Stage3()
        for i in range(len(stage3)):
            if i < len(s3):
                stage3[i] = s3[i]
            else:
                stage3[i] = 0
        print("S3 done")

        stage_sz = Stage_szes(SKETCH_W1, SKETCH_W2, SKETCH_W3)
        print("stage szes done")

        self.obj = lib.EMFSD_new(stage_sz, stage1, stage2, stage3, tuples, len(tuples))

    def next_epoch(self):
        lib.EMFSD_next_epoch(self.obj)

print("Creating EM_FSD")
s1 = [ 1,2,3 ]
s2 = [ 4,5,6 ]
s3 = [ 7,8,9 ]
test_tuple = [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
test_tuple2 = [ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
t1 = FiveTuple(*test_tuple)
t2 = FiveTuple(*test_tuple2)
tuples = [ t1, t2]
f = EM_FSD(s1, s2, s3, tuples)
print("done!")
# f.next_epoch()
