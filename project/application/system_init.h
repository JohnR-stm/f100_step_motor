/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#ifndef _SYSTEM_INIT_H_
#define _SYSTEM_INIT_H_

#include <stdint.h>

#define SYS_FREQ        16000000

#define SYSTICK_PRIO    3UL

#define PEND_SV_PRIORITY  15
#define SYS_TICK_PRIORITY 15


#ifndef NVIC_PRIORITYGROUP_0
#define NVIC_PRIORITYGROUP_0         ((uint32_t)0x00000007) /*!< 0 bit  for pre-emption priority,
                                                                 4 bits for subpriority */
#define NVIC_PRIORITYGROUP_1         ((uint32_t)0x00000006) /*!< 1 bit  for pre-emption priority,
                                                                 3 bits for subpriority */
#define NVIC_PRIORITYGROUP_2         ((uint32_t)0x00000005) /*!< 2 bits for pre-emption priority,
                                                                 2 bits for subpriority */
#define NVIC_PRIORITYGROUP_3         ((uint32_t)0x00000004) /*!< 3 bits for pre-emption priority,
                                                                 1 bit  for subpriority */
#define NVIC_PRIORITYGROUP_4         ((uint32_t)0x00000003) /*!< 4 bits for pre-emption priority,
                                                                 0 bit  for subpriority */
#endif




void system_clock_config(void);
void nvic_priority_config(void);
void system_delay(uint32_t delay);
void MX_SysTick_Init(void);



#endif /* _SYSTEM_INIT_H_ */