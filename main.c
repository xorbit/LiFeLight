/* LiFeLight
 * Demo for LiFePO4wered/USB modules
 *
 * The board has a (boosted) LED and a touch sensor.  The idea is to be able
 * to touch-program a 10 second light sequence which will be repeated.
 */

#include <msp430.h> 
#include <stdint.h>


/* Timer frequency (in MHz) */

#define		TIMER_FREQ			8

/* LED and touch pin mask */

#define		LED_PIN_MASK		0x20
#define		TOUCH_PIN_MASK		0x04

/* Unused pin mask */

#define		UNUSED_PIN_MASK		0xDB

/* LED toggle time (in us) */

#define		LED_TOGGLE_TIME		5

/* Touch capture cycles */

#define		TOUCH_CAP_CYCLES	20

/* Number of touch measurment cycles where baseline follows
 * touch level directly */

#define		START_CYCLES		20

/* Base filter multiplier and shift */

#define 	BASE_FILTER_SHIFT	6
#define		BASE_FILTER_MUL		((1<<BASE_FILTER_SHIFT)-1)

/* Touch detect threshold */

#define		TOUCH_THRESHOLD		20

/* Touch structure */

struct {
	volatile uint8_t 	cap_cycle;
	volatile uint16_t	cap_start;
	volatile uint16_t	cap_end;
	uint32_t			touch_level;
	uint32_t			base_level;
	uint16_t			start_cycle;
	uint8_t				active;
} touch;


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
	    /* Enable interrupts and go to sleep */
		_BIS_SR(GIE | LPM1_bits);

		/* Process touch capture: */
		/* Calculate touch level */
		touch.touch_level = touch.cap_end - touch.cap_start;
		/* Apply filtering to determine baseline */
		if (touch.start_cycle < START_CYCLES) {
			/* Track touch level directly on startup while things settle */
			touch.base_level = touch.touch_level;
			touch.start_cycle++;
		} else if (!touch.active) {
			/* Simple first order filter for baseline */
			touch.base_level = ((touch.base_level * BASE_FILTER_MUL)
				+ touch.touch_level) >> BASE_FILTER_SHIFT;
			/* Immediately reduce baseline if touch is lower though */
			if (touch.touch_level < touch.base_level) {
				touch.base_level = touch.touch_level;
			}
		}
		/* Determine if touch is above threshold relative to baseline */
		touch.active = (touch.touch_level - touch.base_level) > TOUCH_THRESHOLD;

		/* Process LED sequence: */
		/* Turn on LED if touch active */
		SetLED(touch.active);
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
	/* Enable interrupts so Timer A CCR0 can be updated */
	_BIS_SR(GIE);
	/* Clear the interrupt flag */
	TACCTL1 &= ~CCIFG;
	/* In a touch capture sequence? */
	if (--touch.cap_cycle) {
		/* Toggle the sensor pull */
		P1OUT ^= TOUCH_PIN_MASK;
	} else {
		/* Save the end count */
		touch.cap_end = TACCR1;
		/* Exit sleep to process touch capture */
		_BIC_SR_IRQ(LPM3_bits);
	}
}

/* WDT interval timer interrupt handler */

#pragma vector=WDT_VECTOR
__interrupt void WDTInterval_ISR(void) {
	/* Enable interrupts so Timer A CCR0 can be updated */
	_BIS_SR(GIE);
	/* Reset the touch timer capture cycle counter */
	touch.cap_cycle = TOUCH_CAP_CYCLES;
	/* Save the start count */
	touch.cap_start = TAR;
	/* Toggle the sensor pull */
	P1OUT ^= TOUCH_PIN_MASK;
	/* Don't drive LED while we measure and process touch */
	TACCTL0 = OUTMOD_5;
}
