#ifndef _WATERFALL_HPP
#define _WATERFALL_HPP

#include "ControlPlane.hpp"
#include "common.h"
#include <bf_rt/bf_rt_table.hpp>
#include <cstdint>
#include <fstream>
#include <memory>

class Waterfall : ControlPlane {
public:
  Waterfall(TupleSize sz, bool real);
  void run();
  void collectFromDataPlane();
  void verify(vector<TUPLE> inTuples);
  void setupLogging(std::string &datasetName);

private:
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mTablesVec;
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mSwapVec;
  std::vector<std::shared_ptr<const bfrt::BfRtTable>>
      getTableList(std::vector<std::string>);

  std::set<TUPLE> mUniqueTuples;
  std::set<TUPLE> mUniqueInTuples;
  TupleSize mTupleSz;

  uint32_t hashing(const uint8_t *nums, size_t sz, uint32_t depth);
  double mF1 = 0.0;

  // Logging members
  char mResultPath[400];
  std::ofstream mResultFile;
  string mHeaderResult;
  void writeResult();
};

#endif // _WATERFALL_HPP
