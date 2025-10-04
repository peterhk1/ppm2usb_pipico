#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/systick.h"
#include "pico/bootrom.h"

#include "bsp/board.h"
#include "tusb.h"

#include "usb_descriptors.h"


// the IO pin with the PPM signal
#define PPM_PIN 0

#define SYSTICK_ENABLE        0x1
#define SYSTICK_CLKSOURCE     0x4
#define SYSTICK_INIT          0x00FFFFFF

// min sync pulse time ms
#define PULSE_MINGAP 5
// min and max pulse time us
#define PULSE_MIN 400
#define PULSE_MAX 2050
// resolution
#define RANGE 8192.0

#define NCHANNELS 8


typedef uint16_t axisres_t;

typedef struct mine_hid_gamepad_report_t {
	axisres_t  value[ NCHANNELS ];
} mine_hid_gamepad_report;

mine_hid_gamepad_report hidreport;


enum {
	STATE_NOT_MOUNTED,
	STATE_MOUNTED,
	STATE_SUSPENDED
};

// system clock
static uint32_t clock_hz;
// no of ticks for sync pulse
static uint32_t ticks_tsync;
// min/max width of pulse
static uint32_t ticks_offset;
static uint32_t ticks_limit;
static float scale;

static bool send_report;

static uint32_t usb_state = STATE_NOT_MOUNTED;

void hid_task(void);

uint32_t clamp(uint32_t val, uint32_t min, uint32_t max) {
	uint32_t res = val < min ? min : val;
	return res > max ? max : res;
}


void start_timer() {
	systick_hw->rvr = SYSTICK_INIT;
	systick_hw->cvr = 0;
	systick_hw->csr |= (SYSTICK_CLKSOURCE | SYSTICK_ENABLE);
}

/*
 * Returns false on overflow
 */
bool stop_timer() {
	systick_hw->csr &= SYSTICK_ENABLE;
	return systick_hw->csr & 0x10000;
}

uint32_t get_timer_ticks() {
	return SYSTICK_INIT - systick_hw->cvr;
}

/*
 * Irq handler of edge transitions. We're measuring ticks between falling
 * and rising edge.
 */
void gpio_callback(uint gpio, uint32_t events) {
	static int32_t current_channel = -2;

	// shouldn't happen
	if (gpio != PPM_PIN) {
		return;
	}

	// measure the length of negative pulses
	if (events & GPIO_IRQ_EDGE_FALL) {
		// start counting
		stop_timer();
		start_timer();
		if (-1 <= current_channel && current_channel <= NCHANNELS) {
			// -1 means previous was sync
			current_channel++;
		}
	}
	else
	if (events & GPIO_IRQ_EDGE_RISE) {
		// end of pulse
		uint32_t elapsedticks = get_timer_ticks();
		if (stop_timer()) {
			// overflow - more than ~134ms (systick has 2^24 bits)
			// not in sync
			current_channel = -2;
		} else {
			if (elapsedticks > ticks_tsync) {
				// sync pulse
				current_channel = -1;
			} else {
				// channel pulse
				if ( 0 <= current_channel && current_channel < NCHANNELS ) {
					uint32_t clampedticks = clamp(elapsedticks, ticks_offset, ticks_limit);
					uint32_t chan_val = (uint32_t)((clampedticks-ticks_offset)*scale);
					hidreport.value[current_channel] = (axisres_t)chan_val;
				}
				if (current_channel==NCHANNELS-1) {
					send_report = true;
				}
			}
		}
	}
}


void init_io() {
	// set input
	gpio_init(PPM_PIN);

	gpio_set_irq_enabled_with_callback(PPM_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
}


/*------------- MAIN -------------*/
int main(void)
{
	board_init();
	tusb_init();

	clock_hz = clock_get_hz(clk_sys);
	ticks_tsync = PULSE_MINGAP*(clock_hz/1000);
	// min pulse length
	ticks_offset = PULSE_MIN*(clock_hz/1000000);
	ticks_limit = PULSE_MAX*(clock_hz/1000000);
	scale = RANGE/(ticks_limit-ticks_offset);

	// init to middle position
	for(int i=0; i < NCHANNELS; i++) {
		hidreport.value[i] = RANGE/2;
	}

	init_io();

	board_led_write(true);

	while (1) {
		tud_task(); // tinyusb device task

		hid_task();
	}

	return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
	usb_state = STATE_MOUNTED;

	board_led_write(true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	usb_state = STATE_NOT_MOUNTED;

	board_led_write(false);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
	(void) remote_wakeup_en;
	usb_state = STATE_SUSPENDED;

	board_led_write(false);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
	usb_state = STATE_MOUNTED;

	board_led_write(true);
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id)
{
	// skip if hid is not ready yet
	if ( !tud_hid_ready() ) return;

	switch(report_id)
	{
		case REPORT_ID_GAMEPAD:
		{
			tud_hid_report(REPORT_ID_GAMEPAD, &hidreport, sizeof(hidreport));
		}
		break;

		default: break;
	}
}

// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
	const uint32_t interval_ms = 60;
	static uint32_t start_ms = 0;

	if ( !( send_report || (board_millis() - start_ms >= interval_ms) ) ) return;
	start_ms += interval_ms;
	send_report = false;

	// Remote wakeup
	if ( tud_suspended() ) {
		// Wake up host if we are in suspend mode
		// and REMOTE_WAKEUP feature is enabled by host
		//tud_remote_wakeup();
	}else {
		send_hid_report(REPORT_ID_GAMEPAD);
	}
}

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
	(void) instance;
	(void) len;

	uint8_t next_report_id = report[0] + 1;

	if ( next_report_id < REPORT_ID_COUNT ) {
		send_hid_report(next_report_id);
	}
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
	// TODO not Implemented
	(void) instance;
	(void) report_id;
	(void) report_type;
	(void) buffer;
	(void) reqlen;

	return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
	(void) instance;

}

