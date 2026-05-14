/**
 * Copyright (c) 2026 IvanRomanov
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#ifndef _APP_UART_H_
#define _APP_UART_H_


#define RX_BUF_SIZE 64
#define CMD_LINE_SIZE 64


typedef enum {
    STATE_WAIT_PC_READY, // wait "Ready" from PC
    STATE_REQUEST_CMD,   // Step 1 send "Read comm \r\n"
    STATE_WAIT_CMD,      // Steps 2-3: waiting for the string
    STATE_EXECUTE_CMD,   // Step 4: Parsing
    STATE_PROCESSING,    // execute functions
    STATE_END            // Stop
} MainState_t;

typedef enum { CMD_NONE, CMD_MV, CMD_LED, CMD_WT, CMD_END } CmdType_t;

typedef struct {
    CmdType_t type;
    int32_t x;          // Coordinate
    int32_t v;          // velocity
    uint8_t led;        // 1 - ON, 0 - OFF
    uint32_t delay_ms;  // wait time ms
} Command_t;

void uart_init_all(void);
void UART_SendString(const char* str);


///-----------------------------------------------------

///--- direction ---///
typedef enum{
  DSBL = 0,
  FORW,
  REV,
  ENBL
} direction_t;



typedef struct {
  uint32_t steps_X;
  uint32_t accelerate_until;
  uint32_t decelerate_after;
  direction_t dir_X;  
} block_t;

///-----------------------------------------------------


   
//void init_queue(void);


#endif /* _APP_UART_H_ */

