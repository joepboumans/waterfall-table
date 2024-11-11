import os
from ctypes import *

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
FiveTuple = c_uint8 * 13

lib = cdll.LoadLibrary(f"{os.getcwd()}/ptf/EM/lib/libEM_FSD.so")
lib.EMFSD_new.restype = c_void_p

class EM_FSD(object):
    def __init__(self, s1, s2, s3, in_tuples):

        Tuples = FiveTuple * len(in_tuples)
        tuples = Tuples()
        for i in range(len(in_tuples)):
            tuples[i] = in_tuples[i]


        # Needs separate list per depth as passing to c++ does not work well with c_uint32** 
        stage1_1 = Stage1()
        for i in range(len(stage1_1)):
            if i < len(s1[0]):
                stage1_1[i] = s1[0][i]
            else:
                stage1_1[i] = 0
        print("S1_1 done")

        stage1_2 = Stage1()
        for i in range(len(stage1_2)):
            if i < len(s1[1]):
                stage1_2[i] = s1[1][i]
            else:
                stage1_2[i] = 0
        print("S1_2 done")

        stage2_1 = Stage2()
        for i in range(len(stage2_1)):
            if i < len(s2[0]):
                stage2_1[i] = s2[0][i]
            else:
                stage2_1[i] = 0
        print("S2_1 done")
        
        stage2_2 = Stage2()
        for i in range(len(stage2_2)):
            if i < len(s2[1]):
                stage2_2[i] = s2[1][i]
            else:
                stage2_2[i] = 0
        print("S2_2 done")

        stage3_1 = Stage3()
        for i in range(len(stage3_1)):
            if i < len(s3[0]):
                stage3_1[i] = s3[0][i]
            else:
                stage3_1[i] = 0
        print("S3_1 done")
        
        stage3_2 = Stage3()
        for i in range(len(stage3_2)):
            if i < len(s3[1]):
                stage3_2[i] = s3[1][i]
            else:
                stage3_2[i] = 0
        print("S3_2 done")

        stage_sz = Stage_szes(SKETCH_W1, SKETCH_W2, SKETCH_W3)
        print("stage szes done")

        self.obj = c_void_p(lib.EMFSD_new(stage_sz, stage1_1, stage1_2, stage2_1, stage2_2, stage3_1, stage3_1, tuples, len(tuples)))

    def next_epoch(self):
        lib.EMFSD_next_epoch(self.obj)

print("Creating EM_FSD")
s1 = [ [ 255,2,3 ], [ 11,12,13 ] ]
s2 = [ [ 12,13,14 ], [ 12,13,14 ] ]
s3 = [ [ 103,104,105 ], [ 13,14,15 ] ]
test_tuple = [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
test_tuple2 = [ 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
t1 = FiveTuple(*test_tuple)
t2 = FiveTuple(*test_tuple2)
tuples = [ t1, t2]
f = EM_FSD(s1, s2, s3, tuples)
print("Finish init EM_FSD")
print("Start EM")
print(f)
f.next_epoch()
print("done!")
