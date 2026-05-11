/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdint.h>
#include <stdio.h>
#include "app_uart.h"
#include "led_hw.h"

#include "uart.h"

static char string1[] = "Hello! I RECEIVED YOUR MESSAGE! \r\n";


//-----------------------------------------------------------------------------
// uart1 HANDLER
//-----------------------------------------------------------------------------

void uart1_handler(void)
{
  uint8_t val = uart1_read_byte(); // read byte
  
  if (val == 'v')
    uart1_send_string(string1);  
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
