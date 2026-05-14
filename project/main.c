/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "system_init.h"
//#include "uart.h"
//#include "led_hw.h"



//#include "stm32f1xx_ll_rcc.h"
//#include "stm32f1xx_ll_utils.h" 
#include "stm32f1xx_ll_bus.h"
#include "stm32f1xx_ll_gpio.h"
#include "stm32f1xx_ll_tim.h"


#include "app_uart.h"


//----------------------------------------------------------------------------

static void leds_init(void);
static void MX_TIM3_Stepper_Init(uint16_t initial_arr);

static void Execute_Move(int32_t target_x_mm, int32_t v_mm_s);
static void Execute_LED(uint8_t state);
static void exec(void);



char string_A[] = "Hello, I'm STM32G070! \r\n";
char string_B[] = "I am glad to see you! \r\n";


MainState_t current_state = STATE_WAIT_PC_READY;

volatile uint32_t wait_timeout_ms = 0;
volatile uint8_t wait_active = 0;


volatile uint16_t cmd_idx = 0;
volatile uint8_t line_ready = 0; // Flag for 3 algoritm


char cmd_buffer[CMD_LINE_SIZE];

Command_t current_cmd = {CMD_NONE, 0, 0, 0, 0};



//----------------------------------------------------------------------------
// STEPPER MOTOR
//----------------------------------------------------------------------------

#define STEPS_PER_MM      400       // Number of motor microsteps per 1 mm of linear travel
#define TIM3_FREQ         1000000   // TIM3 clock frequency (1 MHz, 1 tick = 1 us)

// Physical axis state variables
volatile int32_t current_pos_steps = 0; // Current position in STEPS relative to zero homing
volatile uint8_t movement_active = 0;   // Axis busy flag (monitored by the parser state machine)

// Motion profile parameters for the current command execution
volatile int32_t total_steps = 0;       // Total steps required for the current movement
volatile int32_t step_count = 0;        // Step counter tracker (ranges from 0 to total_steps)
volatile int32_t accel_steps = 0;       // Number of steps allocated for the acceleration phase
volatile int32_t decel_start_step = 0;  // Step index where deceleration phase must begin
volatile int32_t dir_sign = 1;          // Direction vector indicator (+1 for forward, -1 for reverse)

// Speed and period calculation variables
volatile uint32_t current_period = 0;   // Dynamic ARR value applied to the timer register
volatile uint32_t min_period = 0;       // Minimum target ARR value corresponding to maximum speed 'v'
volatile uint32_t accel_step_inc = 0;   // Fixed period step increment value for linear ramp approximation






//----------------------------------------------------------------------------
// MAIN
//----------------------------------------------------------------------------

int main(void)
{
  system_clock_config();
  leds_init();
  MX_TIM3_Stepper_Init(1000);
  uart_init_all();
    
 
  while (1)
  {
   exec();
   system_delay(10);
   //LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_9);
   
   //system_delay(100);
   //LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_8);
   }
}


//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------


static void leds_init(void)
{
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOC);
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = LL_GPIO_PIN_8 | LL_GPIO_PIN_9; 
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;          
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;      
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL; 
  GPIO_InitStruct.Pull = LL_GPIO_PULL_DOWN;           
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

//----------------------------------------------------------------------------
// TIMER
//----------------------------------------------------------------------------

static void MX_TIM3_Stepper_Init(uint16_t initial_arr)
{
  LL_TIM_InitTypeDef TIM_InitStruct = {0};
  LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);

  // OC PA6 (TIM3_CH1)
  GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // TIMER 3
  TIM_InitStruct.Prescaler = 1500; 
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = initial_arr;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  LL_TIM_Init(TIM3, &TIM_InitStruct);

  LL_TIM_EnableARRPreload(TIM3);

  // 4.  Output Compare (Toggle)
  TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_TOGGLE;
  TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_ENABLE;
  TIM_OC_InitStruct.CompareValue = 0; 
  TIM_OC_InitStruct.OCPolarity = LL_TIM_OCPOLARITY_HIGH;
  LL_TIM_OC_Init(TIM3, LL_TIM_CHANNEL_CH1, &TIM_OC_InitStruct);
  
  LL_TIM_OC_DisablePreload(TIM3, LL_TIM_CHANNEL_CH1);

  // 5. Interrupts
  NVIC_SetPriority(TIM3_IRQn, 0); // high priority
  NVIC_EnableIRQ(TIM3_IRQn);
  LL_TIM_EnableIT_UPDATE(TIM3); //  (Update)

  LL_TIM_EnableCounter(TIM3);
}



void TIM3_IRQHandler(void)
{
    if (LL_TIM_IsActiveFlag_UPDATE(TIM3))
    {
        LL_TIM_ClearFlag_UPDATE(TIM3);
        
        if (!movement_active) {
            LL_TIM_DisableCounter(TIM3);
            return;
        }
        
        // 1. Increment step tracker and update global physical coordinates
        step_count++;
        current_pos_steps += dir_sign;
        
        // 2. Evaluate if current move profile execution is complete
        if (step_count >= total_steps) {
            LL_TIM_DisableCounter(TIM3);
            LL_TIM_DisableIT_UPDATE(TIM3);
            movement_active = 0; // Release execution flag to trigger next line fetch in main loop
            return;
        }
        
        // 3. Re-calculate dynamic ARR reload interval for the upcoming step
        if (step_count < accel_steps) {
            // ACCELERATION RAMP: decrease step period to increase physical motor speed
            if (current_period > min_period + accel_step_inc) {
                current_period -= accel_step_inc;
            } else {
                current_period = min_period;
            }
        } 
        else if (step_count >= decel_start_step) {
            // DECELERATION RAMP: increase step period to smoothly decelerate to zero
            current_period += accel_step_inc;
        } 
        else {
            // CONSTANT VELOCITY CRUISE PHASE
            current_period = min_period;
        }
        
        // 4. Commit updated period profiles to the active hardware timer registers
        LL_TIM_SetAutoReload(TIM3, current_period);
        LL_TIM_OC_SetCompareCH1(TIM3, current_period / 2); // Maintain 50% square wave duty cycle
    }
}


//----------------------------------------------------------------------------
//System TIMER
//----------------------------------------------------------------------------



void SysTick_Timer_Callback(void) {
    if (wait_active && wait_timeout_ms > 0) {
        wait_timeout_ms--;
        if (wait_timeout_ms == 0) {
            wait_active = 0; // ????? ???????, ???? ???????
        }
    }
}


//----------------------------------------------------------------------------
//  F U N C T I O N S
//----------------------------------------------------------------------------


void Execute_Move(int32_t target_x_mm, int32_t v_mm_s) {
    uint32_t accel_mm_s2 = 100; // Example acceleration profile: 100 mm/s^2
    
    // 1. Convert the target position from millimeters to physical steps
    int32_t target_pos_steps = target_x_mm * STEPS_PER_MM;
    
    // 2. Calculate linear distance and determine direction vector
    int32_t distance_steps = target_pos_steps - current_pos_steps;
    if (distance_steps == 0) {
        movement_active = 0;
        return; // Target position reached, skip processing
    }
    
    if (distance_steps > 0) {
        dir_sign = 1;
        LL_GPIO_SetOutputPin(GPIOC, LL_GPIO_PIN_10); // Forward direction
    } else {
        dir_sign = -1;
        LL_GPIO_ResetOutputPin(GPIOC, LL_GPIO_PIN_10); // Reverse direction
        distance_steps = -distance_steps; // Work with absolute value for step processing
    }
    
    total_steps = distance_steps;
    step_count = 0;
    
    // 3. Integer-based calculation of velocity parameters and ARR limits
    // Max step frequency (Steps/s) = v * STEPS_PER_MM
    uint32_t max_step_freq = v_mm_s * STEPS_PER_MM;
    min_period = TIM3_FREQ / max_step_freq; // Minimum ARR required for peak velocity
    
    // Calculate steps needed to accelerate to velocity V with acceleration A:
    // Equation: S_steps = (v_mm_s^2 * STEPS_PER_MM) / (2 * accel_mm_s2)
    accel_steps = (v_mm_s * v_mm_s * STEPS_PER_MM) / (2 * accel_mm_s2);
    
    // Handle short moves where full target velocity cannot be achieved (triangle profile)
    if (accel_steps * 2 > total_steps) {
        accel_steps = total_steps / 2;
    }
    
    decel_start_step = total_steps - accel_steps;
    
    // Set a safe initial period for near-zero starting velocity (e.g., 20000 ticks = 50 Hz)
    current_period = 20000; 
    
    // Calculate the linear period increment per step for ramp approximation
    if (accel_steps > 0) {
        accel_step_inc = (current_period - min_period) / accel_steps;
    } else {
        accel_step_inc = 0;
        current_period = min_period;
    }
    
    // 4. Configure and activate TIM3 peripheral registers
    movement_active = 1;
    
    LL_TIM_SetAutoReload(TIM3, current_period);
    LL_TIM_OC_SetCompareCH1(TIM3, current_period / 2); // Set a stable 50% duty cycle for the stepper driver
    
    LL_TIM_EnableIT_UPDATE(TIM3);
    LL_TIM_EnableCounter(TIM3);
}


//----------------------------------------------------------------------------
//  T I M E R
//----------------------------------------------------------------------------



static void Execute_LED(uint8_t state) {
    if (state) {
      //LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_9);
        LL_GPIO_SetOutputPin(GPIOC, LL_GPIO_PIN_9); 
    } else {
        LL_GPIO_ResetOutputPin(GPIOC, LL_GPIO_PIN_9);
    }
}


//----------------------------------------------------------------------------
//Execute func
//----------------------------------------------------------------------------

static void exec(void)
{
  switch (current_state) 
  {
 //---- S T E P  0 ---------------------------------//
    case STATE_WAIT_PC_READY: 
      if (line_ready) 
      {
        if (strncmp(cmd_buffer, "Ready", 5) == 0) 
        {
          current_state = STATE_REQUEST_CMD;  
        }
        cmd_idx = 0;
        line_ready = 0;
      }
    break;
 //---- S T E P  1 ---------------------------------//
    case STATE_REQUEST_CMD: 
      UART_SendString("Read comm \r\n");
      current_state = STATE_WAIT_CMD;
    break;
 //---- S T E P S 2-3 ------------------------------//
    case STATE_WAIT_CMD: 
      if (line_ready) 
      {
        current_state = STATE_EXECUTE_CMD; 
      }
    break;
 //---- S T E P  4 ---------------------------------//
    case STATE_EXECUTE_CMD: 
      cmd_buffer[strcspn(cmd_buffer, "\r\n")] = 0;
      current_cmd.type = CMD_NONE;
      if (strncmp(cmd_buffer, "MV ", 3) == 0) 
      {
        if (sscanf(cmd_buffer, "MV %ld %ld", &current_cmd.x, &current_cmd.v) == 2) 
        {
          current_cmd.type = CMD_MV;
          Execute_Move(current_cmd.x, current_cmd.v);
        }
      }
      else if (strncmp(cmd_buffer, "LED ", 4) == 0) 
      {
        current_cmd.type = CMD_LED;
        if (strstr(cmd_buffer, "ON"))  current_cmd.led = 1;
        if (strstr(cmd_buffer, "OFF")) current_cmd.led = 0;
        Execute_LED(current_cmd.led);
      }
      else if (strncmp(cmd_buffer, "WT ", 3) == 0) 
      {
        uint32_t seconds = 0;
        if (sscanf(cmd_buffer, "WT %ld", &seconds) == 1) 
        {
          current_cmd.type = CMD_WT;
          wait_timeout_ms = seconds * 1000;
          wait_active = 1; // Reset (activate) the wait flag
        }
      }
      else if (strcmp(cmd_buffer, "END") == 0) 
      {
        current_cmd.type = CMD_END;
        current_state = STATE_END; // Step 5: Block item 1 permanently
        UART_SendString("All commands executed successfully\r\n");
      }
      cmd_idx = 0;
      line_ready = 0;
      if (current_state != STATE_END) 
      {
        current_state = STATE_WAIT_CMD; 
        while (movement_active || wait_active) 
        {
          // Emergency buttons/limit switches can be processed here
          // The movement_active flag must be cleared in the interrupt
        }
        current_state = STATE_REQUEST_CMD; 
      }
    break;
    
    case STATE_END:
      // UART_SendString("All commands executed successfully\r\n");
      //while(1); 
    break;
  }
}



//----------------------------------------------------------------------------
//CRC
//----------------------------------------------------------------------------

/*
static void MX_CRC_Init(void)
{

  // Peripheral clock enable 
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CRC);

  // USER CODE BEGIN CRC_Init 1 

  // USER CODE END CRC_Init 1 
  LL_CRC_SetInputDataReverseMode(CRC, LL_CRC_INDATA_REVERSE_NONE);
  LL_CRC_SetOutputDataReverseMode(CRC, LL_CRC_OUTDATA_REVERSE_NONE);
  LL_CRC_SetPolynomialCoef(CRC, LL_CRC_DEFAULT_CRC32_POLY);
  LL_CRC_SetPolynomialSize(CRC, LL_CRC_POLYLENGTH_32B);
  LL_CRC_SetInitialData(CRC, LL_CRC_DEFAULT_CRC_INITVALUE);

}
*/
//----------------------------------------------------------------------------
// SPI2 + DMA
//----------------------------------------------------------------------------

/*
static void MX_SPI2_Init(void)
{


  LL_SPI_InitTypeDef SPI_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Peripheral clock enable 
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  //SPI2 GPIO Configuration
  //PC2   ------> SPI2_MISO
  //PC3   ------> SPI2_MOSI
  //PA0   ------> SPI2_SCK
  //
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // SPI2 DMA Init 

  // SPI2_RX Init 
  LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_1, LL_DMAMUX_REQ_SPI2_RX);

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_1, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MODE_NORMAL);

  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MDATAALIGN_BYTE);




  // SPI2 parameter configuration
  SPI_InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
  SPI_InitStruct.Mode = LL_SPI_MODE_MASTER;
  SPI_InitStruct.DataWidth = LL_SPI_DATAWIDTH_4BIT;
  SPI_InitStruct.ClockPolarity = LL_SPI_POLARITY_LOW;
  SPI_InitStruct.ClockPhase = LL_SPI_PHASE_1EDGE;
  SPI_InitStruct.NSS = LL_SPI_NSS_SOFT;
  SPI_InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV2;
  SPI_InitStruct.BitOrder = LL_SPI_MSB_FIRST;
  SPI_InitStruct.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
  SPI_InitStruct.CRCPoly = 7;
  LL_SPI_Init(SPI2, &SPI_InitStruct);
  LL_SPI_SetStandard(SPI2, LL_SPI_PROTOCOL_MOTOROLA);
  LL_SPI_EnableNSSPulseMgt(SPI2);


}

*/

//----------------------------------------------------------------------------
// TIM6
//----------------------------------------------------------------------------

/*
static void MX_TIM6_Init(void)
{


  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  // Peripheral clock enable 
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);


  TIM_InitStruct.Prescaler = 0;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 65535;
  LL_TIM_Init(TIM6, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM6);
  LL_TIM_SetTriggerOutput(TIM6, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM6);
}
*/


//----------------------------------------------------------------------------
// USART
//----------------------------------------------------------------------------

/*
static void MX_USART2_UART_Init(void)
{



  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetUSARTClockSource(LL_RCC_USART2_CLKSOURCE_PCLK1);

  // Peripheral clock enable 
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  //USART2 GPIO Configuration
  //PA2   ------> USART2_TX
  //PA3   ------> USART2_RX
  //
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);


  USART_InitStruct.PrescalerValue = LL_USART_PRESCALER_DIV1;
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_7B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART2, &USART_InitStruct);
  LL_USART_SetTXFIFOThreshold(USART2, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_SetRXFIFOThreshold(USART2, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_DisableFIFO(USART2);
  LL_USART_ConfigAsyncMode(USART2);



  LL_USART_Enable(USART2);

  // Polling USART2 initialisation 
  while((!(LL_USART_IsActiveFlag_TEACK(USART2))) || (!(LL_USART_IsActiveFlag_REACK(USART2))))
  {
  }


}
*/

//----------------------------------------------------------------------------
// DMA
//----------------------------------------------------------------------------

/*
static void MX_DMA_Init(void)
{

  // Init with LL driver 
  // DMA controller clock enable 
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  // DMA interrupt init 
  // DMA1_Channel1_IRQn interrupt configuration 
  NVIC_SetPriority(DMA1_Channel1_IRQn, 0);
  NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}
*/




