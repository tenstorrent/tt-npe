#pragma once

#include "npeCommon.hpp"
#include "npeDeviceModel.hpp"

namespace tt_npe {

namespace wormhole_b0 {

// clang-format off

constexpr size_t NUM_COLS = 10;
constexpr size_t NUM_ROWS = 12;
constexpr float LINK_BANDWIDTH = 30;

const TransferBandwidthTable TRANSFER_BW_TABLE = {
    {   0,    0}, 
    { 128,  5.5}, 
    { 256, 10.1}, 
    { 512, 18.0},  
    {1024, 27.4}, 
    {2048, 30.0}, 
    {8192, 30.0}};

const CoreTypeToInjectionRate CORE_TYPE_TO_INJ_RATE = {
    {CoreType::DRAM, 23.2},
    {CoreType::ETH, 23.2},
    {CoreType::UNDEF, 28.1},
    {CoreType::WORKER, 28.1},
}; 

const CoordToTypeMapping CORE_TO_TYPE_MAP = { 
    {{0,0},{CoreType::DRAM}},
    {{0,1},{CoreType::ETH}},
    {{0,2},{CoreType::ETH}},
    {{0,3},{CoreType::ETH}},
    {{0,4},{CoreType::ETH}},
    {{0,5},{CoreType::DRAM}},
    {{0,6},{CoreType::ETH}},
    {{0,7},{CoreType::ETH}},
    {{0,8},{CoreType::ETH}},
    {{0,9},{CoreType::ETH}},

    {{1,0},{CoreType::DRAM}},
    {{1,1},{CoreType::WORKER}},
    {{1,2},{CoreType::WORKER}},
    {{1,3},{CoreType::WORKER}},
    {{1,4},{CoreType::WORKER}},
    {{1,5},{CoreType::DRAM}},
    {{1,6},{CoreType::WORKER}},
    {{1,7},{CoreType::WORKER}},
    {{1,8},{CoreType::WORKER}},
    {{1,9},{CoreType::WORKER}},

    {{2,0},{CoreType::UNDEF}},
    {{2,1},{CoreType::WORKER}},
    {{2,2},{CoreType::WORKER}},
    {{2,3},{CoreType::WORKER}},
    {{2,4},{CoreType::WORKER}},
    {{2,5},{CoreType::DRAM}},
    {{2,6},{CoreType::WORKER}},
    {{2,7},{CoreType::WORKER}},
    {{2,8},{CoreType::WORKER}},
    {{2,9},{CoreType::WORKER}},

    {{3,0},{CoreType::UNDEF}},
    {{3,1},{CoreType::WORKER}},
    {{3,2},{CoreType::WORKER}},
    {{3,3},{CoreType::WORKER}},
    {{3,4},{CoreType::WORKER}},
    {{3,5},{CoreType::DRAM}},
    {{3,6},{CoreType::WORKER}},
    {{3,7},{CoreType::WORKER}},
    {{3,8},{CoreType::WORKER}},
    {{3,9},{CoreType::WORKER}},

    {{4,0},{CoreType::UNDEF}},
    {{4,1},{CoreType::WORKER}},
    {{4,2},{CoreType::WORKER}},
    {{4,3},{CoreType::WORKER}},
    {{4,4},{CoreType::WORKER}},
    {{4,5},{CoreType::DRAM}},
    {{4,6},{CoreType::WORKER}},
    {{4,7},{CoreType::WORKER}},
    {{4,8},{CoreType::WORKER}},
    {{4,9},{CoreType::WORKER}},

    {{5,0},{CoreType::DRAM}},
    {{5,1},{CoreType::WORKER}},
    {{5,2},{CoreType::WORKER}},
    {{5,3},{CoreType::WORKER}},
    {{5,4},{CoreType::WORKER}},
    {{5,5},{CoreType::DRAM}},
    {{5,6},{CoreType::WORKER}},
    {{5,7},{CoreType::WORKER}},
    {{5,8},{CoreType::WORKER}},
    {{5,9},{CoreType::WORKER}},

    {{6,0},{CoreType::DRAM}},
    {{6,1},{CoreType::ETH}},
    {{6,2},{CoreType::ETH}},
    {{6,3},{CoreType::ETH}},
    {{6,4},{CoreType::ETH}},
    {{6,5},{CoreType::DRAM}},
    {{6,6},{CoreType::ETH}},
    {{6,7},{CoreType::ETH}},
    {{6,8},{CoreType::ETH}},
    {{6,9},{CoreType::ETH}},

    {{7,0},{CoreType::DRAM}},
    {{7,1},{CoreType::WORKER}},
    {{7,2},{CoreType::WORKER}},
    {{7,3},{CoreType::WORKER}},
    {{7,4},{CoreType::WORKER}},
    {{7,5},{CoreType::DRAM}},
    {{7,6},{CoreType::WORKER}},
    {{7,7},{CoreType::WORKER}},
    {{7,8},{CoreType::WORKER}},
    {{7,9},{CoreType::WORKER}},

    {{8,0},{CoreType::UNDEF}},
    {{8,1},{CoreType::WORKER}},
    {{8,2},{CoreType::WORKER}},
    {{8,3},{CoreType::WORKER}},
    {{8,4},{CoreType::WORKER}},
    {{8,5},{CoreType::DRAM}},
    {{8,6},{CoreType::WORKER}},
    {{8,7},{CoreType::WORKER}},
    {{8,8},{CoreType::WORKER}},
    {{8,9},{CoreType::WORKER}},

    {{9,0},{CoreType::UNDEF}},
    {{9,1},{CoreType::WORKER}},
    {{9,2},{CoreType::WORKER}},
    {{9,3},{CoreType::WORKER}},
    {{9,4},{CoreType::WORKER}},
    {{9,5},{CoreType::DRAM}},
    {{9,6},{CoreType::WORKER}},
    {{9,7},{CoreType::WORKER}},
    {{9,8},{CoreType::WORKER}},
    {{9,9},{CoreType::WORKER}},

    {{10,0},{CoreType::UNDEF}},
    {{10,1},{CoreType::WORKER}},
    {{10,2},{CoreType::WORKER}},
    {{10,3},{CoreType::WORKER}},
    {{10,4},{CoreType::WORKER}},
    {{10,5},{CoreType::DRAM}},
    {{10,6},{CoreType::WORKER}},
    {{10,7},{CoreType::WORKER}},
    {{10,8},{CoreType::WORKER}},
    {{10,9},{CoreType::WORKER}},

    {{11,0},{CoreType::DRAM}},
    {{11,1},{CoreType::WORKER}},
    {{11,2},{CoreType::WORKER}},
    {{11,3},{CoreType::WORKER}},
    {{11,4},{CoreType::WORKER}},
    {{11,5},{CoreType::DRAM}},
    {{11,6},{CoreType::WORKER}},
    {{11,7},{CoreType::WORKER}},
    {{11,8},{CoreType::WORKER}},
    {{11,9},{CoreType::WORKER}},
   };
}
// clang-format on
}  // namespace tt_npe
