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
 * This is the interface to the CFE Software Bus for CF transmit/recv.
 * Specifically this implements 3 functions used by the CFDP engine:
 *  - CF_CFDP_MsgOutGet() - gets a buffer prior to transmitting
 *  - CF_CFDP_Send() - sends the buffer from CF_CFDP_MsgOutGet
 *  - CF_CFDP_ReceiveMessage() - gets a received message
 *
 * These functions were originally part of the CFDP engine itself
 * but were split into a separate file, both to improve testability
 * as well as to (potentially) allow interfaces to message/packet services
 * other than the CFE software bus.  However, Note that in this version,
 * there is still a fair amount of CFDP engine mixed into these functions
 * that would have to be isolated.
 *
 * Also note that the creation and deletion of SB pipes is not yet
 * moved into this file.
 */

#include "cfe.h"
#include "cf_verify.h"
#include "cf_app.h"
#include "cf_eventids.h"
#include "cf_perfids.h"
#include "cf_cfdp.h"
#include "cf_utils.h"

#include "cf_cfdp_r.h"
#include "cf_cfdp_s.h"
#include "cf_cfdp_sbintf.h"

#include <string.h>
#include "cf_assert.h"

#if defined(CF_SPACEPACKET_PEC) && (CF_SPACEPACKET_PEC)
#include "cf_pec.h"
#endif

/* Offset for CFDP PDU data within a raw CCSDS SpacePacket (6-byte primary header, no secondary).
 * Used for output encoding so that external entities (e.g. python-cfdp MCS) can parse
 * the PDU directly after the CCSDS primary header without cFE secondary header overhead. */
#define CF_RAW_SPACEPACKET_PDU_OFFSET 6

/*----------------------------------------------------------------
 *
 * Application-scope internal function
 * See description in cf_cfdp_sbintf.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
CF_Logical_PduBuffer_t *CF_CFDP_MsgOutGet(const CF_Transaction_t *txn, bool silent)
{
    /* if channel is frozen, do not take message */
    CF_Channel_t *          chan    = CF_AppData.engine.channels + txn->chan_num;
    bool                    success = true;
    CF_Logical_PduBuffer_t *ret;
    int32                   os_status;

    /* this function should not be called more than once before the message
     * is sent, so if there's already an outgoing message allocated
     * then drop and get a new one (not likely) */
    ret = NULL;
    if (CF_AppData.engine.out.msg)
    {
        CFE_SB_ReleaseMessageBuffer(CF_AppData.engine.out.msg);
        CF_AppData.engine.out.msg = NULL;
    }

    if (CF_AppData.config_table->chan[txn->chan_num].max_outgoing_messages_per_wakeup &&
        (chan->outgoing_counter >= CF_AppData.config_table->chan[txn->chan_num].max_outgoing_messages_per_wakeup))
    {
        /* no more messages this wakeup allowed */
        success = false;
    }

    if (success && !CF_AppData.hk.Payload.channel_hk[txn->chan_num].frozen && !txn->flags.com.suspended)
    {
        /* first, check if there's room in the pipe for the message we want to build */
        if (OS_ObjectIdDefined(chan->sem_id))
        {
            os_status = OS_CountSemTimedWait(chan->sem_id, 0);
        }
        else
        {
            os_status = OS_SUCCESS;
        }

        /* Allocate message buffer on success */
        if (os_status == OS_SUCCESS)
        {
            CF_AppData.engine.out.msg = CFE_SB_AllocateMessageBuffer(CF_RAW_SPACEPACKET_PDU_OFFSET + CF_MAX_PDU_SIZE +
                                                                     CF_PDU_ENCAPSULATION_EXTRA_TRAILING_BYTES);
        }

        if (!CF_AppData.engine.out.msg)
        {
            if (!silent && (os_status == OS_SUCCESS))
            {
                CFE_EVS_SendEvent(CF_CFDP_NO_MSG_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "CF: no output message buffer available");
            }
            success = false;
        }

        if (success)
        {
            CFE_MSG_Init(&CF_AppData.engine.out.msg->Msg,
                         CFE_SB_ValueToMsgId(CF_AppData.config_table->chan[txn->chan_num].mid_output),
                         CF_RAW_SPACEPACKET_PDU_OFFSET);
            ++chan->outgoing_counter; /* even if max_outgoing_messages_per_wakeup is 0 (unlimited), it's ok
                                                    to inc this */

            /* prepare for encoding - the "tx_pdudata" is what serves as the temporary holding area for content */
            ret = &CF_AppData.engine.out.tx_pdudata;
        }
    }

    /* If returning NULL, this only happens when we hit some traffic limit for this channel,
     * either the limit of tx per wakeup, the sync sem is unavailable, or the SB pool is empty. */
    if (ret == NULL)
    {
        /* stop trying to send anything until next wake up */
        chan->tx_blocked = true;
    }
    else
    {
        /* if returning a buffer, then reset the encoder state to point to the beginning of the encapsulation msg */
        CF_CFDP_EncodeStart(&CF_AppData.engine.out.encode, CF_AppData.engine.out.msg, ret, CF_RAW_SPACEPACKET_PDU_OFFSET,
                            CF_RAW_SPACEPACKET_PDU_OFFSET + CF_MAX_PDU_SIZE);
    }

    return ret;
}

/*----------------------------------------------------------------
 *
 * Application-scope internal function
 * See description in cf_cfdp_sbintf.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CF_CFDP_Send(uint8 chan_num, const CF_Logical_PduBuffer_t *ph)
{
    CFE_MSG_Size_t sb_msgsize;

    CF_Assert(chan_num < CF_NUM_CHANNELS);

    /* now handle the SB encapsulation - this should reflect the
     * length of the entire message, including encapsulation */
    sb_msgsize = CF_RAW_SPACEPACKET_PDU_OFFSET;
    sb_msgsize += ph->pdu_header.header_encoded_length;
    sb_msgsize += ph->pdu_header.data_encoded_length;
    sb_msgsize += CF_PDU_ENCAPSULATION_EXTRA_TRAILING_BYTES;

    CFE_MSG_SetSize(&CF_AppData.engine.out.msg->Msg, sb_msgsize);
    /* Skip CFE_MSG_SetMsgTime - output uses raw CCSDS header without secondary header,
     * so SetMsgTime would corrupt the CFDP PDU data at offset 6. */

#if defined(CF_SPACEPACKET_PEC) && (CF_SPACEPACKET_PEC)
    /* Append the 16-bit Packet Error Control (PEC) field (CRC-16-CCITT) per
     * ECSS-E-ST-70-41C B.1.6.  The CRC covers the entire space packet except
     * the two PEC octets, which are stored high byte first as the last two
     * octets (already reserved in sb_msgsize via the trailing-byte count). */
    {
        uint8 *pec_base = (uint8 *)&CF_AppData.engine.out.msg->Msg;
        uint16 pec_crc  = CF_PEC_Calc(pec_base, sb_msgsize - CF_PEC_SIZE_BYTES);

        pec_base[sb_msgsize - 2] = (uint8)((pec_crc >> 8) & 0xFF);
        pec_base[sb_msgsize - 1] = (uint8)(pec_crc & 0xFF);
    }
    /* Transmit WITHOUT origination so the Software Bus does not rewrite the CCSDS
     * sequence count (CFE_MSG_SetSequenceCount) after the PEC has been computed -
     * that would invalidate the CRC at the receiver. The sequence count is unused
     * for raw CFDP encapsulation (the CFDP PDU carries its own sequencing). */
    CFE_SB_TransmitBuffer(CF_AppData.engine.out.msg, false);
#else
    CFE_SB_TransmitBuffer(CF_AppData.engine.out.msg, true);
#endif

    ++CF_AppData.hk.Payload.channel_hk[chan_num].counters.sent.pdu;

    CF_AppData.engine.out.msg = NULL;
}

/*----------------------------------------------------------------
 *
 * Application-scope internal function
 * See description in cf_cfdp_sbintf.h for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CF_CFDP_ReceiveMessage(CF_Channel_t *chan)
{
    uint32           count = 0;
    int32            status;
    const int        chan_num = (chan - CF_AppData.engine.channels);
    CFE_SB_Buffer_t *bufptr;
    CFE_MSG_Size_t   msg_size;
    CFE_MSG_Type_t   msg_type = CFE_MSG_Type_Invalid;

    CF_Logical_PduBuffer_t *ph;

    for (; count < CF_AppData.config_table->chan[chan_num].rx_max_messages_per_wakeup; ++count)
    {
        status = CFE_SB_ReceiveBuffer(&bufptr, chan->pipe, CFE_SB_POLL);
        if (status != CFE_SUCCESS)
        {
            break; /* no more messages */
        }

        ph = &CF_AppData.engine.in.rx_pdudata;
        CFE_ES_PerfLogEntry(CF_PERF_ID_PDURCVD(chan_num));
        CFE_MSG_GetSize(&bufptr->Msg, &msg_size);
        CFE_MSG_GetType(&bufptr->Msg, &msg_type);

#if defined(CF_SPACEPACKET_PEC) && (CF_SPACEPACKET_PEC)
        /* Verify the 16-bit Packet Error Control (PEC) field (CRC-16-CCITT)
         * per ECSS-E-ST-70-41C B.1.6.  Running the CRC over the entire packet
         * INCLUDING the two PEC octets yields a zero syndrome when error-free.
         * On failure, drop the packet (do not decode) and count an error. */
        if (msg_size < CF_PEC_SIZE_BYTES || CF_PEC_Calc((const uint8 *)&bufptr->Msg, msg_size) != 0)
        {
            ++CF_AppData.hk.Payload.channel_hk[chan_num].counters.recv.error;
            CFE_EVS_SendEvent(CF_PEC_ERR_EID, CFE_EVS_EventType_ERROR,
                              "CF: dropped received PDU on channel %d: bad packet error control (PEC) CRC",
                              chan_num);
            CFE_ES_PerfLogExit(CF_PERF_ID_PDURCVD(chan_num));
            continue;
        }
#endif

        if (msg_size > CF_PDU_ENCAPSULATION_EXTRA_TRAILING_BYTES)
        {
            /* Ignore/subtract any fixed trailing bytes */
            msg_size -= CF_PDU_ENCAPSULATION_EXTRA_TRAILING_BYTES;
        }
        else
        {
            /* bad message size - not supposed to happen */
            msg_size = 0;
        }
        /* Check if secondary header is present to determine PDU offset */
        bool   has_secondary = false;
        size_t pdu_offset;
        CFE_MSG_GetHasSecondaryHeader(&bufptr->Msg, &has_secondary);
        if (!has_secondary)
        {
            /* No secondary header - CFDP PDU starts after 6-byte CCSDS primary header */
            pdu_offset = 6;
        }
        else if (msg_type == CFE_MSG_Type_Tlm)
        {
            pdu_offset = offsetof(CF_PduTlmMsg_t, ph);
        }
        else
        {
            pdu_offset = offsetof(CF_PduCmdMsg_t, ph);
        }

        CF_CFDP_DecodeStart(&CF_AppData.engine.in.decode, bufptr, ph, pdu_offset, msg_size);

        /* Identify and dispatch this PDU */
        CF_CFDP_ReceivePdu(chan, ph);

        CFE_ES_PerfLogExit(CF_PERF_ID_PDURCVD(chan_num));
    }
}
