#include "ControlPlane.hpp"
#include <filesystem>
#include <iostream>

using namespace std;
using namespace bfrt;

bf_status_t
handleLearnCallback(const bf_rt_target_t &bf_rt_tgt,
                    const std::shared_ptr<bfrt::BfRtSession> session,
                    std::vector<unique_ptr<bfrt::BfRtLearnData>> learnDataVec,
                    bf_rt_learn_msg_hdl *const learn_msg_hdl,
                    const void *cookie) {

  learnInterface *cpLearnInterface = (learnInterface *)cookie;
  uint64_t val;
  bf_rt_id_t field = 1;

  for (auto &data : learnDataVec) {
    data->getValue(1, &val);
    cpLearnInterface->mLearnDataVec.push_back(val);
    /*data->getValue(2, &val);*/
    /*cpLearnInterface->mLearnDataVec.push_back(val);*/
    /*data->getValue(3, &val);*/
    /*cpLearnInterface->mLearnDataVec.push_back(val);*/
    /*std::cout << "Data val " << val << std::endl;*/
    /*std::cout << "Msg hdl " << learn_msg_hdl << std::endl;*/
  }
  cpLearnInterface->hasNewData = true;

  bf_status_t bf_status =
      cpLearnInterface->mLearn->bfRtLearnNotifyAck(session, learn_msg_hdl);
  if (bf_status != BF_SUCCESS) {
    printf("Notifying ack failed with %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to ack learn filter");
  }
  return BF_SUCCESS;
}

ControlPlane::ControlPlane(string programName) {
  /* Allocate memory for the libbf_switchd context. */
  mSwitchContext =
      (bf_switchd_context_t *)calloc(1, sizeof(bf_switchd_context_t));
  if (!mSwitchContext) {
    throw runtime_error("Cannot allocate switchd context");
  }

  /* Always set "background" because we do not want bf_switchd_lib_init to start
   * a CLI session.  That can be done afterward by the caller if requested
   * through command line options. */
  mSwitchContext->running_in_background = true;

  /* Always set "skip port add" so that ports are not automatically created when
   * running on either model or HW. */
  mSwitchContext->skip_port_add = true;
  mSwitchContext->install_dir = getenv("SDE_INSTALL");

  // Set separately to keek pointer intact
  string confPath = filesystem::current_path().string() + string("/build/p4/") +
                    programName + string("/tofino/") + programName +
                    string(".conf");
  mSwitchContext->conf_file = confPath.data();
  mSwitchContext->dev_sts_thread = true;
  mSwitchContext->dev_sts_port = 7777;

  /* Initialize libbf_switchd. */
  bf_status_t bf_status = bf_switchd_lib_init(mSwitchContext);
  if (bf_status != BF_SUCCESS) {
    free(mSwitchContext);
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to initialize libbf_switchd");
  }

  mDeviceTarget.dev_id = 0;
  mDeviceTarget.pipe_id = 0xFFFF; // 0xFFFF = All pipes

  // Get devMgr singleton instance
  const auto &devMgr = BfRtDevMgr::getInstance();
  printf("Initialized BfRtDevMgr instance\n");

  // Key field ids, table data field ids, action ids, Table object required for
  // interacting with the table
  // Get bfrtInfo object from dev_id and p4 program name
  const BfRtInfo *infoPtr = nullptr;
  bf_status = devMgr.bfRtInfoGet(mDeviceTarget.dev_id, programName, &infoPtr);
  mInfo = shared_ptr<const BfRtInfo>(infoPtr);
  // Check for status
  if (bf_status != BF_SUCCESS) {
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to get BfRt info");
  }
  printf("Retrieved BfRtInfo\n");
  // Create a session object
  mSession = BfRtSession::sessionCreate();
  printf("Created BfRt session\n");
}

ControlPlane::~ControlPlane() {
  printf("Destroying session\n");
  mSession->sessionDestroy();
  printf("Destroyed session\n");
  free(mSwitchContext);
  printf("Freed switch context memory\n");
}

shared_ptr<const BfRtTable> ControlPlane::getTable(string name) {
  const BfRtTable *tablePtr = nullptr;
  bf_status_t bf_status = mInfo->bfrtTableFromNameGet(name, &tablePtr);
  if (bf_status != BF_SUCCESS) {
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to get table");
  }
  return shared_ptr<const BfRtTable>(tablePtr);
}

shared_ptr<const BfRtLearn> ControlPlane::getLearnFilter(string name) {
  const BfRtLearn *learnPtr = nullptr;
  bf_status_t bf_status = mInfo->bfrtLearnFromNameGet(name, &learnPtr);
  if (bf_status != BF_SUCCESS) {
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to get learn filter or digest");
  }

  std::vector<bf_rt_id_t> list;
  bf_status = learnPtr->learnFieldIdListGet(&list);
  if (bf_status != BF_SUCCESS) {
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to get learn filter id list");
  }

  for (auto id : list) {
    std::string name;
    bf_status = learnPtr->learnFieldNameGet(id, &name);
    if (bf_status != BF_SUCCESS) {
      printf("Error: %s\n", bf_err_str(bf_status));
      throw runtime_error("Failed to get name of learnFieldId");
    }
    std::cout << name << " : " << id << " ";
  }
  std::cout << std::endl;
  // Average data set has 35 milion packets so 50M should always fit
  mLearnInterface.mLearnDataVec.reserve(40000000);

  bfrt::bfRtCbFunction cbFunc = handleLearnCallback;
  mLearnInterface.mLearn = std::shared_ptr<const bfrt::BfRtLearn>(learnPtr);

  bf_status = learnPtr->bfRtLearnCallbackRegister(
      mSession, mDeviceTarget, cbFunc, (void *)&mLearnInterface);

  if (bf_status != BF_SUCCESS) {
    printf("Error: %s\n", bf_err_str(bf_status));
    throw runtime_error("Failed to setup Learn callback");
  }
  return shared_ptr<const BfRtLearn>(learnPtr);
}

shared_ptr<BfRtSession> ControlPlane::getSession() { return mSession; }

bf_rt_target_t ControlPlane::getDeviceTarget() { return mDeviceTarget; }

void ControlPlane::addEntry(shared_ptr<const BfRtTable> table,
                            vector<pair<string, uint64_t>> keys,
                            vector<pair<string, uint64_t>> data,
                            string action) {
  unique_ptr<BfRtTableKey> tableKey;
  table->keyAllocate(&tableKey);
  for (const auto [keyName, keyValue] : keys) {
    bf_rt_id_t fieldId;
    table->keyFieldIdGet(keyName, &fieldId);
    tableKey->setValue(fieldId, keyValue);
  }

  unique_ptr<BfRtTableData> tableData;
  bf_rt_id_t actionId;
  if (action != "") {
    table->actionIdGet(action, &actionId);
    table->dataAllocate(actionId, &tableData);
  } else {
    table->dataAllocate(&tableData);
  }

  for (const auto [dataName, dataValue] : data) {
    bf_rt_id_t fieldId;
    if (action != "") {
      table->dataFieldIdGet(dataName, actionId, &fieldId);
    } else {
      table->dataFieldIdGet(dataName, &fieldId);
    }
    tableData->setValue(fieldId, dataValue);
  }

  const uint64_t flags = 0;
  table->tableEntryAdd(*mSession, mDeviceTarget, flags, *tableKey, *tableData);
}

unordered_map<string, uint64_t>
ControlPlane::getEntry(shared_ptr<const BfRtTable> table,
                       vector<pair<string, uint64_t>> keys, string action) {
  unique_ptr<BfRtTableKey> tableKey;
  table->keyAllocate(&tableKey);
  for (const auto [keyName, keyValue] : keys) {
    bf_rt_id_t fieldId;
    table->keyFieldIdGet(keyName, &fieldId);
    tableKey->setValue(fieldId, keyValue);
  }

  unique_ptr<BfRtTableData> tableData;
  table->dataAllocate(&tableData);
  uint64_t getFlags = 0;
  BF_RT_FLAG_SET(getFlags, BF_RT_FROM_HW);
  table->tableEntryGet(*mSession, mDeviceTarget, getFlags, *tableKey,
                       tableData.get());

  std::vector<bf_rt_id_t> dataFieldIds;
  bf_rt_id_t actionId;
  if (action != "") {
    table->actionIdGet(action, &actionId);
    table->dataFieldIdListGet(actionId, &dataFieldIds);
  } else {
    table->dataFieldIdListGet(&dataFieldIds);
  }
  unordered_map<string, uint64_t> obtainedValues;
  for (const auto id : dataFieldIds) {
    bool isActive = false;
    tableData->isActive(id, &isActive);
    if (!isActive) {
      continue;
    }

    string fieldName = string();
    if (action != "") {
      table->dataFieldNameGet(id, actionId, &fieldName);
    } else {
      table->dataFieldNameGet(id, &fieldName);
    }
    if (fieldName == "") {
      continue;
    }

    uint64_t fieldValue;
    tableData->getValue(id, &fieldValue);
    obtainedValues.insert({fieldName, fieldValue});
  }

  return obtainedValues;
}
