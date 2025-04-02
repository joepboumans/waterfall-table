#ifndef _COMMON_H
#define _COMMON_H

/*#include "BOBHash32.h"*/
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glob.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <ostream>
#include <set>
#include <unordered_map>
#include <vector>
#include <zlib.h>

#define NUM_STAGES 3
#define DEPTH 2
#define K 8
#define W3 8192 // 32-bit, level 3
/*#define W3 2357          // 32-bit, level 3*/
#define W2 (K * W3)      // 16-bit, level 2
#define W1 (K * W2)      // 8-bit, level 1
#define ADD_LEVEL1 255   // 2^8 -2 + 1 (actual count is 254)
#define ADD_LEVEL2 65789 // (2^8 - 2) + (2^16 - 2) + 1 (actual count is 65788)
#define OVERFLOW_LEVEL1 254        // 2^8 - 1 maximum count is 254
#define OVERFLOW_LEVEL2 65534      // 2^16 - 1 maximum count is 65536
#define OVERFLOW_LEVEL3 4294967295 // 2^32 - 1 maximum count is 65536

#define WATERFALL_WIDTH 65536

using std::string;
using std::vector;

#define MAX_TUPLE_SZ 13
enum TupleSize : uint8_t { FiveTuple = 13, FlowTuple = 8, SrcTuple = 4 };

struct TUPLE {
  uint8_t num_array[MAX_TUPLE_SZ] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t sz = 13; // Sz of 13 is FiveTuple, sz of 8 is Src/Dst tuple and sz of
                   // 4 is only src ip

  friend std::ostream &operator<<(std::ostream &os, TUPLE const &tuple) {
    uint32_t srcPort = ((tuple.num_array[8] << 8) | tuple.num_array[9]);
    uint32_t dstPort = ((tuple.num_array[10] << 8) | tuple.num_array[11]);
    uint32_t protocol = tuple.num_array[12];
    char srcIp[16];
    sprintf(srcIp, "%i.%i.%i.%i", tuple.num_array[0], tuple.num_array[1],
            tuple.num_array[2], tuple.num_array[3]);
    char dstIp[16];
    sprintf(dstIp, "%i.%i.%i.%i", tuple.num_array[4], tuple.num_array[5],
            tuple.num_array[6], tuple.num_array[7]);

    if (tuple.sz == 4) {
      return os << srcIp << "\tsz " << int(tuple.sz);
    } else if (tuple.sz == 8) {
      return os << srcIp << "|" << dstIp << "\tsz " << int(tuple.sz);
    }
    return os << srcIp << ":" << srcPort << "|" << dstIp << ":" << dstPort
              << "|" << protocol << "\tsz " << int(tuple.sz);
  }

  operator string() {
    char ftuple[MAX_TUPLE_SZ];
    memcpy(ftuple, this->num_array, this->sz);
    return ftuple;
  }
  operator uint8_t *() { return this->num_array; }

  TUPLE() {}
  TUPLE(string s_tuple, uint8_t sz) {
    for (size_t i = 0; i < MAX_TUPLE_SZ; i++) {
      this->num_array[i] = reinterpret_cast<char>(s_tuple[i]);
    }
    this->sz = sz;
  }
  TUPLE(uint8_t *array_tuple, uint8_t sz) {
    for (size_t i = 0; i < MAX_TUPLE_SZ; i++) {
      this->num_array[i] = array_tuple[i];
    }
    this->sz = sz;
  }

  TUPLE &operator++() {
    for (size_t i = 0; i < this->sz; i++) {
      if (this->num_array[i] >= std::numeric_limits<unsigned char>::max()) {
        this->num_array[i]++;
        continue;
      }
      this->num_array[i]++;
      return *this;
    }
    return *this;
  }

  TUPLE operator++(int) {
    TUPLE old = *this;
    operator++();
    return old;
  }

  TUPLE operator+(int z) {
    for (size_t i = 0; i < this->sz; i++) {
      if (this->num_array[i] + z >= std::numeric_limits<unsigned char>::max()) {
        this->num_array[i] += z;
        continue;
      }
      this->num_array[i] += z;
      return *this;
    }
    return *this;
  }
  TUPLE operator+=(int z) { return *this + z; }

  TUPLE operator^=(TUPLE tup) {
    *this = *this ^ tup;
    return *this;
  }

  TUPLE operator^(TUPLE tup) {
    for (size_t i = 0; i < this->sz; i++) {
      this->num_array[i] ^= tup.num_array[i];
    }
    return *this;
  }

  /*auto operator<=>(const TUPLE &) const = default;*/
  bool operator==(const TUPLE &rhs) const {
    for (size_t i = 0; i < this->sz; i++) {
      if (this->num_array[i] != rhs.num_array[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator<(const TUPLE &rhs) const {
    int cmp = std::memcmp(num_array, rhs.num_array, sz);
    if (cmp != 0) {
      return cmp < 0;
    }
    return false;
  }
};
typedef vector<TUPLE> TRACE;

struct TupleHash {
  std::size_t operator()(const TUPLE &k) const {
    uint32_t crc = 0;
    return crc32(crc, k.num_array, k.sz);
  }
};

#endif
