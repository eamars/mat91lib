/** @file   usart.h
    @author M. P. Hayes, UCECE
    @date   21 June 2007
    @brief  Unbuffered USART interface.
*/
#ifndef USART_H
#define USART_H

#include "config.h"
#include "usart0.h"


/** usart configuration structure.  */
typedef struct
{
    /* 0 for USART0, 1 for USART1.  */
    uint8_t channel;
    /* Baud rate.  */
    uint32_t baud_rate;
    /* Baud rate divisor (this is used if baud_rate is zero).  */
    uint32_t baud_divisor;
    /* Non-zero for blocking I/O.  */
    bool block;
}
usart_cfg_t;

typedef struct usart_dev_struct usart_dev_t;

typedef usart_dev_t *usart_t;

#define USART_BAUD_DIVISOR(BAUD_RATE) USART0_BAUD_DIVISOR(BAUD_RATE)


/* Initialise UART for desired channel, baud rate, etc.  */
usart_t 
usart_init (const usart_cfg_t *cfg);


/** Return non-zero if there is a character ready to be read without blocking.  */
bool
usart_read_ready_p (usart_t usart);


/** Return non-zero if a character can be written without blocking.  */
bool
usart_write_ready_p (usart_t usart);


/** Read character.  */
int
usart_getc (usart_t usart);


/** Write character.  */
int
usart_putc (usart_t usart, char ch);


/** Write string.  */
int
usart_puts (usart_t usart, const char *str);


/** Read size bytes.  */
int16_t
usart_read (usart_t usart, void *data, uint16_t size);


/** Write size bytes.  */
int16_t
usart_write (usart_t usart, const void *data, uint16_t size);


/* Shutdown UART to save power.  */
void
usart_shutdown (usart_t usart);

#endif
