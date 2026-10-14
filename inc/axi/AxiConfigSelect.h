// SPDX-License-Identifier: GPL-3.0-or-later
// AXI config for the CYCLE6_AXI build: matchlib's 64-bit axi::cfg::standard,
// as in CVA6, or a wider one from AxiConfigWide.h when CMake sets
// AXI_WIDTH_BITS (TIMING_MODEL=CYCLE6_AXI128/256/512/1024). It has to be a
// build option because every matchlib AXI type is a template on this config.
#pragma once
#ifndef AXI_CONFIG_SELECT_H
#define AXI_CONFIG_SELECT_H

#include <axi/axi4_configs.h>

#ifdef AXI_WIDTH_BITS
#include "axi/AxiConfigWide.h"
#endif

namespace riscv_axi {

#if AXI_WIDTH_BITS == 128
using AxiCfg = axi::cfg::wide128;
#elif AXI_WIDTH_BITS == 256
using AxiCfg = axi::cfg::wide256;
#elif AXI_WIDTH_BITS == 512
using AxiCfg = axi::cfg::wide512;
#elif AXI_WIDTH_BITS == 1024
using AxiCfg = axi::cfg::wide1024;
#else
using AxiCfg = axi::cfg::standard;
#endif

} // namespace riscv_axi

#endif // AXI_CONFIG_SELECT_H
