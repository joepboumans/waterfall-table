#ifndef _WATERFALL_HPP
#define _WATERFALL_HPP

#include "ControlPlane.hpp"
#include "common.h"
#include <bf_rt/bf_rt_table.hpp>
#include <cstdint>
#include <memory>

class Waterfall : ControlPlane {
public:
  Waterfall(TupleSize sz);
  void run();
  void collectFromDataPlane();
  void verify(vector<TUPLE> inTuples);

private:
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mTablesVec;
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mSwapVec;
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mSketchVec;
  std::shared_ptr<const bfrt::BfRtTable> mPktCount;
  std::vector<std::shared_ptr<const bfrt::BfRtTable>>
      getTableList(std::vector<std::string>);

  std::set<TUPLE> mUniqueTuples;
  std::set<TUPLE> mUniqueInTuples;
  std::unordered_map<TUPLE, uint32_t, TupleHash> mTrueCounts;
  vector<uint32_t> mTrueFSD;
  std::vector<std::vector<std::vector<uint32_t>>> mSketchData;
  TupleSize mTupleSz;

  uint32_t hashing(const uint8_t *nums, size_t sz, uint32_t depth);
  vector<vector<uint32_t>> getInitialDegrees();
  void calculateFSD();
  vector<double> mEstFSD;
  uint32_t mItersEM = 5;
  void calculateWMRE(std::vector<double> &ns);
  double mWMRE = 0.0;
  void calculateEntropy(std::vector<double> &ns);
  double mEntropy = 0.0;
};

#endif // _WATERFALL_HPP
