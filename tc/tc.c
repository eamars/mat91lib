/** @file   tc.c
    @author M. P. Hayes
    @date   30 November 2010
    @brief  Timer counter routines for AT91SAM7 and SAM4S
    @note   Only TIOAx can be driven at present.  
*/

#include "tc.h"
#include "irq.h"
#include "pio.h"
#include "mcu.h"
#include "bits.h"
#include "pinmap.h"


/* The SAM7 has one timer/counter controller that supports three timer
   counter channels.  Each of the three TCs has a 16 bit counter with
   3 16 bit registers (RA, RB,and RC).  In waveform mode, RA and RB
   are used to drive TIOA and TIOB, respectively.  RC can be used to
   stop the timer clock to provide a one-shot.
   
   The counter can be clocked with MCK divided by 2, 8, 32, 128, and 1024.

   The 100-pin SAM4S MCUs have two timer/counter controllers.  The
   channels are enumerated TC0, TC1, TC2..., TC5 but perversely the
   controllers are also named TC0 and TC1.  This driver only supports
   TC0, TC1, and TC2.

   In Capture Mode, TIOA and TIOB are configured as inputs.  In
   Waveform Mode, TIOA is always configured to be an output and TIOB
   is an output if it is not selected to be the external trigger.
   However, this driver does not support TIOB.  

   Although the counters are only 16 bit, this driver synthesises 64 bit
   counters using overflow interrupts.   Even with a 48 MHz clock,
   these 64 bit counters will take 3000 years to overflow!  
*/

#define TC_CHANNEL(TC) ((TC) - tc_devices)


#ifdef __SAM4S__
#define TC0_BASE (TC0->TC_CHANNEL)
#define TC1_BASE (TC0->TC_CHANNEL + 1)
#define TC2_BASE (TC0->TC_CHANNEL + 2)
#else
#define TC0_BASE TC0
#define TC1_BASE TC1
#define TC2_BASE TC2
#endif


/* Define known TC pins.  */
static const pinmap_t tc_pins[] = 
{
    {0, TIOA0_PIO, TIOA0_PERIPH, 0}, /* TIOA0 */
    {1, TIOA1_PIO, TIOA1_PERIPH, 0}, /* TIOA1 */
    {2, TIOA2_PIO, TIOA2_PERIPH, 0}  /* TIOA2 */
};
#define TC_PINS_NUM ARRAY_SIZE (tc_pins)

#define TC_DEVICES_NUM TC_PINS_NUM

static tc_dev_t tc_devices[TC_DEVICES_NUM];


static
void tc_handler (tc_t tc)
{
    uint32_t status = 0;
    
    /* When the status register is read, the capture status
       flags are cleared!  */
    status = tc->base->TC_SR;

    /* There is a race condition to fix.  This can occur 
       if an overflow occurs after the capture event but
       before the status register is read.  */
    
    if (status & TC_SR_LDRAS)
        tc->captureA = (tc->overflows << 16) | tc->base->TC_RA;

    if (status & TC_SR_LDRBS)
        tc->captureB = (tc->overflows << 16) | tc->base->TC_RB;

    if (status & TC_SR_COVFS)
        tc->overflows++;
}


static
void tc_handler0 (void)
{
    tc_handler (&tc_devices[0]);
}


static
void tc_handler1 (void)
{
    tc_handler (&tc_devices[1]);
}


static
void tc_handler2 (void)
{
    tc_handler (&tc_devices[2]);
}


tc_ret_t
tc_start (tc_t tc)
{
    /* The TC_CCR register is write only.  */
    tc->base->TC_CCR |= TC_CCR_CLKEN | TC_CCR_SWTRG; 
    return TC_OK;
}


tc_ret_t
tc_stop (tc_t tc)
{
    tc->base->TC_CCR |= TC_CCR_CLKDIS; 
    return TC_OK;
}


tc_counter_t
tc_counter_get (tc_t tc)
{
    tc_counter_t overflows;
    uint16_t counter_value;

    /* Unfortunately the hardware counter is only 16 bits.  We try to
       synthesise a 64 bit counter by counting overflows.  The
       implementation gets tricky due two different race conditions.

       The first is due to non-atomic reading of the 64 bit
       tc->overflows.  This is avoided by reading tc->overflows in a
       critical section.

       The second is due to the non-atomic reading of the counter
       value and reading of the status register to determine an
       overflow.  This could be avoided by pausing the counter but
       this will drop counts every time this function is read.  */

    /* Disable interrupts to ensure that reading tc->overflows is atomic.  */
    irq_global_disable ();
    overflows = tc->overflows;

    /* Read counter value.  */
    counter_value = tc->base->TC_CV;

    /* Check for overflows and service pending interrups.  */
    tc_handler (tc);

    if (overflows != tc->overflows)
    {
        /* An overflow has occurred since disabling of interrupts.
           There are three cases:
           Case 1.  The overflow occured before reading the counter
           value.  This case can be detected by reading a small value.

           Case 2.  The overflow occured after reading the counter.
           This case can be detected by reading a large value. 

           Case 3.  Another interrupt handler has hogged the CPU
           for at least half the counter rollover period.   This 
           is avoided by globally disabling interrupts.
        */
        if (counter_value < 32768)
            overflows++;
    }

    irq_global_enable ();

    return (overflows << 16) | counter_value;
}


tc_counter_t
tc_capture_get (tc_t tc, tc_capture_t reg)
{
    switch (reg)
    {
    case TC_CAPTURE_A:
        return tc->captureA;

    case TC_CAPTURE_B:
        return tc->captureB;
        
    default:
        return 0;
    }
}


/** Poll to see if the capture registers have been loaded.  */
tc_capture_mask_t
tc_capture_poll (tc_t tc)
{
    uint32_t status = 0;
    int return_val = 0;
    
    /* When the status register is read, the capture status
       flags are cleared!  */
    status = tc->base->TC_SR;
    
    if (status & TC_SR_LDRAS)
    {
        tc->captureA = tc->base->TC_RA;
        return_val |= BIT (TC_CAPTURE_A);
    }
    
    if (status & TC_SR_LDRBS)
    {
        tc->captureB = tc->base->TC_RB;
        return_val |= BIT (TC_CAPTURE_B);
    }
    
    return return_val;
}


static tc_ret_t
tc_output_set (tc_t tc)
{
    if ((tc->mode == TC_MODE_ADC) || (tc->mode == TC_MODE_COUNTER)
        || (tc->mode == TC_MODE_INTERRUPT))
        
        return TC_OK;

    /* Generate a software trigger with the clock stopped to set TIOAx
       to desired state.  Note, the clock is stopped.  */
    tc->base->TC_CCR |= TC_CCR_CLKDIS | TC_CCR_SWTRG; 

    /* Make timer pin TIOAx a timer output.  Perhaps we could use
       different logical timer channels to generate pulses on TIOBx
       pins?  */
    switch (TC_CHANNEL (tc))
    {
    case TC_CHANNEL_0:
        /* Switch to peripheral B and disable pin as PIO.  */
        pio_config_set (TIOA0_PIO, TIOA0_PERIPH);
        break;

    case TC_CHANNEL_1:
        pio_config_set (TIOA1_PIO, TIOA1_PERIPH);
        break;

    case TC_CHANNEL_2:
        pio_config_set (TIOA2_PIO, TIOA2_PERIPH);
        break;

    default:
        return TC_ERROR_CHANNEL;
    }

    return TC_OK;
}


/** Get the delay in clocks.  */
tc_period_t
tc_delay_get (tc_t tc)
{
    return tc->delay;
}


/** Get the period in clocks.  */
tc_period_t
tc_period_get (tc_t tc)
{
    return tc->period;
}


/** Set the delay in clocks.  In clock modes this sets the duty.  */
tc_period_t
tc_delay_set (tc_t tc, tc_period_t delay)
{
    tc->delay = delay;

    if (!delay)
        delay = tc->period >> 1;

    /* This register is read only when not in wave mode.  */
    tc->base->TC_RA = delay;

    return delay;
}


/** Set the period in clocks.  */
tc_period_t
tc_period_set (tc_t tc, tc_period_t period)
{
    /* This register is read only when not in wave mode.  */
    tc->base->TC_RC = period;

    tc->period = period;
    return period;
}


/** Set the TC output frequency in Hz.  This returns the actual
    frequency, closest to the desired frequency.  It also sets the
    duty factor to 0.5.  */
tc_frequency_t
tc_frequency_set (tc_t tc, tc_frequency_t frequency)
{
    tc_period_t period;

    period = TC_PERIOD_DIVISOR (frequency, tc->prescale);
    
    period = tc_period_set (tc, period);
    tc_delay_set (tc, period >> 1);

    return TC_CLOCK_FREQUENCY (tc->prescale) / period;
}


/** Configure TC with specified mode.  */
tc_ret_t
tc_mode_set (tc_t tc, tc_mode_t mode)
{
    /* Many timer counters can only generate a pulse with a single
       timer clock period.  This timer counter allows the pulse width
       to be varied.  It is specified by period - delay. 

       We configure the TC in mode WAVSEL_UP_RC.  Here the counter
       is incremented and is reset when RC matches.  The output is
       driven when RA matches.  */
    switch (mode)
    {
    case TC_MODE_ADC:
    case TC_MODE_CLOCK:
    case TC_MODE_INTERRUPT:
    case TC_MODE_PULSE:
        /* Set TIOAx when RA matches and clear TIOAx when RC matches.  */
        tc->base->TC_CMR = TC_CMR_BURST_NONE | TC_CMR_WAVE
            | TC_CMR_WAVSEL_UP_RC | TC_CMR_ACPA_SET | TC_CMR_ACPC_CLEAR
            | TC_CMR_ASWTRG_CLEAR;
        break;

    case TC_MODE_PULSE_INVERT:
        /* Clear TIOAx when RA matches and set TIOAx when RC matches.  */
        tc->base->TC_CMR = TC_CMR_BURST_NONE | TC_CMR_WAVE
            | TC_CMR_WAVSEL_UP_RC | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET
            | TC_CMR_ASWTRG_SET;
        break;

    case TC_MODE_PULSE_ONESHOT:
        /* Set TIOAx when RA matches and clear TIOAx when RC matches.
           Stop clock when RC matches.   */
        tc->base->TC_CMR = TC_CMR_BURST_NONE | TC_CMR_WAVE
            | TC_CMR_CPCSTOP | TC_CMR_WAVSEL_UP_RC
            | TC_CMR_ACPA_SET | TC_CMR_ACPC_CLEAR
            | TC_CMR_ASWTRG_CLEAR;
        break;

    case TC_MODE_PULSE_ONESHOT_INVERT:
        /* Clear TIOAx when RA matches and set TIOAx when RC matches.
           Stop clock when RC matches.  */
        tc->base->TC_CMR = TC_CMR_BURST_NONE | TC_CMR_WAVE
            | TC_CMR_CPCSTOP | TC_CMR_WAVSEL_UP_RC
            | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET
            | TC_CMR_ASWTRG_SET;
        break;

    case TC_MODE_DELAY_ONESHOT:
        /* Don't change TIOAx.  Stop clock when RC matches.  */
        tc->base->TC_CMR = TC_CMR_BURST_NONE | TC_CMR_WAVE
            | TC_CMR_CPCSTOP | TC_CMR_WAVSEL_UP_RC;
        break;

        /* In the capture modes, the time clock is not stopped or
           disabled when RB loaded.  The capture trigger can only be
           controlled by TIOAx.  Either TIOAx or TIOBx can be used to
           reset the counter (this is called external trigger).
           ABETRG = 1 specifies TIOAx for external trigger.  There are
           many possible combinations of reset and capture.  This
           driver does not support resetting of the counter.  

           The docs say that the external trigger gates the clock but
           it appears that it resets the counter.  Specifying
           TC_CMR_ETRGEDG_NONE disables this.  */

    case TC_MODE_CAPTURE_RISE_RISE:
        tc->base->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1
            | TC_CMR_LDRA_RISING | TC_CMR_LDRB_RISING
            | TC_CMR_ABETRG | TC_CMR_ETRGEDG_NONE;
        tc->base->TC_IER = TC_IER_COVFS | TC_IER_LDRAS | TC_IER_LDRBS;
        break;

    case TC_MODE_CAPTURE_RISE_FALL:
        tc->base->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1
            | TC_CMR_LDRA_RISING | TC_CMR_LDRB_FALLING
            | TC_CMR_ABETRG | TC_CMR_ETRGEDG_NONE;
        tc->base->TC_IER = TC_IER_COVFS | TC_IER_LDRAS | TC_IER_LDRBS;
        break;

    case TC_MODE_CAPTURE_FALL_RISE:
        tc->base->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1
            | TC_CMR_LDRA_FALLING | TC_CMR_LDRB_RISING
            | TC_CMR_ABETRG | TC_CMR_ETRGEDG_NONE;
        tc->base->TC_IER = TC_IER_COVFS | TC_IER_LDRAS | TC_IER_LDRBS;
        break;

    case TC_MODE_CAPTURE_FALL_FALL:
        tc->base->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1
            | TC_CMR_LDRA_FALLING | TC_CMR_LDRB_FALLING
            | TC_CMR_ABETRG | TC_CMR_ETRGEDG_NONE;
        tc->base->TC_IER = TC_IER_COVFS | TC_IER_LDRAS | TC_IER_LDRBS;
        break;

    case TC_MODE_COUNTER:
        break;
        
    default:
        return TC_ERROR_MODE;
    }

    tc->mode = mode;
    return TC_OK;
}


tc_prescale_t
tc_prescale_set (tc_t tc, tc_prescale_t prescale)
{
    /* The available prescaler values are 1, 4, 16, 64 for MCK / 2.
       Thus the effective prescaler values are 2, 8, 32, and 128.  On
       the SAM7 TIMER_CLOCK5 is MCK / 1024 but on the SAM4S it is
       SLCK.  */
    if (prescale > 32 && prescale <= 128)
    {
        tc->base->TC_CMR |= TC_CMR_TCCLKS_TIMER_CLOCK4;
        prescale = 128;
    }
    else if (prescale > 8 && prescale <= 32)
    {
        tc->base->TC_CMR |= TC_CMR_TCCLKS_TIMER_CLOCK3;
        prescale = 32;
    }
    else if (prescale > 2 && prescale <= 8)
    {
        tc->base->TC_CMR |= TC_CMR_TCCLKS_TIMER_CLOCK2;
        prescale = 8;
    }
    else
    {
        tc->base->TC_CMR |= TC_CMR_TCCLKS_TIMER_CLOCK1;
        prescale = 2;
    }

    tc->prescale = prescale;
    
    return prescale;
}


tc_ret_t
tc_config_set (tc_t tc, const tc_cfg_t *cfg)
{
    tc_ret_t ret;

    if (tc_prescale_set (tc, cfg->prescale) != cfg->prescale
        && cfg->prescale != 0)
        return TC_ERROR_PRESCALE;

    ret = tc_mode_set (tc, cfg->mode);
    if (ret < 0)
        return ret;
    
    if (cfg->frequency)
    {
        tc_frequency_set (tc, cfg->frequency);
    }
    else
    {
        tc_period_set (tc, cfg->period);
        tc_delay_set (tc, cfg->delay);
    }

    return TC_OK;
}


void
tc_shutdown (tc_t tc)
{
    /* Disable peripheral clock.  */
    mcu_pmc_disable (ID_TC0 + TC_CHANNEL (tc));

    irq_disable (ID_TC0 + TC_CHANNEL (tc));

    /* Perhaps should force TC output pin low?  Let the user decide
       how to deal with this.  */
}


tc_t 
tc_init (const tc_cfg_t *cfg)
{
    tc_t tc;
    const pinmap_t *pin;
    unsigned int i;

    pin = 0;
    for (i = 0; i < TC_PINS_NUM; i++)
    {
        pin = &tc_pins[i]; 
        if (pin->pio == cfg->pio)
            break;
    }
    if (i >= TC_PINS_NUM)
        return 0;

    tc = &tc_devices[pin->channel];

    switch (pin->channel)
    {
    case TC_CHANNEL_0:
        tc->base = TC0_BASE;
        irq_config (ID_TC0, 7, tc_handler0);
        break;

    case TC_CHANNEL_1:
        tc->base = TC1_BASE;
        irq_config (ID_TC1, 7, tc_handler1);
        break;

    case TC_CHANNEL_2:
        tc->base = TC2_BASE;
        irq_config (ID_TC2, 7, tc_handler2);
        break;
    }

    /* Enable TCx peripheral clock.  */
    mcu_pmc_enable (ID_TC0 + pin->channel);
    
    tc_config_set (tc, cfg);

    /* Configure output pin if applicable.  */
    tc_output_set (tc);

    tc->overflows = 0;

    irq_enable (ID_TC0 + TC_CHANNEL (tc));

    return tc;
}


static void
tc_clock_sync_handler (void)
{
    /* Read status register to clear interrupt.  */
    TC0_BASE->TC_SR;
    TC1_BASE->TC_SR;
    TC2_BASE->TC_SR;
}


/* Sleep for specified period.  This is useful for synchronising the
   CPU clock MCK to the timer clock, especially since the fastest
   timer clock is MCK / 2.  */
void
tc_clock_sync (tc_t tc, tc_period_t period)
{
    uint32_t id;

    /* Should set prescale to 2.  */
    tc_mode_set (tc, TC_MODE_DELAY_ONESHOT);
    tc_period_set (tc, period);

    id = ID_TC0 + TC_CHANNEL (tc);

    irq_config (id, 7, tc_clock_sync_handler);
            
    irq_enable (id);

    /* Enable interrupt when have compare on A.  */
    tc->base->TC_IER = TC_IER_CPAS;

    tc_start (tc);
    
    /* Stop CPU clock until interrupt.  FIXME, should disable other
       interrupts first. */
    mcu_cpu_idle ();

    /* Disable interrupt when have compare on A.  */
    tc->base->TC_IDR = TC_IDR_CPAS;

    irq_disable (id);

    tc_stop (tc);
}
