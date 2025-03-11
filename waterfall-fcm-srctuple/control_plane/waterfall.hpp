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
  getTableList(std::string, uint32_t len);

private:
  std::vector<std::shared_ptr<const bfrt::BfRtTable>> mTablesVec;
  std::vector<std::shared_ptr<const bfrt::BfRtTable>> mSwapVec;
};

#endif // _WATERFALL_HPP
