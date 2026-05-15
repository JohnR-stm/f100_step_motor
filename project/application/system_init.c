/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */



#include "stm32f100xb.h"
#include "stm32f1xx_ll_rcc.h"
#include "stm32f1xx_ll_system.h" // FLASH
#include "stm32f1xx_ll_utils.h" 
#include "stm32f1xx_ll_bus.h"
#include "system_init.h"

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

void system_clock_config(void)
{
  
  //LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  //LL_PWR_EnableOverDriveMode();
  
  //--- HSE ---//
  // LL_RCC_HSE_EnableBypass(); ???
  LL_RCC_HSE_Enable();
  //--- Wait till HSE is ready ---//
  while(LL_RCC_HSE_IsReady() != 1)
  {  }
  LL_RCC_HSE_EnableCSS();
  
  //--- PLL ---//
  //LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 50, LL_RCC_PLLP_DIV_2);
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE_DIV_2, LL_RCC_PLL_MUL_4);
  LL_RCC_PLL_Enable();
  //--- Wait till PLL is ready ---//
  while(LL_RCC_PLL_IsReady() != 1)
  {  }
   
  //--- Prescalers ---//
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  //--- System MUX ---//
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

  //--- Wait till System clock is ready ---//
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
  {  }
  
  //--- timer delay in utils.h ---//
  LL_Init1msTick(SYS_FREQ);
  LL_SetSystemCoreClock(SYS_FREQ);
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

//--- 10 ms --////
void MX_SysTick_Init(void)
{
  LL_SetSystemCoreClock(SYS_FREQ);
  
  if (SysTick_Config(SYS_FREQ / 100UL))
  {
    while (1);
  }
  NVIC_SetPriority(SysTick_IRQn, SYSTICK_PRIO);
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


void nvic_priority_config(void)
{
  NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
  NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),PEND_SV_PRIORITY, 0));
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),SYS_TICK_PRIORITY, 0));
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

void system_delay(uint32_t delay)
{
  LL_mDelay(delay);
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


