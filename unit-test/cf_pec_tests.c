/************************************************************************
 * NASA Docket No. GSC-18,447-1, and identified as “CFS CFDP (CF)
 * Application version 3.0.0”
 *
 * Copyright (c) 2019 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/* cf testing includes */
#include "cf_test_utils.h"
#include "cf_pec.h"

/*
 * The CRC-16-CCITT compliance vectors from ECSS-E-ST-70-41C Table B-2.
 * For each data field the standard lists the expected 16-bit CRC.
 */
void Test_CF_PEC_Calc_ComplianceVectors(void)
{
    uint8 v1[] = {0x00, 0x00};
    uint8 v2[] = {0x00, 0x00, 0x00};
    uint8 v3[] = {0xAB, 0xCD, 0xEF, 0x01};
    uint8 v4[] = {0x14, 0x56, 0xF8, 0x9A, 0x00, 0x01};

    UtAssert_UINT32_EQ(CF_PEC_Calc(v1, sizeof(v1)), 0x1D0F);
    UtAssert_UINT32_EQ(CF_PEC_Calc(v2, sizeof(v2)), 0xCC9C);
    UtAssert_UINT32_EQ(CF_PEC_Calc(v3, sizeof(v3)), 0x04A2);
    UtAssert_UINT32_EQ(CF_PEC_Calc(v4, sizeof(v4)), 0x7FD5);
}

/*
 * Empty input pre-sets the syndrome to all-ones and never iterates.
 */
void Test_CF_PEC_Calc_Empty(void)
{
    UtAssert_UINT32_EQ(CF_PEC_Calc(NULL, 0), 0xFFFF);
}

/*
 * The ECSS B.1.6 decode procedure: running the CRC over the data field with
 * the computed CRC appended (high byte first) yields a zero syndrome, while a
 * corrupted octet yields a non-zero syndrome.
 */
void Test_CF_PEC_Calc_SyndromeProperty(void)
{
    uint8  buf[8] = {0xAB, 0xCD, 0xEF, 0x01};
    size_t n      = 4;
    uint16 crc    = CF_PEC_Calc(buf, n);

    buf[n]     = (uint8)((crc >> 8) & 0xFF);
    buf[n + 1] = (uint8)(crc & 0xFF);

    /* Error-free packet (data + PEC) -> zero syndrome */
    UtAssert_UINT32_EQ(CF_PEC_Calc(buf, n + CF_PEC_SIZE_BYTES), 0);

    /* Flip a data bit -> non-zero syndrome */
    buf[0] ^= 0x01;
    UtAssert_True(CF_PEC_Calc(buf, n + CF_PEC_SIZE_BYTES) != 0, "Corrupted packet yields non-zero syndrome");
}

void UtTest_Setup(void)
{
    TEST_CF_ADD(Test_CF_PEC_Calc_ComplianceVectors);
    TEST_CF_ADD(Test_CF_PEC_Calc_Empty);
    TEST_CF_ADD(Test_CF_PEC_Calc_SyndromeProperty);
}
