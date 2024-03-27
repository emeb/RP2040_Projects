/*
 * Buddhabox
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h" // wait for interrupt 
#include <math.h>
#include "debounce.h"
#include "pcm_data.h"

// Indicator
#define LED_PIN 22

// Button
#define BTN_PIN 23

// Amp control
#define AMP_PIN 24

// PWM output
#define AUDIO_PIN 25

// Pot analog input
#define POT_PIN 26

int wav_position = 0;
volatile uint8_t vol = 0, mode = 0, update = 0;
volatile uint32_t tick = 0;
debounce_state btn_dbs;
volatile uint8_t btn_fe, btn_re;
struct repeating_timer tick_rt;
uint8_t sine_table[256];
uint32_t sin_phs, sin_frq;

/* build version in simple format */
const char *fwVersionStr = "V0.1";

/* build time */
const char *bdate = __DATE__;
const char *btime = __TIME__;

/*
 * set sine freq
 */
void set_frq(float hz)
{
	/* set freq to 1kHz for 88kHz sample rate */
	sin_frq = floorf(powf(2.0F, 32.0F) * hz / 88000.0F);
}

/*
 * generate sine table
 */
void init_wav(void)
{
	int i;
	float th;
	
	/* build table */
	for(i=0;i<256;i++)
	{
		th = (float)i * 6.2832 / 256.0F;
		sine_table[i] = (128.0F + (100.0F*sinf(th)));
	}
	
	/* default freq */
	set_frq(1000.0F);
	sin_phs = 0;
}

/* White Noise Generator State */
#define NOISE_BITS 8
#define NOISE_MASK ((1<<NOISE_BITS)-1)
#define NOISE_POLY_TAP0 31
#define NOISE_POLY_TAP1 21
#define NOISE_POLY_TAP2 1
#define NOISE_POLY_TAP3 0

uint32_t wht_noise_lfsr;

/* Pink noise states */
uint8_t pnk_state[8], pnk_cntr;

void init_noise(void)
{
	/* init white noise generator */
	wht_noise_lfsr = 1;
	
	/* init pink noise generator */
	pnk_cntr = 0;
}

/* 32-bit LFSR noise generator */
uint8_t PRN(uint32_t *lfsr)
{
	uint8_t bit;
	uint32_t new_data;
	
	for(bit=0;bit<NOISE_BITS;bit++)
	{
		new_data = ((*lfsr>>NOISE_POLY_TAP0) ^
					(*lfsr>>NOISE_POLY_TAP1) ^
					(*lfsr>>NOISE_POLY_TAP2) ^
					(*lfsr>>NOISE_POLY_TAP3));
		*lfsr = (*lfsr<<1) | (new_data&1);
	}
	
	return *lfsr&NOISE_MASK;
}

/* plain white noise */
uint8_t wht_noise(void)
{
	return PRN(&wht_noise_lfsr);
}

/* Pink noise needs white input */
uint8_t pnk_noise(void)
{
	uint16_t bit;
	int32_t sum;
	
	/* get raw white noise */
	uint8_t wht = PRN(&wht_noise_lfsr);
	
	/* store new white value in overlapping states */
	for(bit=0;bit<7;bit++)
		if(((pnk_cntr>>bit)&1) == 1)
			break;
	pnk_state[bit] = wht;
	pnk_cntr++;
	
	/* sum overlapping states */
	sum = 0;
	for(bit=0;bit<8;bit++)
		sum += pnk_state[bit];
	
	return sum >> 4;
}

/*
 * compute next audio sample and send to PWM 
 */
void pwm_interrupt_handler(void)
{
	uint8_t pwm_dat;
	
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
	
	if(update)
	{
		/* sample record changed so reset wav_position */
		update = 0;
		wav_position = 0;
		
		/* set sine freqs */
		if(mode == 4)
			set_frq(440.0F);
		else if(mode == 5)
			set_frq(1000.0F);
	}
	
	/* get sample */
	switch(mode)
	{
		case 0:
		case 1:
		case 2:
		case 3:
			/* get sample from loop arrays, ZOH by 8x */
			pwm_dat = pcm_data[mode&3][wav_position>>3] ^ 0x80;
	
			/* update position */
			wav_position++;
			if(wav_position >= (*pcm_len[mode&3]<<3))
				wav_position = 0;
			break;
		
		case 4:
		case 5:
			/* sine tones */
			pwm_dat = sine_table[sin_phs>>24];
			sin_phs += sin_frq;
			break;
		
		case 6:
			/* white noise */
			pwm_dat = wht_noise();
			break;
		
		case 7:
			/* pink noise */
			pwm_dat = pnk_noise();
			break;
		
		default:
			pwm_dat = 0;
	}
	
	/* scale by volume and send to speaker */
	uint16_t vol_dat = ((uint16_t)pwm_dat * (uint16_t)vol)>>8;
	pwm_set_gpio_level(AUDIO_PIN, vol_dat);
}

/*
 * 1ms tick callback for button and adc refresh
 */
bool tick_callback(repeating_timer_t *rt)
{
	/* update tick counter */
	tick++;
	
	/* debounce button */
	debounce(&btn_dbs, (!gpio_get(BTN_PIN)));
	btn_fe |= btn_dbs.fe;
	btn_re |= btn_dbs.re;
	
	/* refresh pot value */
	vol = 255 - (adc_read()>>4);
    return true;
}

/*
 * return TRUE if goal is reached
 */
uint32_t tickcheck(uint32_t goal)
{
    /**************************************************/
    /* DANGER WILL ROBINSON!                          */
    /* the following syntax is CRUCIAL to ensuring    */
    /* that this test doesn't have a wrap bug         */
    /**************************************************/
	return (((int32_t)tick - (int32_t)goal) < 0);
}


/*
 * entry point
 */
int main()
{
	int32_t i;
	uint32_t tick_goal;
	pico_unique_board_id_t id_out;
	
	/* overclock for PWM sample rate */
	set_sys_clock_khz(176000, true);
	
	/* init SDK */
    stdio_init_all();
	
	printf("\n\nBuddhabox %s starting\n\r", fwVersionStr);
	printf("System Clock: %d\n\r", clock_get_hz(clk_sys));
	printf("CHIP_ID: 0x%08X\n\r", *(volatile uint32_t *)(SYSINFO_BASE));
	printf("BOARD_ID: 0x");
	pico_get_unique_board_id(&id_out);
	for(i=0;i<PICO_UNIQUE_BOARD_ID_SIZE_BYTES;i++)
		printf("%02X", id_out.id[i]);
	printf("\n");
	printf("Build Date: %s\n\r", bdate);
	printf("Build Time: %s\n\r", btime);
	printf("\n");

	/* setup audio */
	init_wav();
	init_noise();
	
	/* setup indicator LED */
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	printf("LED initialized\n\r");
	
	/* setup button input */
	gpio_init(BTN_PIN);
	gpio_set_dir(BTN_PIN, GPIO_IN);
	gpio_set_pulls(BTN_PIN, true, false);
	init_debounce(&btn_dbs, 15);
	btn_fe = 0;
	btn_re = 0;
	printf("Button initialized\n\r");
	
	/* set up ADC to do continuous conversions on chl 0 (GPIO 26) */
	adc_init();
    adc_gpio_init(POT_PIN);
	adc_select_input(0);
	printf("ADC initialized\n\r");
	
	/* 1ms timer to refresh button and pot state */
	add_repeating_timer_ms(
		1,	// 1ms
		tick_callback,
		NULL,
		&tick_rt
	);
	printf("Tick initialized\n\r");
	
	/* setup amp enable */
	gpio_init(AMP_PIN);
	gpio_set_dir(AMP_PIN, GPIO_OUT);
	printf("Amp enable initialized\n\r");
	
	/* setup PWM output pin */
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
	
    /* Setup PWM interrupt to fire when PWM cycle is complete */
    pwm_clear_irq(audio_pin_slice);
    pwm_set_irq_enabled(audio_pin_slice, true);
	
    /* set the handle function above */
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler); 
    irq_set_enabled(PWM_IRQ_WRAP, true);

    /* Setup PWM for audio output */
    pwm_config config = pwm_get_default_config();
    /* Base clock 176,000,000 Hz divide by wrap 250 then the clock divider further divides
     * to set the interrupt rate. 
     * 
     * 11 KHz is fine for speech. Phone lines generally sample at 8 KHz
     * 
     * 
     * So clkdiv should be as follows for given sample rate
     *  8.0f for 11 KHz
     *  4.0f for 22 KHz
     *  2.0f for 44 KHz etc
     */
    pwm_config_set_clkdiv(&config, 8.0f); 
    pwm_config_set_wrap(&config, 250); 
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);
	printf("PWM output initialized\n\r");
	
	/* turn on amp */
	gpio_put(AMP_PIN, 1);
	printf("Amp enabled\n\r");
	
	/* wait forever, blinking */
	printf("Looping...\n\r");
	tick_goal = tick + 100;
    while (true)
	{
		/* check for button press and adjust mode */
		if(btn_re)
		{
			btn_re = 0;
			mode = (mode + 1) & 0x7;
			update = 1;
		}
		
		/* time for UI update? */
		if(!tickcheck(tick_goal))
		{
			tick_goal = tick + 100;
			printf("vol = %3d mode = %d\n\r", vol, mode);
			gpio_put(LED_PIN, 1^gpio_get(LED_PIN));
		}
    }
}
