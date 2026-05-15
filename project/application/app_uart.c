/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdint.h>
#include <string.h>



#include "stm32f1xx_ll_bus.h"
#include "stm32f1xx_ll_gpio.h"
#include "stm32f1xx_ll_usart.h"
#include "stm32f1xx_ll_dma.h"


#include "app_uart.h"


//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
//------ TX  -------  RX  -------------------------------------------------------
//------ PA9 ------- PA10 -------------------------------------------------------
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
//oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo
//oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo
//ooooooo ooooooooooooooooooo o ooooooooooo o ooooooooo oo            oooooooooo
//oooooooo ooooooooooooooooo ooo oooooooooo oo oooooooo oo oooooooooo oooooooooo
//ooooooooo ooooooooooooooo ooooo ooooooooo ooo ooooooo oo oooooooooo oooooooooo
//oooooooooo ooooooooooooo ooooooo oooooooo oooo oooooo oo oooooooooo oooooooooo
//ooooooooooo ooooooooooo ooooooooo ooooooo ooooo ooooo oo oooooooooo oooooooooo
//oooooooooooo ooooooooo oooo   oooo oooooo oooooo oooo oo oooooooooo oooooooooo
//ooooooooooooo ooooooo ooooooooooooo ooooo ooooooo ooo oo oooooooooo oooooooooo
//oooooooooooooo ooooo ooooooooooooooo oooo oooooooo oo oo oooooooooo oooooooooo
//ooooooooooooooo ooo ooooooooooooooooo ooo ooooooooo o oo oooooooooo oooooooooo
//oooooooooooooooo o ooooooooooooooooooo oo oooooooooo  oo oooooooooo oooooooooo
//ooooooooooooooooo ooooooooooooooooooooo o ooooooooooo oo            oooooooooo
//oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo




//----------- G L O B A L --------------//
extern volatile uint16_t cmd_idx;
extern volatile uint8_t line_ready;
extern char cmd_buffer[CMD_LINE_SIZE];

//extern  uint32_t wait_timeout_ms;
//extern uint8_t wait_active;


uint8_t rx_dma_buffer[RX_BUF_SIZE]; // for DMA


uint32_t last_pos = 0; 

static void Process_And_Send(uint8_t *data, uint32_t len);
static void Append_To_Line_Buffer(uint8_t* source, uint32_t length);

// static char string1[] = "Hello! I RECEIVED YOUR MESSAGE! \r\n";









//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//UART + DMA
//----------------------------------------------------------------------------


void uart_init_all(void)
{
  // UART 1 //
  //-- config GPIO and GPIO_PORT--//
  //TX (PA9)
  //RX (PA10)

  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
    
  // TX
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // RX
  GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_FLOATING;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  
  NVIC_SetPriority(USART1_IRQn, 0);
  NVIC_EnableIRQ(USART1_IRQn);


  LL_USART_InitTypeDef USART_InitStruct = {0};

  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  //USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);

  //LL_USART_ConfigAsyncMode(USART1); 
  
  LL_USART_EnableIT_IDLE(USART1);
  LL_USART_EnableDMAReq_RX(USART1);


  // DMA
  LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_5, 
                        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | 
                        LL_DMA_PRIORITY_LOW               | 
                        LL_DMA_MODE_CIRCULAR              | 
                        LL_DMA_PERIPH_NOINCREMENT         | 
                        LL_DMA_MEMORY_INCREMENT           | 
                        LL_DMA_PDATAALIGN_BYTE            | 
                        LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_5,
                         LL_USART_DMA_GetRegAddr(USART1),
                         (uint32_t)rx_dma_buffer,
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_5, RX_BUF_SIZE);

  LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_5);
  
  LL_USART_Enable(USART1); 
}

///--------------------------------------------------------------------------------------

static void Process_And_Send(uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        //  a-z to A-Z
        if (data[i] >= 'a' && data[i] <= 'z') {
            data[i] = data[i] - 32; 
        }
        
        // Send to UART 
        while (!LL_USART_IsActiveFlag_TXE(USART1));
        LL_USART_TransmitData8(USART1, data[i]);
    }
    //while (!LL_USART_IsActiveFlag_TXE(USART1));
    //LL_USART_TransmitData8(USART1, '\r');
    //while (!LL_USART_IsActiveFlag_TXE(USART1));
    //LL_USART_TransmitData8(USART1, '\n');
}



static void Append_To_Line_Buffer(uint8_t* source, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) 
    {
        char ch = source[i];
        // buffer overflow protection
        if (cmd_idx < (CMD_LINE_SIZE - 1)) {
            cmd_buffer[cmd_idx++] = ch;
        }
        
        // End of line found
        if (ch == '\n' || ch == '\r') 
        {
            cmd_buffer[cmd_idx] = '\0'; 
            line_ready = 2;             // reset in main
            //  don't reset cmd_idx here so that main has time to read the data safely
        }
    }
    if (line_ready == 2) line_ready=1;
}



///--------------------------------------------------------------------------------------
/*
void dma_init(void)
{
  
  /// DMA controller clock enable 
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
  /// DMA Config 
  LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_2, LL_DMA_CHANNEL_4);
  LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_2, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_2, LL_DMA_PRIORITY_LOW);
  LL_DMA_SetMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MODE_CIRCULAR);
  LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_PERIPH_NOINCREMENT);
  LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MEMORY_INCREMENT);
  LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_2, LL_DMA_PDATAALIGN_BYTE);
  LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_2, LL_DMA_MDATAALIGN_BYTE);
  LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_2);
  /// DMA memory 
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_2, 
                         LL_USART_DMA_GetRegAddr(USART1), 
                         (uint32_t)rx_dma_buffer, 
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  
  LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_2, RX_BUF_SIZE);

  /// Enable DMA and interrupt 
  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_2);
  LL_USART_EnableDMAReq_RX(USART1);

  
}
*/


///--------------------------------------------------------------------------------------


void USART1_IRQHandler(void) {
    if (LL_USART_IsActiveFlag_IDLE(USART1)) {
        // Reset IDLE Flag (resd SR then DR)
        LL_USART_ClearFlag_IDLE(USART1);
        
        // calc of bytes
        uint32_t curr_pos = RX_BUF_SIZE - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_5);
        
        if (curr_pos != last_pos) {
          if (curr_pos > last_pos) {
                Append_To_Line_Buffer(&rx_dma_buffer[last_pos], curr_pos - last_pos);
                //Process_And_Send(&rx_dma_buffer[last_pos], curr_pos - last_pos);
            } 
            else {
                // 1. End of buf
                //Process_And_Send(&rx_dma_buffer[last_pos], RX_BUF_SIZE - last_pos);
                // 2. begin
                //Process_And_Send(&rx_dma_buffer[0], curr_pos);
                Append_To_Line_Buffer(&rx_dma_buffer[last_pos], RX_BUF_SIZE - last_pos);
                if (curr_pos > 0) {
                    Append_To_Line_Buffer(&rx_dma_buffer[0], curr_pos);
                } 
            }
        }
        last_pos = curr_pos; 
        
    }
}







//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


void UART_SendString(const char* str) {
    while (*str) {
        while (!LL_USART_IsActiveFlag_TXE(USART1));
        LL_USART_TransmitData8(USART1, *str++);
    }
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
