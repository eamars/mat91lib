/** @file   uart1.h
    @author Michael Hayes
    @date   27 May 2015
    @brief Routines for interfacing with the uart1 on an AT91 ARM
*/

#ifndef UART1_H
#define UART1_H

#include "config.h"


#define UART1_BAUD_DIVISOR(BAUD_RATE)  ((F_CPU / 16) / (BAUD_RATE))


/* Return non-zero if there is a character ready to be read.  */
bool
uart1_read_ready_p (void);

/* Read character from UART1.  This blocks if nothing
   is available to read.  */
int
uart1_getc (void);

/* Return non-zero if a character can be written without blocking.  */
bool
uart1_write_ready_p (void);

/* Return non-zero if transmitter finished.  */
bool
uart1_write_finished_p (void);

/* Write character to UART1.  This returns zero if
   the character could not be written.  */
int
uart1_putc (char ch);

/* Write string to UART1.  */
int
uart1_puts (const char *str);

/* Initialise UART1 and set baud rate.  */
int
uart1_init (uint16_t baud_divisor);


/* Shutdown UART1 in preparation for sleep.  */
void
uart1_shutdown (void);
#endif
