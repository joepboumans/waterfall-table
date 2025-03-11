set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} $ENV{SDE}/cmake)
set(CMAKE_PROGRAM_PATH ${CMAKE_PROGRAM_PATH} $ENV{SDE_INSTALL}/bin)
set(CMAKE_INSTALL_PREFIX=$ENV{SDE_INSTALL})

set(TOFINO ON CACHE BOOL "Enable Tofino as default" FORCE)
set(P4_LANG "p4-16")

set(PYTHON_EXECUTABLE python3)
set(CMAKE_BUILD_TYPE "RelWithDebInfo")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/lib")
#set(THRIFT-DRIVER ON)
option(CPUVETH       "Use CPU eth port, applicable for model" ON)
option(GRPC          "Drivers: Build with support for GRPC/protobuf" ON)
option(BFRT          "Drivers: Build with BFRuntime support" ON)
option(BFRT-GENERIC-FLAGS "Drivers: Build BF-RT C API with generic flags support" ON)

# p4flags will be appended to internal compiler options
if(NOT P4FLAGS)
set(P4FLAGS "" CACHE STRING "" FORCE)
else()
message(STATUS "\nP4FLAGS: ${P4FLAGS}")
endif()

# p4ppflags will be appended to internal compiler options
if(NOT P4PPFLAGS)
set(P4PPFLAGS "" CACHE STRING "" FORCE)
else()
message(STATUS "\nP4PPFLAGS: ${P4PPFLAGS}")
endif()

find_library(DRIVER driver)
#find_package(Thrift REQUIRED)

# dummy targets to satisfy dependencies
add_custom_target(bf-p4c)
add_custom_target(driver)

find_program(P4C bf-p4c)
find_program(P4C-GEN-BFRT-CONF p4c-gen-bfrt-conf)
find_program(P4C-MANIFEST-CONFIG p4c-manifest-config)
find_program(PDGEN generate_tofino_pd)
find_program(PDGENCLI gencli)
find_program(PDSPLIT split_pd_thrift.py)
  
#include($ENV{SDE}/cmake/P4Build.cmake)
option (PSA "Build for PSA architecture" OFF)

add_definitions(-DTOFINO)
set(P4_tofino_ARCHITECTURE "tna")

function(P4_BUILD_TARGET t arch target p4program)
  set(input_rt_list ${ARGV4})
  # set output files/ commands  based upon whether bfrt and/or p4rt is needed
  set(output_files "")
  set(rt_commands "")
  set(depends_target "")
  set(chiptype ${target})
  if (${target} STREQUAL "tofino2m")
    set(chiptype "tofino2")
  else()
    set(chiptype ${target})
  endif()

  if ("${input_rt_list}" STREQUAL "")
    set(input_rt_list "bfrt")
  endif()
  foreach(rt in lists input_rt_list)
    if (${rt} STREQUAL "bfrt")
      set(output_files "${output_files}" "${t}/${target}/bf-rt.json")
      set(rt_commands "${rt_commands}" "--bf-rt-schema" "${t}/${target}/bf-rt.json")
      set(depends_target "${depends_target}" "${t}/${target}/bf-rt.json")
    elseif (${rt} STREQUAL "p4rt")
      set(output_files "${output_files}" "${t}/${target}/p4info.pb.txt")
      set(rt_commands "${rt_commands}" "--p4runtime-files" "${t}/${target}/p4info.pb.txt")
      set(depends_target "${depends_target}" "${t}/${target}/p4info.pb.txt")
    endif()
  endforeach()
  
  set(install_dir ${CMAKE_CURRENT_BINARY_DIR}/${t}/${target})

  separate_arguments(COMPUTED_P4FLAGS UNIX_COMMAND ${P4FLAGS})
  separate_arguments(COMPUTED_P4PPFLAGS UNIX_COMMAND ${P4PPFLAGS})
  # compile the p4 program
  add_custom_command(OUTPUT ${output_files}
    COMMAND ${P4C} --std ${P4_LANG} --target ${target} --arch ${arch} ${rt_commands} -o ${CMAKE_CURRENT_BINARY_DIR}/${t}/${target} ${COMPUTED_P4PPFLAGS} ${COMPUTED_P4FLAGS} ${P4FLAGS_INTERNAL} -g ${p4program}
    COMMAND ${P4C-GEN-BFRT-CONF} --name ${t} --device ${chiptype} --testdir ./${t}/${target}
         --installdir ${install_dir} --pipe `${P4C-MANIFEST-CONFIG} --pipe ./${t}/${target}/manifest.json`
    DEPENDS ${p4program} bf-p4c
  )
   add_custom_target(${t}-${target} DEPENDS ${depends_target} driver)
  # install generated conf file
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${t}/${target}/ DESTINATION share/p4/targets/${target}
    FILES_MATCHING
    PATTERN "*.conf"
    PATTERN "pipe" EXCLUDE
    PATTERN "logs" EXCLUDE
    PATTERN "graphs" EXCLUDE
  )
  # install bf-rt.json, context.json and tofino.bin
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${t}/${target}/ DESTINATION share/${target}pd/${t}
    FILES_MATCHING
    PATTERN "*.json"
    PATTERN "*.bin"
    PATTERN "*.pb.txt"
    PATTERN "*manifest*" EXCLUDE
    PATTERN "logs" EXCLUDE
    PATTERN "graphs" EXCLUDE
    PATTERN "*dynhash*" EXCLUDE
    PATTERN "*prim*" EXCLUDE
  )
endfunction()


p4_build_target("waterfall-fcm" "tna" "tofino" ${CMAKE_CURRENT_SOURCE_DIR}/waterfall_fcm.p4)
add_custom_target(waterfall-fcm DEPENDS
    waterfall-fcm-tofino
)
