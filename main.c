/*
 * LiFeLight (c) 2015 Patrick Van Oosterwijck
 * Demo for LiFePO4wered/USB modules
 *
 * The board has a (boosted) LED and a touch sensor.  The idea is to be able
 * to touch-program a ten second light sequence which will be repeated every
 * ten seconds.

 * MIT Licensed.
 */

#include <msp430.h> 
#include <stdint.h>


/* Timer frequency (in MHz) */

#define     TIMER_FREQ          8

/* LED and touch pin mask */

#define     LED_PIN_MASK        0x20
#define     TOUCH_PIN_MASK      0x04

/* Unused pin mask */

#define     UNUSED_PIN_MASK     0xDB

/* LED toggle time (in us) */

#define     LED_TOGGLE_TIME     5


/* Touch capture cycles */

#define     TOUCH_CAP_CYCLES    20

/* Number of touch measurment cycles where baseline follows
 * touch level directly */

#define     START_CYCLES        40

/* Base filter multiplier and shift */

#define     BASE_FILTER_SHIFT   6
#define     BASE_FILTER_MUL     (uint32_t)((1<<BASE_FILTER_SHIFT)-1)

/* Base filter multiplier and shift for when touch is active */

#define     BASE_ACTIVE_SHIFT   10
#define     BASE_ACTIVE_MUL     (uint32_t)((1<<BASE_ACTIVE_SHIFT)-1)

/* Touch detect threshold and hysteresis */

#define     TOUCH_THRESHOLD     20
#define     TOUCH_HYSTERESIS    5

/* Define active states */

#define     TOUCH_INACTIVE      0x00
#define     TOUCH_START         0x01
#define     TOUCH_STOP          0x02
#define     TOUCH_HELD          0x03
#define     TOUCH_ACTIVE_MASK   0x01
#define     TOUCH_MASK          0x03

/* Touch structure */

struct {
    /* Touch active state variable */
    uint8_t             active;
    /* Touch capture variables used from the interrupt */
    volatile uint8_t    cap_cycle;
    volatile uint16_t   cap_start;
    /* Touch and baseline levels */
    volatile uint16_t   touch_level;
    uint16_t            base_level;
    /* Baseline follows touch during initial cycles */
    uint8_t             start_cycle;
} touch;


/* LED sequence divider */

#define     LED_SEQ_DIV         2

/* LED sequence length */

#define     LED_SEQ_LENGTH      100

/* LED programming countdown count */

#define     LED_PROG_COUNT      30

/* LED sequence structure */

struct {
    /* Sequencer divide counter (lower rate than WDT timer) */
    uint8_t             div_cnt;
    /* Accumulator while programming */
    uint8_t             prog_acc;
    /* Sequence store */
    uint8_t             seq[LED_SEQ_LENGTH];
    /* Sequence read and write indices */
    uint8_t             seq_read_idx;
    uint8_t             seq_write_idx;
    /* Sequence has light flag */
    uint8_t             seq_has_light;
    /* Programming countdown */
    uint8_t             prog_countdown;
} led;


/* Initialize hardware */

void Init(void) {
    /* Configure watchdog as interval timer, clocked from ACLK,
     * ACLK/512 interval */
    WDTCTL = WDTPW | WDTTMSEL | WDTCNTCL | WDTSSEL | WDTIS1;
    IE1 |= WDTIE;

    /* Set MCLK and SMCLK to calibrated 8MHz clock */
    DCOCTL = 0;
    BCSCTL1 = XT2OFF | CALBC1_8MHZ;
    DCOCTL = CALDCO_8MHZ;
    /* ACLK is generated by VLOCLK */
    BCSCTL3 = LFXT1S1;

    /* LED and touch pin are both connected to Timer A, LED is output compare
     * and touch is input capture.  Other P1 bits should be pulled down. */
    P1REN = UNUSED_PIN_MASK | TOUCH_PIN_MASK;
    P1DIR = LED_PIN_MASK;
    P1OUT = 0;
    P1SEL = LED_PIN_MASK | TOUCH_PIN_MASK;

    /* Port 2 is not bonded out but shouldn't be left floating to minimize
     * current consumption */
    P2REN = 0xFF;

    /* Timer A continuous mode clocked from SMCLK */
    TACTL = TASSEL1 | MC1;
    /* CCR1 input capture on both rising and falling edge */
    TACCTL1 = CM0 | CM1 | CAP | CCIE;

    /* Initialize variables */
    led.seq_write_idx = LED_SEQ_LENGTH;
}

/* Set LED state */

void SetLED(uint8_t on) {
    if (on) {
        TACCTL0 = OUTMOD_4 | CCIE;
        TACCR0 = TAR + LED_TOGGLE_TIME * TIMER_FREQ;
    } else {
        TACCTL0 = OUTMOD_5;
    }
}

/* Program entry point */

int main(void) {
    /* Initialize hardware */
    Init();

    /* Main loop */
    for (;;) {
        /* Determine how deep we sleep: if we're programming, or the
         * sequence has light, we need to keep SMCLK on */
        if (led.seq_write_idx < LED_SEQ_LENGTH || led.seq_has_light) {
            /* Enable interrupts and go to sleep, keep SMCLK on */
            _BIS_SR(GIE | LPM0_bits);
        } else {
            /* Enable interrupts and go to deeper sleep */
            _BIS_SR(GIE | LPM3_bits);
        }

        /* Process touch capture: */
        /* Apply filtering to determine baseline */
        if (touch.start_cycle < START_CYCLES) {
            /* Track touch level directly on startup while things settle */
            touch.base_level = touch.touch_level;
            touch.start_cycle++;
        } else {
            /* Simple first order filter for baseline, much slower when
             * touch is active to be able to press and hold */
            if (touch.active & TOUCH_ACTIVE_MASK) {
                touch.base_level = ((touch.base_level * BASE_ACTIVE_MUL)
                    + touch.touch_level) >> BASE_ACTIVE_SHIFT;
            } else {
                touch.base_level = ((touch.base_level * BASE_FILTER_MUL)
                    + touch.touch_level) >> BASE_FILTER_SHIFT;
            }
            /* Immediately reduce baseline if touch is lower than baseline */
            if (touch.touch_level < touch.base_level) {
                touch.base_level = touch.touch_level;
            }
        }
        /* Determine if touch is above threshold relative to baseline */
        touch.active = ((touch.active << 1) & TOUCH_MASK) |
                        ((touch.touch_level - touch.base_level) >
                        (TOUCH_THRESHOLD + (touch.active & TOUCH_ACTIVE_MASK ?
                        -TOUCH_HYSTERESIS : TOUCH_HYSTERESIS)));

        /* Process LED sequence: */
        /* Are we in programming countdown? */
        if (led.prog_countdown > 0) {
            /* Blink LED */
            SetLED(led.prog_countdown & 0x01);
            led.prog_countdown--;
        } else
        /* Are we programming a sequence? */
        if (led.seq_write_idx < LED_SEQ_LENGTH) {
            /* Turn on LED if touch active */
            SetLED(touch.active & TOUCH_ACTIVE_MASK);
            /* Accumulate touch state */
            led.prog_acc += touch.active & TOUCH_ACTIVE_MASK;
            /* Run sequence divider */
            if (++led.div_cnt >= LED_SEQ_DIV) {
                /* Sequence step ready, reset divider */
                led.div_cnt = 0;
                /* Save the step */
                led.seq[led.seq_write_idx] = led.prog_acc > (LED_SEQ_DIV/2);
                /* Update sequence has light flag */
                led.seq_has_light |= led.seq[led.seq_write_idx];
                /* Increment the write index */
                led.seq_write_idx++;
                /* Reset the accumulator */
                led.prog_acc = 0;
            }
        } else
        /* Not in programming, did a touch just start? */
        if (touch.active == TOUCH_START) {
            /* Turn on programming countdown */
            led.prog_countdown = LED_PROG_COUNT;
            /* Reset indeces and divider */
            led.seq_write_idx = 0;
            led.seq_read_idx = 0;
            led.div_cnt = 0;
            /* Reset sequence has light flag */
            led.seq_has_light = 0;
        } else {
            /* Set the LED from the sequence */
            SetLED(led.seq[led.seq_read_idx]);
            /* Run sequence divider */
            if (++led.div_cnt >= LED_SEQ_DIV) {
                /* Sequence step ready, reset divider */
                led.div_cnt = 0;
                /* Increment and wrap index */
                if (++led.seq_read_idx >= LED_SEQ_LENGTH) {
                    led.seq_read_idx = 0;
                }
            }
        }
    }
}

/* Timer A CCR0 interrupt handler */

#pragma vector=TIMERA0_VECTOR
__interrupt void TimerA0_ISR(void) {
    /* Generate 100kHz by toggling every 5us */
    TACCR0 += LED_TOGGLE_TIME * TIMER_FREQ;
}

/* Handler for other Timer A interrupts (only CCR1 is enabled) */

#pragma vector=TIMERA1_VECTOR
__interrupt void TimerA_ISR(void) {
    /* Clear the interrupt flag */
    TACCTL1 &= ~CCIFG;
    /* In a touch capture sequence? */
    if (--touch.cap_cycle) {
        /* Toggle the sensor pull */
        P1OUT ^= TOUCH_PIN_MASK;
    } else {
        /* Calculate the touch level */
        touch.touch_level = TACCR1 - touch.cap_start;
        /* Exit sleep to process touch capture and run LED sequence */
        _BIC_SR_IRQ(LPM3_bits);
    }
}

/* WDT interval timer interrupt handler */

#pragma vector=WDT_VECTOR
__interrupt void WDTInterval_ISR(void) {
    /* Reset the touch timer capture cycle counter */
    touch.cap_cycle = TOUCH_CAP_CYCLES;
    /* Save the start count */
    touch.cap_start = TAR;
    /* Don't drive LED while we measure and process touch */
    TACCTL0 = OUTMOD_5;
    /* Toggle the sensor pull */
    P1OUT ^= TOUCH_PIN_MASK;
    /* Turn SMCLK and DCO back on if they were off */
    _BIC_SR_IRQ(SCG1 | SCG0);
}
