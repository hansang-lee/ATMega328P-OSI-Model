#pragma once
#include <avr/interrupt.h>
#include "interrupt.h"
#include "crc.h"
#include "uart.h"
#include "layer3.c"

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
            rightShift(tPayloadBuffer, *tDlcBuffer, 2);
            tPayloadBuffer[0] = *tDestination;
            tPayloadBuffer[1] = *tSource;

            generateCrc(
                    tCrcBuffer,         // destination
                    tPayloadBuffer,     // source
                    *tDlcBuffer,        // source_size
                    _polynomial);       // polynomial
       
            /* Initialization for the next flag */
            tCounter = 0;
            tFlag = FLAG_SENDING_PREAMBLE;
        }

        ////////////////////////////////////////////////////////
        // STEP2. SENDING PREAMBLE
        else if(tFlag == FLAG_SENDING_PREAMBLE)
        {
            /* Sending Preamble */
            if(readBit(_preamble, tCounter)) SEND_DATA_ONE();
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
	}
}

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
        updateBit(rFrame->crc, rCounter, receiveData());
        
        /* Printing Received-Bits-String */
        printBit(rFrame->crc, SIZE_OF_CRC);
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
        updateBit(rFrame->dlc, rCounter, receiveData());

        /* Printing Received-Bits-String */
        printBit(rFrame->dlc, SIZE_OF_DLC);
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
        updateBit(rFrame->payload, rCounter, receiveData());
        
        /* Printing Received-Bits-String */
        printBit(rFrame->payload, *(rFrame->dlc));
        uart_transmit('\r');

        /* Finished Receiving PAYLOAD */
        if((++rCounter) >= (*(rFrame->dlc)))
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
        if((checkCrc(rFrame->crc, rFrame->payload, *(rFrame->dlc), _polynomial)))
        {
            /* Printing Received-Bits-String */
            printBit(rFrame->crc, SIZE_OF_CRC);
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
            printBit(rFrame->crc, SIZE_OF_CRC);
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
        rCounter = 0;
        rFlag = FLAG_PROCESSING_DATA;
    }
    
    ////////////////////////////////////////////////////////
    // STEP6. LAYER3
    else if(rFlag == FLAG_PROCESSING_DATA)
    {
        layer3(rFrame);
    }
}

/* Clock Signal Interrupt */
ISR(TIMER0_COMPB_vect)
{
	if ((timerB++) > INTERRUPT_PERIOD)
	{
		timerB = 0;
		
        ////////////////////////////////////////////////////////
		/* CLOCK SIGNAL TRANSMIT */
		PIN_CHANGE();
        ////////////////////////////////////////////////////////
	}
}