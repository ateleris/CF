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
 * Implements the 16-bit Packet Error Control field (CRC-16-CCITT) per
 * ECSS-E-ST-70-41C Annex B.1.6.  Generator polynomial G(x) = x^16 + x^12 +
 * x^5 + 1 (0x1021), syndrome pre-set to all-ones (0xFFFF), processed MSB
 * first with no reflection and no final XOR.
 *
 * The MCS ground segment requires this trailing CRC on the CCSDS space
 * packets that carry CFDP PDUs (both directions).
 */

#ifndef CF_PEC_H
#define CF_PEC_H

#include "cfe.h"

/**
 * @brief Number of octets in a CCSDS Packet Error Control field.
 */
#define CF_PEC_SIZE_BYTES 2

/************************************************************************/
/** @brief Compute the ECSS-E-ST-70-41C CRC-16-CCITT over a buffer.
 *
 * @par Description
 *       Direct transcription of the unoptimized reference implementation in
 *       ECSS-E-ST-70-41C Annex B.1.6 (the "Crc"/"crc_encode" core loop).  The
 *       syndrome is pre-set to 0xFFFF and each byte is processed MSB first
 *       against polynomial 0x1021.
 *
 *       To generate a PEC: call over the packet data only (excluding the two
 *       PEC octets); the returned value is the CRC, high byte first.
 *
 *       To verify a PEC: call over the entire packet INCLUDING the two PEC
 *       octets; a return value of 0 means no error was detected (per the
 *       Annex B.1.6 "crc_decode" syndrome property).
 *
 * @par Assumptions, External Events, and Notes:
 *       buf must not be NULL when octets is non-zero.
 *
 * @param buf    Pointer to the octets to digest
 * @param octets Number of octets to digest
 *
 * @returns The 16-bit CRC syndrome
 */
uint16 CF_PEC_Calc(const uint8 *buf, size_t octets);

#endif /* !CF_PEC_H */
