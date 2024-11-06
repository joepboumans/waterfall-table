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

print(os.getcwd())
lib = cdll.LoadLibrary(f"{os.getcwd()}/ptf/EM/lib/libEM_FSD.so")

class EM_FSD(object):
    def __init__(self, s1, s2, s3):
        stage1 = Stage1()
        for i in range(len(stage1)):
            if i < len(s1):
                stage1[i] = s1[i]
            else:
                stage1[i] = 0
        print("S1 done")
        for s in stage1:
            print(s, end="")

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

        self.obj = lib.EMFSD_new(stage_sz, stage1, stage2, stage3)

    def next_epoch(self):
        lib.EMFSD_next_epoch(self.obj)

print("Creating EM_FSD")
s1 = [ 1,2,3 ]
s2 = [ 4,5,6 ]
s3 = [ 7,8,9 ]
f = EM_FSD(s1, s2, s3)
print("done!")
# f.next_epoch()
