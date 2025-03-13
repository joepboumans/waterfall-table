#ifndef _WATERFALL_HPP
#define _WATERFALL_HPP

#include "ControlPlane.hpp"
#include <bf_rt/bf_rt_table.hpp>
#include <cstdint>
#include <memory>

class Waterfall : ControlPlane {
public:
  Waterfall();
  void run();
  std::vector<std::shared_ptr<const bfrt::BfRtTable>>
      getTableList(std::vector<std::string>);

private:
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mTablesVec;
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mSwapVec;
  std::vector<std::vector<std::shared_ptr<const bfrt::BfRtTable>>> mSketchVec;
  std::shared_ptr<const bfrt::BfRtTable> mPktCount;

  uint32_t hashing(uint8_t *nums, size_t sz, uint32_t depth);
};

#endif // _WATERFALL_HPP
