#include "interrupt.h"
#include "crc.h"

volatile uint32_t timerA = (INTERRUPT_PERIOD/2);
volatile uint32_t tFlag = FLAG_GENERATING_CRC;
volatile uint32_t tCounter = 0;

uint8_t tPreambleBuffer[(SIZE_OF_PREAMBLE/8)] = { 0b01111110 };  // 0x7e
uint8_t tCrcBuffer[(SIZE_OF_CRC/8)]           = { 0x00000000 };
uint8_t tDlcBuffer[(SIZE_OF_DLC/8)]           = { 0b00100000 };  // 0x20
uint8_t tPayloadBuffer[(SIZE_OF_PAYLOAD/8)]   = { 0b01110100,    // 0x74
                                                  0b01100101,    // 0x65
                                                  0b01110011,    // 0x73
                                                  0b01110100 };  // 0x74
const uint8_t tPolynomial[5]                  = { 0b10000010,
                                                  0b01100000,
                                                  0b10001110,
                                                  0b11011011,
                                                  0b10000000 };

/* Transmitter Interrupt */
ISR(TIMER0_COMPA_vect)
{
	if ((timerA++) > INTERRUPT_PERIOD)
	{
		timerA = 0;
        
        ////////////////////////////////////////////////////////
        // STEP1. GENERATING CRC
        if(tFlag == FLAG_GENERATING_CRC)
        {
            /* Initializes CRC Buffer */
            for(int i=0; i<(SIZE_OF_CRC/8); i++)
                tCrcBuffer[i] = 0x00;

            /* Calculates CRC */
            generateCrc(
                    tCrcBuffer,         // destination
                    tPayloadBuffer,     // source
                    *tDlcBuffer,        // source_size
                    tPolynomial);       // polynomial
       
            /* Initialization for the next flag */
            tCounter = 0;
            tFlag = FLAG_SENDING_PREAMBLE;
        }

        ////////////////////////////////////////////////////////
        // STEP2. SENDING PREAMBLE
        else if(tFlag == FLAG_SENDING_PREAMBLE)
        {
            /* Sending Preamble */
            if(readBit(tPreambleBuffer, tCounter)) SEND_DATA_ONE();
            else SEND_DATA_ZERO();

            /* Finished Sending Preamble */
            if((++tCounter) >= SIZE_OF_PREAMBLE)
            {
                /* Initialization for the next flag */
                tCounter = 0;
                tFlag = FLAG_SENDING_CRC;
            }
        }

        ////////////////////////////////////////////////////////
        // STEP3. SENDING CRC
        else if(tFlag == FLAG_SENDING_CRC)
        {
            /* Sending CRC */
            if(readBit(tCrcBuffer, tCounter)) SEND_DATA_ONE();
            else SEND_DATA_ZERO();
            
            /* Finished Sending CRC */
            if((++tCounter) >= SIZE_OF_CRC)
            {
                /* Initialization for the next flag */
                tCounter = 0;
                tFlag = FLAG_SENDING_DLC;
            }
        }

        ////////////////////////////////////////////////////////
        // STEP4. SENDING DLC
        else if(tFlag == FLAG_SENDING_DLC)
        {
            /* Sending DLC */
            if(readBit(tDlcBuffer, tCounter)) SEND_DATA_ONE();
            else SEND_DATA_ZERO();

            /* Finished Sending DLC */
            if((++tCounter) >= SIZE_OF_DLC)
            {
                /* Initialization for the next flag */
                tCounter = 0;
                tFlag = FLAG_SENDING_PAYLOAD;
            }
        }

        ////////////////////////////////////////////////////////
        // STEP5. SENDING PAYLOAD
        else if(tFlag == FLAG_SENDING_PAYLOAD)
        {
            /* Sending Payload */
            if(readBit(tPayloadBuffer, tCounter)) SEND_DATA_ONE();
            else SEND_DATA_ZERO();
            
            /* Finished Sending Payload */
            if((++tCounter) >= SIZE_OF_PAYLOAD)
            {
                /* Initialization for the next flag */
                tCounter = 0;
                tFlag = 9999;//FLAG_SENDING_PREAMBLE;
            }
        }
        ////////////////////////////////////////////////////////
	}
}

volatile uint32_t rFlag = FLAG_DETECTING_PREAMBLE;
volatile uint32_t rCounter = 0;

uint8_t rQueue[(SIZE_OF_PREAMBLE/8)]          = {0x00};
uint8_t rCrcBuffer[(SIZE_OF_CRC/8)]           = {0x00, 0x00, 0x00, 0x00};
uint8_t rDlcBuffer[(SIZE_OF_DLC/8)]           = {0x00};
uint8_t rPayloadBuffer[(SIZE_OF_PAYLOAD/8)]   = {0x00, 0x00, 0x00, 0x00};

const uint8_t logMsg_preamble[18]   = "Preamble Detected";
const uint8_t logMsg_crc[13]        = "CRC Received";
const uint8_t logMsg_crc_true[14]   = "CRC is correct";
const uint8_t logMsg_crc_false[16]  = "CRC is incorrect";
const uint8_t logMsg_dlc[13]        = "DLC Received";
const uint8_t logMsg_payload[17]    = "Payload Received";

/* Receiver Pin-Change-Interrupt */
ISR(PCINT2_vect)
{
    ////////////////////////////////////////////////////////
    // STEP1. DETECTING PREAMBLE
    if(rFlag == FLAG_DETECTING_PREAMBLE)
    {
        /* Receiving Data */
        updateBit(rQueue, (rCounter%8), receiveData());
            
        /* Printing Received-Bits-String */
        printBit(rQueue, SIZE_OF_PREAMBLE);
        uart_transmit('\r');

        /* Detected the Preamble */
        if(checkPreamble(*rQueue))
        {
            /* Log-Messages */
            uart_changeLine();
            uart_transmit(' ');
            printMsg(logMsg_preamble, 17);
            uart_changeLine();
            uart_changeLine();

            /* Initialization for the next cycle */
            rCounter = 0;
            rFlag = FLAG_RECEIVING_CRC;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP2. RECEIVING CRC
    else if(rFlag == FLAG_RECEIVING_CRC)
    {
        /* Receiving Data */
        updateBit(rCrcBuffer, rCounter, receiveData());
        
        /* Printing Received-Bits-String */
        printBit(rCrcBuffer, SIZE_OF_CRC);
        uart_transmit('\r');

        /* Finished Receiving CRC */
        if((++rCounter) >= SIZE_OF_CRC)
        {
            /* Log-Messages */
            uart_changeLine();
            uart_transmit(' ');
            printMsg(logMsg_crc, 12);
            uart_changeLine();
            uart_changeLine();

            /* Initialization for the next cycle */
            rCounter = 0;
            rFlag = FLAG_RECEIVING_DLC;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP3. RECEIVING DLC
    else if(rFlag == FLAG_RECEIVING_DLC)
    {
        /* Receiving Data */
        updateBit(rDlcBuffer, rCounter, receiveData());

        /* Printing Received-Bits-String */
        printBit(rDlcBuffer, SIZE_OF_DLC);
        uart_transmit('\r');
        
        /* Finished Receiving DLC */
        if((++rCounter) >= SIZE_OF_DLC)
        {
            /* Log-Messages */
            uart_changeLine();
            uart_transmit(' ');
            printMsg(logMsg_dlc, 12);
            uart_changeLine();
            uart_changeLine();

            /* Initialization for the next cycle */
            rCounter = 0;
            rFlag = FLAG_RECEIVING_PAYLOAD;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP4. RECEIVING PAYLOAD
    else if(rFlag == FLAG_RECEIVING_PAYLOAD)
    {
        /* Receiving Data */
        updateBit(rPayloadBuffer, rCounter, receiveData());
        
        /* Printing Received-Bits-String */
        printBit(rPayloadBuffer, SIZE_OF_PAYLOAD);
        uart_transmit('\r');

        /* Finished Receiving PAYLOAD */
        if((++rCounter) >= SIZE_OF_PAYLOAD)
        {
            /* Log-Messages */
            uart_changeLine();
            uart_transmit(' ');
            printMsg(logMsg_payload, 16);
            uart_changeLine();
            uart_changeLine();
            
            /* Initialization for the next cycle */
            rCounter = 0;
            rFlag = FLAG_CHECKING_CRC;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP5. CHECKING CRC
    else if(rFlag == FLAG_CHECKING_CRC)
    {
        /* Checks CRC and Sets Flag */
        if((checkCrc(rCrcBuffer, rPayloadBuffer, *rDlcBuffer, tPolynomial)))
        {
            /* Printing Received-Bits-String */
            printBit(rCrcBuffer, SIZE_OF_CRC);
            uart_changeLine();

            /* Log-Messages */
            uart_transmit(' ');
            printMsg(logMsg_crc_true, 14);
            uart_changeLine();
            uart_changeLine();
        }
        else
        {
            /* Printing Received-Bits-String */
            printBit(rCrcBuffer, SIZE_OF_CRC);
            uart_transmit('\r');
            uart_changeLine();

            /* Log-Messages */
            uart_transmit(' ');
            printMsg(logMsg_crc_false, 16);
            uart_changeLine();
            uart_changeLine();
        }
        
        /* Initialization for the next cycle */
        *rQueue = 0x00;
        for(int i=0; i<4; i++) {rCrcBuffer[i] = 0x00;}
        rDlcBuffer[0] = 0x00;
        for(int i=0; i<4; i++) {rPayloadBuffer[i] = 0x00;}
        rCounter = 0;
        rFlag = FLAG_DETECTING_PREAMBLE;
    }
}

/* Clock Signal Interrupt */
ISR(TIMER0_COMPB_vect)
{
	static volatile uint16_t timerB = 0;
	if ((timerB++) > INTERRUPT_PERIOD)
	{
		timerB = 0;
		
        ////////////////////////////////////////////////////////
		/* CLOCK SIGNAL TRANSMIT */
		PIN_CHANGE();
        ////////////////////////////////////////////////////////
	}
}
