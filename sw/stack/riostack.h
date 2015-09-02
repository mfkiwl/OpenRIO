/******************************************************************************
 * (C) Copyright 2013-2015 Magnus Rosenius and the Free Software Foundation.
 *
 * This file is part of OpenRIO.
 *
 * OpenRIO is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenRIO is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Lesser Public License
 * along with OpenRIO.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

/******************************************************************************
 * Description:
 * -----------------
 * |  OS dependent |
 * |  (your code)  |
 * -----------------
 *        |
 * -----------------
 * |   RioStack    |
 * -----------------
 *        |
 * -----------------
 * | Symbol Codec  |
 * |  (your code)  |
 * -----------------
 *        |
 * -----------------
 * |  Port driver  |
 * -----------------
 *        |
 * -----------------
 * | Physical port |
 * -----------------
 *
 * The symbol codec maps a RapidIO symbol to the physical transmission media.
 *
 * Symbols are in four flavors, idle, control, data and error. They are abstract 
 * and should be serialized by any implementation to be sent on a transmission 
 * channel. Error symbols are never generated by the stack and are used if the 
 * symbol decoder encounters an error that the stack should be notified of.
 * 
 * Symbols are inserted into the stack by calling RIOSTACK_portAddSymbol() and symbols to
 * transmit are fetched from the stack using RIOSTACK_portGetSymbol(). These two
 * functions are the low-level interface towards a physical transmission channel.
 * The function RIOSTACK_portSetStatus() is used to indicate to the stack that initial
 * training of the symbol codec has been completed and that the transmission port
 * is ready to accept other symbols than idle. The procedure is to set the port
 * status to initialized once idle symbols are successfully received.
 *
 * On the high-level interface are RIOSTACK_setOutboundPacket() functions used to 
 * insert packets into the outbound transmission queue and RIOSTACK_getInboundPacket() 
 * to get packet from the inbound reception queue. The RIOSTACK_getInboundQueueLength() 
 * function is used to check if any packet is available for reading in the 
 * inbound reception queue.
 * 
 * Some typical patterns to handle this stack are:
 * Initialization:
 *   RIO_open(...);
 *   RIO_portSetTimeout(...);
 *   ...
 *   <Symbol transcoder is successfully decoding symbols from the link>
 *   RIO_portSetStatus(1);
 *
 * Bottom-half traffic handling:
 *   RIO_portSetTime(...);
 *   <get symbol from decoder>
 *   RIO_portAddSymbol(...);
 *   s = RIO_portGetSymbol(...);
 *   <send symbol to encoder>
 *
 * Receiving packets:
 *   if(RIOSTACK_getInboundQueueLength(stack) > 0)
 *   {
 *     RIOSTACK_getInboundPacket(stack, packet);
 *     switch(RIOPACKET_getFtype(packet))
 *     {
 *       case RIOPACKET_FTYPE_MAINTENANCE:
 *         if(RIOPACKET_getTransaction(packet) == RIOPACKET_TRANSACTION_MAINT_READ_REQUEST)
 *         {
 *           RIOPACKET_getMaintReadRequest(packet, ...);
 *           ...
 *         }
 *       ...
 *     }
 *   }
 *
 * Transmitting packets:
 *   if(RIOSTACK_getOutboundQueueAvailable(stack) > 0)
 *   {
 *     <create a riopacket>
 *     RIOSTACK_setOutboundPacket(stack, packet);
 *   }
 *   ...
 *
 * Any application specific tailoring needed to compile properly should be done 
 * in rioconfig.h.
 *******************************************************************************/

#ifndef _RIOSTACK_H
#define _RIOSTACK_H

/*******************************************************************************
 * Includes
 *******************************************************************************/

#include "riopacket.h"


/*******************************************************************************
 * Global typedefs
 *******************************************************************************/

/** The size of a buffer that can fit a full sized RapidIO packet and its size 
    in words (32-bit). */
#define RIOSTACK_BUFFER_SIZE (RIOPACKET_SIZE_MAX+1u)


/** Define the different types of RioSymbols. */
typedef enum 
{
  RIOSTACK_SYMBOL_TYPE_IDLE, 
  RIOSTACK_SYMBOL_TYPE_CONTROL, 
  RIOSTACK_SYMBOL_TYPE_DATA, 
  RIOSTACK_SYMBOL_TYPE_ERROR
} RioSymbolType_t;


/**
 * RapidIO symbol definition.
 * Idle symbol: Sent when nothing else to send. Does not use the data field.
 * Control symbol: Sent when starting, ending and acknowleding a packet. Data 
 * is right aligned, (Unused, C0, C1, C2) where C0 is transmitted/received first.
 * Data symbol: Sent to transfer packets. Uses the full data field, (D0, D1, 
 * D2, D3) where D0 is transmitted/received first.
 * Error symbols are created when a symbols could not be created and the stack 
 * should know about it.
 */
typedef struct
{
  RioSymbolType_t type;
  uint32_t data;
} RioSymbol_t;
 

/** Receiver states. */
typedef enum 
{
  RX_STATE_UNINITIALIZED, 
  RX_STATE_PORT_INITIALIZED, /**< This state is entered to initialize the link.  */
  RX_STATE_LINK_INITIALIZED, /**< The normal state. Accept packets and forward them. */
  RX_STATE_INPUT_RETRY_STOPPED, /**< This state is entered when no more buffers was available and a packet was received. */
  RX_STATE_INPUT_ERROR_STOPPED /**< This state is entered when an error situation has occurred. */
} RioReceiverState_t;


/** Transmitter states. */
typedef enum 
{
  TX_STATE_UNINITIALIZED, 
  TX_STATE_PORT_INITIALIZED, /**< This state is entered to initialize the link. */
  TX_STATE_LINK_INITIALIZED, /**< The normal state. Accept packets and forward them. */
  TX_STATE_SEND_PACKET_RETRY, /**< This state is set by the receiver to force a packet-retry-symbol to be transmitted. */
  TX_STATE_SEND_PACKET_NOT_ACCEPTED, /**< This state is set by the receiver to force a packet-not-accepted-symbol 
                                        to be transmitted. */
  TX_STATE_SEND_LINK_RESPONSE, /**< This state is set by the receiver to force a link-response-symbol to be 
                                  transmitted. */
  TX_STATE_OUTPUT_RETRY_STOPPED, /**< This state is entered when the link-partner has transmitted a 
                                    packet-retry-symbol.  */
  TX_STATE_OUTPUT_ERROR_STOPPED /**< This state is entered when the link partner has encountered any problem 
                                   which is indicated by sending a packet-not-accepted symbol or if a packet 
                                   timeout has expired. */
} RioTransmitterState_t;


/** RioQueue_t definition. */
/** The RioQueue_t contains functionality to handle a transmission window. A packet is added at the back, 
    transmitted at the window and removed from the front. It is used in both ingress and egress directions but 
    the window functionality is unused at the ingress direction.*/
/** \internal Note that this structure is for internal usage only. */
typedef struct 
{
  uint8_t size; /**< The maximum number of elements in the queue. */
  uint8_t available; /**< The number of free elements in the queue. */
  uint8_t windowSize; /**< The number of pending packets that has not been acknowledged. */
  uint8_t windowIndex; /**< The element to transmit next. */
  uint8_t frontIndex; /**< The element to remove next (when an acknowledge has arrived). */
  uint8_t backIndex; /**< The element to fill with a new value. */
  uint32_t *buffer_p; /**< The data area to store the queue elements in. */
} RioQueue_t;


/* Constant used to forward different errors to the link partner. */
/** \internal Note that this structure is for internal usage only. */
typedef enum
{
  PACKET_NOT_ACCEPTED_CAUSE_RESERVED=0u,
  PACKET_NOT_ACCEPTED_CAUSE_UNEXPECTED_ACKID=1u,
  PACKET_NOT_ACCEPTED_CAUSE_CONTROL_CRC=2u,
  PACKET_NOT_ACCEPTED_CAUSE_NON_MAINTENANCE=3u,
  PACKET_NOT_ACCEPTED_CAUSE_PACKET_CRC=4u,
  PACKET_NOT_ACCEPTED_CAUSE_ILLEGAL_CHARACTER=5u,
  PACKET_NOT_ACCEPTED_CAUSE_NO_RESOURCE=6u,
  PACKET_NOT_ACCEPTED_CAUSE_DESCRAMBLER=7u,
  PACKET_NOT_ACCEPTED_CAUSE_GENERAL=31u
} RioStackPacketNotAcceptedCause_t;



/** The structure to keep all the RapidIO stack variables. */
typedef struct
{
  /* Receiver variables. */
  RioReceiverState_t rxState; /**< The state of the receiver. */
  uint8_t rxCounter; /**< Counter for keeping track of the current inbound packet position. */
  uint16_t rxCrc; /**< Current CRC value for the inbound packet. */
  uint8_t rxStatusReceived; /**< Indicate if a correct status has been received. */
  uint8_t rxAckId; /**< The current ackId of the receiver. */
  uint8_t rxAckIdAcked; /**< The ackId that has been acknowledged. Indicates to the transmitter to send packet-accepted. */
  RioStackPacketNotAcceptedCause_t rxErrorCause; /**< The cause of a packet not being accepted to send by the transmitter. */
  RioQueue_t rxQueue; /**< The inbound queue of packets. */

  /* Transmitter variables. */
  RioTransmitterState_t txState; /**< The state of the transmitter. */
  uint8_t txCounter; /**< Counter for keeping track of the current outbound packet position. */
  uint16_t txStatusCounter; /**< Counter for keeping track of the number of status-control-symbols transmitted at startup. */
  uint8_t txFrameState; /**< The state of the outbound packet, i.e. what to send next. */
  uint32_t txFrameTimeout[32]; /**< An array of timestamps mapping to when the packet with ackId was transmitted. */
  uint8_t txAckId; /**< The ackId that is awaiting a packet-accepted. */
  uint8_t txAckIdWindow; /**< The ackId that was las transmitted. */
  uint8_t txBufferStatus; /**< The buffer status of the link-partner. */
  RioQueue_t txQueue; /**< The outbound queue of packets. */

  /* Common protocol stack variables. */
  uint32_t portTime; /**< The current time to use. */
  uint32_t portTimeout; /**< The time to use as timeout. */

  /** The number of successfully received packets. */
  uint32_t statusInboundPacketComplete;

  /** The number of retried received packets. 
      This will happen if the receiver does not have resources available when an inbound packet is received. */
  uint32_t statusInboundPacketRetry;

  /** The number of received erronous control symbols. 
      This may happen if the inbound link has a high bit-error-rate. */
  uint32_t statusInboundErrorControlCrc;

  /** The number of received packets with an unexpected ackId. 
      This may happen if the inbound link has a high bit-error-rate. */
  uint32_t statusInboundErrorPacketAckId;

  /** The number of received packets with a checksum error. 
      This may happen if the inbound link has a high bit-error-rate. */
  uint32_t statusInboundErrorPacketCrc;

  /** The number of received symbols that contains an illegals character. 
      This may happen if the inbound link has a high bit-error-rate or if characters are missing in the 
      inbound character stream. */
  uint32_t statusInboundErrorIllegalCharacter;

  /** The number of general errors encountered at the receiver that does not fit into the other categories. 
      This happens if too short or too long packets are received. */
  uint32_t statusInboundErrorGeneral;

  /** The number of received packets that were discarded since they were unsupported by the stack. 
      This will happen if an inbound packet contains information that cannot be accessed using the function API 
      of the stack. */
  uint32_t statusInboundErrorPacketUnsupported;

  /** The number of successfully transmitted packets. */
  uint32_t statusOutboundPacketComplete;

  /** The maximum time between a completed outbound packet and the reception of its pcakcet-accepted control-symbol. */
  uint32_t statusOutboundLinkLatencyMax;

  /** The number of retried transmitted packets. 
      This will happen if the receiver at the link-partner does not have resources available when an outbound
      packet is received. */
  uint32_t statusOutboundPacketRetry;

  /** The number of outbound packets that has had its retransmission timer expired. 
      This happens if the latency of the system is too high or if a packet is corrupted due to a high 
      bit-error-rate on the outbound link. */
  uint32_t statusOutboundErrorTimeout;

  /** The number of packet-accepted that was received that contained an unexpected ackId. 
      This happens if the transmitter and the link-partner is out of synchronization, probably due 
      to a software error. */
  uint32_t statusOutboundErrorPacketAccepted;

  /** The number of packet-retry that was received that contained an unexpected ackId. 
      This happens if the transmitter and the link-partner is out of synchronization, probably due to
      a software error. */
  uint32_t statusOutboundErrorPacketRetry;

  /** The number of received link-requests. 
      This happens if the link-partner transmitter has found an error and need to resynchronize itself 
      to the receiver. */
  uint32_t statusPartnerLinkRequest;

  /** The number of received erronous control symbols at the link-partner receiver. 
      This may happen if the outbound link has a high bit-error-rate. */
  uint32_t statusPartnerErrorControlCrc;

  /** The number of received packets with an unexpected ackId at the link-partner receiver. 
      This may happen if the outbound link has a high bit-error-rate. */
  uint32_t statusPartnerErrorPacketAckId;

  /** The number of received packets with a checksum error at the link-partner receiver. 
      This may happen if the outbound link has a high bit-error-rate. */
  uint32_t statusPartnerErrorPacketCrc;

  /** The number of received symbols that contains an illegals character at the link-parter receiver. 
      This may happen if the outbound link has a high bit-error-rate or if characters are missing in the 
      outbound character stream. */
  uint32_t statusPartnerErrorIllegalCharacter;

  /** The number of general errors encountered at the receiver that does not fit into the other categories. 
      This happens depending on the link-partner implementation. */
  uint32_t statusPartnerErrorGeneral;

  /* Private user data. */
  void* private;
} RioStack_t;


/*******************************************************************************
 * Global function prototypes
 *******************************************************************************/

/**
 * \brief Open the RapidIO stack for operation.
 *
 * \param[in] stack Stack instance to operate on.
 * \param[in] private Pointer to an opaque data area containing private user data.
 * \param[in] rxPacketBufferSize Number of words to use as reception buffer. This 
 *            argument specifies the size of rxPacketBuffer.
 * \param[in] rxPacketBuffer Pointer to buffer to store inbound packets in.
 * \param[in] txPacketBufferSize Number of words to use as transmission buffer. This 
 *            argument specifies the size of txPacketBuffer.
 * \param[in] txPacketBuffer Pointer to buffer to store outbound packets in.
 *
 * This function initializes all internally used variables in the stack. The stack will 
 * however not be operational until the transcoder has signalled that it is ready for
 * other symbols than idle. This is done using the function RIOSTACK_portSetStatus(). Once 
 * this function has been called it is possible to get and set symbols and to issue
 * requests. The requests will be transmitted once the link initialization has 
 * been completed.
 * 
 * The rxPacketBuffer/txPacketBuffer arguments are word buffers that are used internally 
 * to store the inbound and outbound packet queues. 
 */
void RIOSTACK_open(RioStack_t *stack, void *private, 
                   const uint32_t rxPacketBufferSize, uint32_t *rxPacketBuffer, 
                   const uint32_t txPacketBufferSize, uint32_t *txPacketBuffer);

/*******************************************************************************************
 * Stack status functions.
 * Note that status counters are access directly in the stack-structure.
 *******************************************************************************************/

/**
 * \brief Get the status of the link.
 *
 * \param[in] stack The stack to operate on.
 * \return Returns the status of the link, zero if link is uninitialized and non-zero if 
 * the link is initialized.
 *
 * This function indicates if the link is up and ready to relay packets. 
 */
uint8_t RIOSTACK_getLinkIsInitialized(const RioStack_t *stack);

/**
 * \note Deprecated, use RIOSTACK_getLinkIsInitialized().
 */
uint8_t RIOSTACK_getStatus(const RioStack_t *stack);

/**
 * \brief Get the number of pending outbound packets.
 *
 * \param[in] stack The stack to operate on.
 * \return Returns the number of pending outbound packets.
 *
 * This function checks the outbound queue and returns the number of packets 
 * that are pending to be transmitted onto the link.
 */
uint8_t RIOSTACK_getOutboundQueueLength(const RioStack_t *stack);

/**
 * \brief Get the number of available outbound packets.
 *
 * \param[in] stack The stack to operate on.
 * \return Returns the number of available outbound packets.
 *
 * This function checks the outbound queue and returns the number of packets 
 * that are available before the queue is full.
 */
uint8_t RIOSTACK_getOutboundQueueAvailable(const RioStack_t *stack);

/**
 * \brief Add a packet to the outbound queue.
 *
 * \param[in] stack The stack to operate on.
 * \param[in] packet The packet to send.
 *
 * This function sends a packet.
 *
 * \note The packet CRC is not checked. It must be valid before it is used as 
 * argument to this function.
 *
 * \note Call RIOSTACK_outboundQueueAvailable() before this function is called to make sure
 * the outbound queue has transmission buffers available.
 *
 * \note Use RIOSTACK_getStatus() to know when a packet is allowed to be transmitted.
 */
void RIOSTACK_setOutboundPacket(RioStack_t *stack, RioPacket_t *packet);

/**
 * \brief Get the number of pending inbound packets.
 *
 * \param[in] stack The stack to operate on.
 * \return Returns the number of pending inbound packets.
 *
 * This function checks the inbound queue and returns the number of packets 
 * that has been received but not read by the user yet.
 */
uint8_t RIOSTACK_getInboundQueueLength(const RioStack_t *stack);

/**
 * \brief Get the number of available inbound packets.
 *
 * \param[in] stack The stack to operate on.
 * \return Returns the number of available inbound packets.
 *
 * This function checks the inbound queue and returns the number of packets 
 * that can be received without the queue is full.
 */
uint8_t RIOSTACK_getInboundQueueAvailable(const RioStack_t *stack);

/**
 * \brief Get, remove and return a packet from the inbound queue.
 *
 * \param[in] stack The stack to operate on.
 * \param[in] packet The packet to receive to.
 *
 * This function moves a packet from the inbound packet queue to the location of the packet 
 * in the argument list.
 */
void RIOSTACK_getInboundPacket(RioStack_t *stack, RioPacket_t *packet);

/*******************************************************************************************
 * Port functions (backend API towards physical device)
 *******************************************************************************************/

/**
 * \brief Set a port current time.
 *
 * \param[in] stack The stack to operate on.
 * \param[in] timer The current time.
 *
 * This function indicates to the stack the current time and this is used internally 
 * to calculate when a packet timeout should be triggered. Use this together with RIOSTACK_setPortTimeout() 
 * to allow for the stack to handle timeouts.
 * 
 * \note The time value must have the same unit as RIOSTACK_portSetTimeout().
 */
void RIOSTACK_portSetTime(RioStack_t *stack, const uint32_t timer);

/**
 * \brief Set a port timeout limit.
 *
 * \param[in] stack The stack to operate on.
 * \param[in] timer The time out threshold.
 *
 * The time to wait for a response from the link partner. The unit of the 
 * timeout value should be the same as the time used in RIOSTACK_setPortTime().
 *
 * This function is used to set a timeout threshold value and is used to know when 
 * an acknowledge should have been received from a link partner.
 * 
 * \note The time value must have the same unit as RIOSTACK_portSetTime().
 */
void RIOSTACK_portSetTimeout(RioStack_t *stack, const uint32_t timer);

/**
 * \brief Set a ports status.
 * 
 * \param[in] stack The stack to operate on.
 * \param[in] initialized The state of the port.
 *
 * If set to non-zero, the symbol encoder/decoder indicates to the stack that
 * it is successfully encoding/decoding symbol, i.e. synchronized to the link.
 *
 * This function indicates to the stack if the port that are encoding/decoding
 * symbols are ready to accept other symbols than idle-symbols. If the
 * encoding/decoding loses synchronization then this function should be called
 * with an argument equal to zero to force the stack to resynchronize the link.
 */
void RIOSTACK_portSetStatus(RioStack_t *stack, const uint8_t initialized);

/**
 * \brief Add a new symbol to the RapidIO stack.
 *
 * \param[in] stack The stack to operate on.
 * \param[in] symbol A symbol received from a port.
 *
 * This function is used to insert new data, read from a port, into the stack. The
 * symbols will be concatenated to form packets that can be accessed using other
 * functions.
 */
void RIOSTACK_portAddSymbol(RioStack_t *stack, const RioSymbol_t symbol);

/**
 * \brief Get the next symbol to transmit on a port.
 *
 * \param[in] stack The stack to operate on.
 * \return A symbol that should be sent on a port.
 *
 * This function is used to fetch new symbols to transmit on a port. Packets that
 * are inserted are split into symbols that are accessed with this function.
 */
RioSymbol_t RIOSTACK_portGetSymbol(RioStack_t *stack);

#endif /* _RIOSTACK_H */
 
/*************************** end of file **************************************/
