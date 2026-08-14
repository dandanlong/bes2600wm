# ##############################################################################
# vendor/bes/boards/***/***/configs/***/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

# set target var:
set(CONFIG_ARCH_BOARD "nx_bestbsp_dsp")

if(CONFIG_BOOT_RUNFROMFLASH)
  set(LD_SCRIPT_FILE _armca_flash.lds)
elseif(CONFIG_BOOT_RUNFROMISRAM)
  set(LD_SCRIPT_FILE _armca_psram.lds)
endif()

set(BES_SDK_BUILD_DIR ${NUTTX_DIR}/../vendor/bes/${CONFIG_BES_DDK_DIR})

list(
  APPEND
  BES_SDK_INCDIR
  ${BES_SDK_BUILD_DIR}/platform/hal
  ${BES_SDK_BUILD_DIR}/platform
  ${BES_SDK_BUILD_DIR}/platform/cmsis/inc
  ${BES_SDK_BUILD_DIR}/services/multimedia
  ${BES_SDK_BUILD_DIR}/services/multimedia/audio/process)

set_property(
  TARGET BES_SDK_TARGET
  APPEND
  PROPERTY CONFIG_ARCH_BOARD ${CONFIG_ARCH_BOARD})

set_property(
  TARGET BES_SDK_TARGET
  APPEND
  PROPERTY LD_SCRIPT_FILE ${LD_SCRIPT_FILE})

set(OUTBIN_NAME "nuttx_a7")

set(TEST_OUTBIN_WITHOUT_BOOTLOADER "n")

if(TEST_OUTBIN_WITHOUT_BOOTLOADER STREQUAL "n")
  if(NOT CONFIG_BOOT_RUNFROMFLASH)
    list(APPEND OBJCOPYARGS -R .boot_loader -R .boot_loader_data -R .boot_loader_bss)
  endif()

  if(CONFIG_DSP_USE_PSRAM)
    list(APPEND OBJCOPYARGS -R .psram_text -R .psram_data)
  endif()

  list(APPEND OBJCOPYARGS -R .boot_struct -R .build_info -R .code_start_addr)
endif()


set(OBJCOPY "arm-none-eabi-objcopy")

# POSTBUILD

if(CONFIG_DSP_IMAGE_COMP)

  add_custom_target(
    nuttx_post_build
    DEPENDS nuttx
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating: ${OUTBIN_NAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E copy nuttx ${OUTBIN_NAME}.elf
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.map ${OUTBIN_NAME}.map
    COMMAND ${OBJCOPY} ${OBJCOPYARGS} -O binary nuttx nuttx.bin
    COMMAND ${OBJCOPY} -j .boot_loader -j .boot_loader_data -O binary ${OUTBIN_NAME}.elf _bl.bin
    COMMAND ${OBJCOPY} -j .psram_text -j .psram_data -O binary ${OUTBIN_NAME}.elf _psram_xip.bin
    COMMAND ${OBJCOPY} -j .build_info -j .code_start_addr -O binary ${OUTBIN_NAME}.elf _suffix.bin
    COMMAND ${OBJCOPY} -j .boot_struct -O binary ${OUTBIN_NAME}.elf _prefix.bin
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh _bl.bin _bl.bin.tmp 1 0
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh nuttx.bin nuttx.bin.tmp 1 1
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh _psram_xip.bin _psram_xip.bin.tmp 1 1
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_postbuild.sh ${CMAKE_BINARY_DIR} ${OUTBIN_NAME} _bl.bin.lzma)

elseif(CONFIG_BOOT_RUNFROMFLASH)

  add_custom_target(
    nuttx_post_build
    DEPENDS nuttx
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating: ${OUTBIN_NAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E copy nuttx ${OUTBIN_NAME}.elf
    COMMAND ${OBJCOPY} ${OBJCOPYARGS} -O binary nuttx nuttx.bin
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.map ${OUTBIN_NAME}.map
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.bin ${OUTBIN_NAME}.bin
    COMMAND ${OBJCOPY} -j .psram_text -j .psram_data -O binary ${OUTBIN_NAME}.elf ${OUTBIN_NAME}_psram_xip_tmp.bin
    COMMAND stat -c %s ${OUTBIN_NAME}_psram_xip_tmp.bin | tr -d '\\n' | xargs -i printf "%08x" {} | tac -rs .. | xxd -r -ps > tmp.bin
    COMMAND cat tmp.bin ${OUTBIN_NAME}_psram_xip_tmp.bin > ${OUTBIN_NAME}_psram_xip.bin
    COMMAND rm tmp.bin ${OUTBIN_NAME}_psram_xip_tmp.bin)

elseif(TEST_OUTBIN_WITHOUT_BOOTLOADER)

  add_custom_target(
    nuttx_post_build
    DEPENDS nuttx
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating: ${OUTBIN_NAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E copy nuttx ${OUTBIN_NAME}.elf
    COMMAND ${OBJCOPY} ${OBJCOPYARGS} -O binary nuttx nuttx.bin
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.map ${OUTBIN_NAME}.map
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.bin ${OUTBIN_NAME}.bin)

else()

  add_custom_target(
    nuttx_post_build
    DEPENDS nuttx
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating: ${OUTBIN_NAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E copy nuttx ${OUTBIN_NAME}.elf
    COMMAND ${CMAKE_COMMAND} -E copy nuttx.map ${OUTBIN_NAME}.map
    COMMAND ${OBJCOPY} ${OBJCOPYARGS} -O binary nuttx nuttx.bin
    COMMAND ${OBJCOPY} -j .boot_loader -j .boot_loader_data -O binary ${OUTBIN_NAME}.elf _bl.bin
    COMMAND ${OBJCOPY} -j .psram_text -j .psram_data -O binary ${OUTBIN_NAME}.elf _psram_xip.bin
    COMMAND ${OBJCOPY} -j .build_info -j .code_start_addr -O binary ${OUTBIN_NAME}.elf _suffix.bin
    COMMAND ${OBJCOPY} -j .boot_struct -O binary ${OUTBIN_NAME}.elf _prefix.bin
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh _bl.bin _bl.bin.tmp 0 0
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh nuttx.bin nuttx.bin.tmp 0 1
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_partition.sh _psram_xip.bin _psram_xip.bin.tmp 0 1
    COMMAND ${BES_SDK_BUILD_DIR}/tools/a7_bin_postbuild.sh ${CMAKE_BINARY_DIR} ${OUTBIN_NAME} _bl.bin)

endif()
