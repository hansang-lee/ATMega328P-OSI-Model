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
            uint32_t size = ((*tDlcBuffer+SIZE_OF_CRC)/8);
            uint8_t data[size];
            for(int i=0; i<size; i++) data[i]=0x00;
            uint32_t steps = (*tDlcBuffer + 1);
            
            /* Copies the payload to the buffer for CRC calculation */
            for(int i=0; i<4; i++)
                data[i] = tPayloadBuffer[i];

            /* CRC Calculation */
            while(tCounter < steps)
            {
                /* Payload MSB is 0 : Left Shift */
                if(!(readBit(data, 0)))
                {
                    for(int k=0; k<5; k++)
                    {
                        if(readBit(&data[k], 0))
                            (!((k-1)<0))?(data[k-1]+=0b00000001):(0);

                        data[k] &= 0b01111111;
                        data[k] <<= 1;
                    }
                    tCounter++;
                }

                /* Payload MSB is 0 : Executes XOR */
                else
                {
                    for(int j=0; j<SIZE_OF_POLYNOMIAL; j++)
                        writeBit(data, j, (readBit(data, j) ^ readBit(tPolynomial, j)));
                }
            }

            /* Copies the generated CRC to global variable */
            for(int i=0; i<4; i++)
                tCrcBuffer[i] = data[i];
            
            /* Debugging */
            for(int i=0; i<SIZE_OF_CRC; i++)
            {
                if((i%8)==0) uart_transmit(' ');
                if(readBit(data, i)) uart_transmit('1');
                else uart_transmit('0');
            }
            uart_changeLine();

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
        // STEP2. SENDING CRC
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
        // STEP3. SENDING DLC
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
        // STEP4. SENDING PAYLOAD
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
                tFlag = 1000;
                //tFlag = FLAG_SENDING_PREAMBLE;
            }
        }
        ////////////////////////////////////////////////////////
	}
}

volatile uint32_t rFlag = FLAG_DETECTING_PREAMBLE;
volatile uint32_t rCounter = 0;

uint8_t rPreambleBuffer[1]  = {0x00};
uint8_t rCrcBuffer[4]       = {0x00, 0x00, 0x00, 0x00};
uint8_t rDlcBuffer[1]       = {0x00};
uint8_t rPayloadBuffer[4]   = {0x00, 0x00, 0x00, 0x00};

const uint8_t logMsg_preamble[18]   = "Preamble Detected";
const uint8_t logMsg_crc[13]        = "CRC Received";
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
        updateBit(rPreambleBuffer, (rCounter%8), receiveData());

        /* Printing Received-Bits-String */
        uart_transmit(' ');
        printBit(rPreambleBuffer, SIZE_OF_PREAMBLE);
        uart_transmit('\r');

        /* Detected the Preamble */
        if(checkPreamble(*rPreambleBuffer) == TRUE)
        {
            /* Log-Messages */
            uart_changeLine();
            uart_transmit(' ');
            printMsg(logMsg_preamble, 17);
            uart_changeLine();
            uart_changeLine();

            /* Initialization for the next cycle */
            *rPreambleBuffer = 0x00;
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
        uart_transmit(' ');
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
            for(int i=0; i<4; i++)
                rCrcBuffer[i] = 0x00;
            rCounter = 0;
            rFlag = FLAG_RECEIVING_SIZE;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP3. RECEIVING DLC
    else if(rFlag == FLAG_RECEIVING_SIZE)
    {
        /* Receiving Data */
        updateBit(rDlcBuffer, rCounter, receiveData());
        
        /* Printing Received-Bits-String */
        uart_transmit(' ');
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
            rDlcBuffer[0] = 0x00;
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
        uart_transmit(' ');
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
            for(int i=0; i<4; i++)
                rPayloadBuffer[i] = 0x00;
            rCounter = 0;
            rFlag = FLAG_CHECKING_CRC;
        }
    }

    ////////////////////////////////////////////////////////
    // STEP5. CHECKING CRC
    else if(rFlag == FLAG_CHECKING_CRC)
    {
        //rFlag = FLAG_DETECTING_PREAMBLE;
    }
    ////////////////////////////////////////////////////////
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
