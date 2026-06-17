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

/**
 * @file
 *
 * CCSDS Packet Error Control (PEC) field CRC for CF space packets.
 *
 * Transcribed from the unoptimized reference implementation in
 * ECSS-E-ST-70-41C Annex B.1.6 (CRC-16-CCITT).  The unoptimized variant is
 * used deliberately: CFDP PDUs carried here are small and infrequent, so the
 * table-optimized variant's speedup is not worth a global lookup table.
 */

#include "cf_pec.h"

/*----------------------------------------------------------------
 *
 * Compute the new CRC syndrome for a single byte (ECSS B.1.6 "Crc").
 *
 *-----------------------------------------------------------------*/
static uint16 CF_PEC_Step(uint8 data, uint16 syndrome)
{
    uint8 i;

    for (i = 0; i < 8; ++i)
    {
        if ((data & 0x80) ^ ((syndrome & 0x8000) >> 8))
        {
            syndrome = (uint16)(((syndrome << 1) ^ 0x1021) & 0xFFFF);
        }
        else
        {
            syndrome = (uint16)((syndrome << 1) & 0xFFFF);
        }
        data = (uint8)(data << 1);
    }

    return syndrome;
}

/*----------------------------------------------------------------
 *
 * Application-scope function
 * See description in cf_pec.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
uint16 CF_PEC_Calc(const uint8 *buf, size_t octets)
{
    uint16 chk = 0xFFFF; /* Reset syndrome to all ones */
    size_t i;

    for (i = 0; i < octets; ++i)
    {
        chk = CF_PEC_Step(buf[i], chk);
    }

    return chk;
}
