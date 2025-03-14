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
  Waterfall(TupleSize sz);
  void run();
  void collectFromDataPlane();
  void verify(vector<TUPLE> inTuples);
  void setupLogging(std::string &datasetName);

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
  double mF1 = 0.0;
  double mAverageRelativeError = 0.0;
  double mAverageAbsoluteError = 0.0;
  double mF1HeavyHitter = 0.0;
  void calculateFSD();
  vector<double> mEstFSD;
  uint32_t mItersEM = 5;
  void calculateWMRE(std::vector<double> &ns);
  double mWMRE = 0.0;
  void calculateEntropy(std::vector<double> &ns);
  double mEntropy = 0.0;

  // Logging members
  char mFilePathOverall[400];
  char mFilePathEst[400];
  char mFilePathNs[400];
  std::ofstream mFileOverall;
  std::ofstream mFileEst;
  std::ofstream mFileNs;
  string mHeaderFileOverall;
  string mHeaderEst;
  string mHeaderNs;
  void writeResOverall();
  void writeResEst(uint32_t iter, size_t time, size_t totalTime, double card);
  void writeResNs(vector<double> &ns);
};

#endif // _WATERFALL_HPP
