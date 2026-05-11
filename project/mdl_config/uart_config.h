#ifndef _UART_CONFIG_H_
#define _UART_CONFIG_H_


//-- GPIO CLOCK --//
#define CONF_UART_GPIO_IsEnabledClock           LL_AHB1_GRP1_IsEnabledClock
#define CONF_UART_GPIO_EnableClock              LL_AHB1_GRP1_EnableClock
//-- USART CLOCKS --//
#define CONF_UART_IsEnabledClock                LL_APB2_GRP1_IsEnabledClock
#define CONF_UART_EnableClock                   LL_APB2_GRP1_EnableClock
//#define CONF_UART_ClockSource                   LL_RCC_SetUSARTClockSource

//--- gpio uart1 config ---//
#define CONF_UART1_GPIO_TX_BUS                  LL_AHB1_GRP1_PERIPH_GPIOA
#define CONF_UART1_GPIO_RX_BUS                  LL_AHB1_GRP1_PERIPH_GPIOA
#define CONF_UART1_GPIO_TX_PORT                 GPIOA 
#define CONF_UART1_GPIO_RX_PORT                 GPIOA
#define CONF_UART1_GPIO_TX_AF                   LL_GPIO_AF_7
#define CONF_UART1_GPIO_RX_AF                   LL_GPIO_AF_7
#define CONF_UART1_GPIO_TX_PIN                  LL_GPIO_PIN_9
#define CONF_UART1_GPIO_RX_PIN                  LL_GPIO_PIN_10
//--- uart1 config ---//
#define CONF_UART1_BUS                          LL_APB2_GRP1_PERIPH_USART1
//#define CONF_UART1_CLK_SOURSE                   LL_RCC_USART2_CLKSOURCE_PCLK1
//#define CONF_UART1_PRESCALER                    LL_USART_PRESCALER_DIV1
#define CONF_UART1_PORT                         USART1 
#define CONF_UART1_RATE                         ((uint32_t)115200)
#define CONF_UART1_DATA_WIDTH                   LL_USART_DATAWIDTH_8B
#define CONF_UART1_STOP_BITS                    LL_USART_STOPBITS_1
#define CONF_UART1_PARITY                       LL_USART_PARITY_NONE
#define CONF_UART1_DIRECTION                    LL_USART_DIRECTION_TX_RX

//--- uart1 interrupts ---//
#define uart1_IRQ_handler                       USART1_IRQHandler
#define uart_IRQ_vector                         USART1_IRQn


#endif /* _UART_CONFIG_H_ */

