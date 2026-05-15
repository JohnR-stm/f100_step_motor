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
static void MX_TIM3_InterruptsEnable(void);

static void Execute_Move(int32_t target_x_mm, int32_t nom_speed, block_t * block);
static void Execute_LED(uint8_t state);
static void exec(void);

static void SysTick_delay(void);


void motor_X_dir(direction_t dir);

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
//----------------------------------------------------------------------------
//----  PUL  ---- DIR ----- EN  ------------------------------------------
//----  PA6 ----- PC8 ----
//-----------------------------------------------------------------------------

// Number of motor microsteps per 1 mm of linear travel
//#define STEPS_PER_MM      400  
// TIM3 clock frequency (1 MHz, 1 tick = 1 us)
//#define TIM3_FREQ         1000000   

#define MIN_SPEED       2.0f
#define MAX_SPEED       60.0f
#define TIM_KOEFF       ((float)2000000.0f)

static float prev_X = 0;
static int32_t stp_glob_X = 0;

const float min_period = 0.01f;
const float accel = 100.0f;
const float accel_rev = 1.0f/(2.0f * 100.0f);
float period = 0.01f;
float vel = MIN_SPEED;        /// Start velocity

const float len_stp_x = 0.04f;        //mm/step
const float stp_len_x = 1.0f / 0.04f; 


block_t Block = {0, 0, 0, DSBL};

volatile int32_t step_count = 0; 
volatile uint8_t movement_active = 0;

// Physical axis state variables
//volatile int32_t current_pos_steps = 0; // Current position in STEPS relative to zero homing
//volatile uint8_t movement_active = 0;   // Axis busy flag (monitored by the parser state machine)

// Motion profile parameters for the current command execution
//volatile int32_t total_steps = 0;       // Total steps required for the current movement
//volatile int32_t step_count = 0;        // Step counter tracker (ranges from 0 to total_steps)
//volatile int32_t accel_steps = 0;       // Number of steps allocated for the acceleration phase
//volatile int32_t decel_start_step = 0;  // Step index where deceleration phase must begin
//volatile int32_t dir_sign = 1;          // Direction vector indicator (+1 for forward, -1 for reverse)

// Speed and period calculation variables
//volatile uint32_t current_period = 0;   // Dynamic ARR value applied to the timer register
//volatile uint32_t min_per = 0;       // Minimum target ARR value corresponding to maximum speed 'v'
//volatile uint32_t accel_step_inc = 0;   // Fixed period step increment value for linear ramp approximation






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
    if (wait_active == 0)
      exec();
    SysTick_delay();
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
  TIM_InitStruct.Prescaler = 16-1; 
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

    MX_TIM3_InterruptsEnable(); 
}

static void MX_TIM3_InterruptsEnable(void)
{
  NVIC_SetPriority(TIM3_IRQn, 0); // high priority
  NVIC_EnableIRQ(TIM3_IRQn);
  
  LL_TIM_ClearFlag_UPDATE(TIM3); // clear flag
  LL_TIM_EnableIT_UPDATE(TIM3);
}





void motor_X_dir(direction_t dir)
{
  if(FORW == dir)
    LL_GPIO_SetOutputPin(GPIOC, LL_GPIO_PIN_8);
  else
    LL_GPIO_ResetOutputPin(GPIOC, LL_GPIO_PIN_8);
}


void TIM3_IRQHandler(void)
{
    if (LL_TIM_IsActiveFlag_UPDATE(TIM3))
    {
        LL_TIM_ClearFlag_UPDATE(TIM3);
        
        step_count++;
        
        //--- direction ---///
        motor_X_dir(Block.dir_X);
        
        
        //-- calc speed, period ---///
        if (step_count < Block.accelerate_until)
          vel = vel + accel * period;
        else
        if (step_count > Block.decelerate_after)
          vel = vel - accel * period;
        
        if(vel < MIN_SPEED)
          vel = MIN_SPEED;
        if(vel > MAX_SPEED)
          vel = MAX_SPEED;
        
        period = len_stp_x/vel;
        
        //--- set timer period ---//
        LL_TIM_SetAutoReload(TIM3, (uint32_t)(period * TIM_KOEFF));
        
        //--- OFF TIMER ---///
        if (step_count >= Block.steps_X) 
        {
          step_count = 0;
          LL_TIM_DisableCounter(TIM3);
          //LL_TIM_DisableIT_UPDATE(TIM3);
          movement_active = 0; // Release execution flag to trigger next line fetch in main loop
          current_state = STATE_REQUEST_CMD;
          return;
        }
        
        
         if (!movement_active) {
           // LL_TIM_DisableCounter(TIM3);
            return;
        }
    }
}


//----------------------------------------------------------------------------
//System TIMER
//----------------------------------------------------------------------------


void SysTick_delay(void)
{
  if (wait_active && wait_timeout_ms > 0) 
  {
    wait_timeout_ms--;
    if (wait_timeout_ms == 0) {
      wait_active = 0; // Reset flag
      current_state = STATE_REQUEST_CMD;
    }
  }
}

/*
void SysTick_Timer_Callback(void) {
    if (wait_active && wait_timeout_ms > 0) {
        wait_timeout_ms--;
        if (wait_timeout_ms == 0) {
            wait_active = 0; // ????? ???????, ???? ???????
            current_state = STATE_REQUEST_CMD;
        }
    }
}
*/


//----------------------------------------------------------------------------
//  F U N C T I O N S
//----------------------------------------------------------------------------
/*
static float prev_X = 0;
static int32_t stp_glob_X = 0;

float period = 0.01f;
float accel = 100.0f;
float accel_rev = 1.0f/(2.0f * accel);
float vel = MIN_SPEED;        /// Start velocity

const float len_stp_x = 0.0185f;        //mm/step
const float stp_len_x = 1.0f / 0.0185f;        //step/mm
*/

void Execute_Move(int32_t target_x_mm, int32_t nom_speed, block_t * block) 
{
  float dX = (float)target_x_mm - prev_X;         //mm
  
  ///----- steps, dir X ------------------------------------------------///
  // calculate delta_X in steps
  block->steps_X = (uint32_t)fabsf(dX * stp_len_x);
  // direction_X
  if(dX > 0){
    block->dir_X = FORW; 
    stp_glob_X += (int32_t)block->steps_X;}
  else {
    block->dir_X = REV;
    stp_glob_X -= (int32_t)block->steps_X;}
  // recalc prev_X
  if(stp_glob_X < 0) stp_glob_X = 0; 
  prev_X = (float)stp_glob_X * len_stp_x;
  
  ///----- Period ---////
// min period = const
 // block->period = steps_koef / block->entry_speed;
  
  /// --- accel steps ---///
  float accel_dist = 0; //mm
  accel_dist = (float)(nom_speed * nom_speed) * accel_rev;
  if (accel_dist*2 > dX)
    accel_dist  = dX/2;
  block->accelerate_until = (uint32_t)fabsf(accel_dist * stp_len_x);
  block->decelerate_after = block->steps_X - block->accelerate_until;
  
  vel = MIN_SPEED;
  if(vel < MIN_SPEED)
    vel = MIN_SPEED;
  if(vel > MAX_SPEED)
    vel = MAX_SPEED;
  
  period = len_stp_x/vel;
  
  //start_timer();
  
  // Configure and activate TIM3 peripheral registers
  movement_active = 1;
  
  //----  T I M E R   3   S T A R T  ----//
  LL_TIM_SetAutoReload(TIM3, (uint32_t)(period * TIM_KOEFF));
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
    current_state = STATE_REQUEST_CMD;
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
      if (line_ready == 1) 
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
      if (line_ready == 1) 
      {
        current_state = STATE_EXECUTE_CMD; 
      }
    break;
 //---- S T E P  4 ---------------------------------//
    case STATE_EXECUTE_CMD: 
      cmd_buffer[strcspn(cmd_buffer, "\r\n")] = 0; /// WTF
      current_cmd.type = CMD_NONE;
      
      ///--- M O T O R  M O V E  ---///
      if (strncmp(cmd_buffer, "MV ", 3) == 0) 
      {
        if (sscanf(cmd_buffer, "MV %ld %ld", &current_cmd.x, &current_cmd.v) == 2)  /// WTF
        {
          current_cmd.type = CMD_MV;
          current_state = STATE_PROCESSING;
          Execute_Move(current_cmd.x, current_cmd.v, &Block);
        }
      }
      
      ///--- L E D   O N / O F F  ---///
      else if (strncmp(cmd_buffer, "LED ", 4) == 0) 
      {
        current_cmd.type = CMD_LED;
        if (strstr(cmd_buffer, "ON"))  current_cmd.led = 1;
        if (strstr(cmd_buffer, "OFF")) current_cmd.led = 0;
        current_state = STATE_PROCESSING;
        Execute_LED(current_cmd.led);
      }
      
      ///---  W A I T   ---///
      else if (strncmp(cmd_buffer, "WT ", 3) == 0) 
      {
        uint32_t seconds = 0;
        if (sscanf(cmd_buffer, "WT %ld", &seconds) == 1) 
        {
          current_state = STATE_PROCESSING;
          current_cmd.type = CMD_WT;
          wait_timeout_ms = seconds * 100;
          wait_active = 1; // Set (activate) the wait flag
        }
      }
      
      ///--- E N D  -  S T O P  ---///
      else if (strcmp(cmd_buffer, "END") == 0) 
      {
        current_cmd.type = CMD_END;
        current_state = STATE_END; // Step 5: Block item 1 permanently
        UART_SendString("All commands executed successfully\r\n");
      }
      
      ///--- r e s e t   a l l  ---///
      cmd_idx = 0;
      line_ready = 0;

    break;
 //----  E  N  D   ---------------------------------//   
    case STATE_END:
      // UART_SendString("All commands executed successfully\r\n");
      //while(1); 
    break;
    
    default:
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




