#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "amcom.h"

/// Start of packet character
const uint8_t  AMCOM_SOP         = 0xA1;
const uint16_t AMCOM_INITIAL_CRC = 0xFFFF;
enum{AMCOM_MAX_PAYLOAD = 200};

static uint16_t AMCOM_UpdateCRC(uint8_t byte, uint16_t crc)
{
	byte ^= (uint8_t)(crc & 0x00ff);
	byte ^= (uint8_t)(byte << 4);
	return ((((uint16_t)byte << 8) | (uint8_t)(crc >> 8)) ^ (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3));
}


void AMCOM_InitReceiver(AMCOM_Receiver* receiver, AMCOM_PacketHandler packetHandlerCallback, void* userContext) {

	receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
	receiver->packetHandler = packetHandlerCallback;
	receiver->userContext = userContext;
}

size_t AMCOM_Serialize(uint8_t packetType, const void* payload, size_t payloadSize, uint8_t* destinationBuffer) {

	if (packetType != NULL || destinationBuffer != NULL || payloadSize <= 200) {

	    destinationBuffer[0] = AMCOM_SOP;
	    destinationBuffer[1] = packetType;
	    destinationBuffer[2] = payloadSize;

	    uint16_t crc = AMCOM_INITIAL_CRC;
	    crc = AMCOM_UpdateCRC(packetType, crc);
        crc = AMCOM_UpdateCRC(payloadSize, crc);

	    if (payloadSize == 0) {
	        destinationBuffer[3] = crc & 0xFF;
	        destinationBuffer[4] =  crc >> 8;

	        return 5;
	    } else {

            size_t i = 0;
            for(i; i < payloadSize; i++) {
                crc = AMCOM_UpdateCRC(*((uint8_t*)payload + i), crc);
                destinationBuffer[i+5] = *((uint8_t*)payload + i);
            }

            destinationBuffer[3] = crc & 0xFF;
	        destinationBuffer[4] =  crc >> 8;

            return 5 + i;
	    }
	} else {
	    return 0;
	}
}

void AMCOM_Deserialize(AMCOM_Receiver* receiver, const void* data, size_t dataSize) {

    uint8_t data_i;

    for (size_t i = 0; i < dataSize; i++) {

        data_i = *((uint8_t*)data + i);

        switch(receiver->receivedPacketState) {

            case AMCOM_PACKET_STATE_EMPTY:
                if (AMCOM_SOP == data_i) {
                    receiver->receivedPacket.header.sop = AMCOM_SOP;
                    receiver->payloadCounter = 0;
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_SOP;
                }
                break;

            case AMCOM_PACKET_STATE_GOT_SOP:
                receiver->receivedPacket.header.type = data_i;
                receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_TYPE;
                break;

            case AMCOM_PACKET_STATE_GOT_TYPE:
                if (data_i > AMCOM_MAX_PAYLOAD) {
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
                } else {
                    receiver->receivedPacket.header.length = data_i;
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_LENGTH;
                }
                break;

            case AMCOM_PACKET_STATE_GOT_LENGTH:
                receiver->receivedPacket.header.crc = (uint16_t)data_i;
                receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_CRC_LO;
                break;

            case AMCOM_PACKET_STATE_GOT_CRC_LO:
                receiver->receivedPacket.header.crc |= (uint16_t)(data_i << 8);
                if (receiver->receivedPacket.header.length == 0) {
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                } else {
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GETTING_PAYLOAD;
                }
                break;

            case AMCOM_PACKET_STATE_GETTING_PAYLOAD:
                if (receiver->payloadCounter < receiver->receivedPacket.header.length) {
                    receiver->receivedPacket.payload[receiver->payloadCounter] = data_i;
                    receiver->payloadCounter++;
                }

                if (receiver->payloadCounter == receiver->receivedPacket.header.length) {
                    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
                }
                break;
        }

        if (receiver->receivedPacketState == AMCOM_PACKET_STATE_GOT_WHOLE_PACKET) {
            uint16_t crc_check = AMCOM_INITIAL_CRC;

            crc_check = AMCOM_UpdateCRC(receiver->receivedPacket.header.type, crc_check);
            crc_check = AMCOM_UpdateCRC(receiver->receivedPacket.header.length, crc_check);

            for (size_t j = 0; j < receiver->receivedPacket.header.length; j++) {
                crc_check = AMCOM_UpdateCRC(receiver->receivedPacket.payload[j], crc_check);
            }

            if (receiver->receivedPacket.header.crc == crc_check) {
                receiver->packetHandler(&receiver->receivedPacket, receiver->userContext);
            }

            receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
        }
    }
}