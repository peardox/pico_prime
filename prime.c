/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Pico

#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#define PRIMECOUNT 60000

float primer(bool firstpass);

/* Code to check how much memory is available to the program
 *
 * Purloined from https://forums.raspberrypi.com/viewtopic.php?t=347638
 */
 
uint32_t getTotalHeap(void) {
   extern char __StackLimit, __bss_end__;
   
   return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap(void) {
   struct mallinfo m = mallinfo();

   return getTotalHeap() - m.uordblks;
}

/* Code to check if the bootsel button is pressed
 *
 * Blatantly pinched from https://github.com/raspberrypi/pico-examples/blob/master/picoboard/button/button.c
 */ 
bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

/* main
 * Waits for bootsel to be pressed then runs the prime test
 */
int main() {
    int passcount;
    float this_run, total_runs;

    // Set stdin/out redirection etc
    stdio_init_all();

    passcount = 0;

    // Ad infinitum...
    while(1) {
        // Wait for user to press BOOTSEL button
        if(get_bootsel_button()) {
            passcount++;
            if(passcount == 1) {
                // First pass show how much memory we've got (could verify we've got enough for PRIMECOUNT records)
                printf("FreeHeap = %d\n", getFreeHeap());
                this_run = primer(true);
                total_runs = this_run;
                // Print the runtime
                printf("Runtime = %f\n", this_run);
            } else {
                this_run = primer(false);
                total_runs += this_run;
                // Print the runtime, pass # and average runtime
                printf("Runtime = %f, Pass = %d, Average Runtime = %f\n", this_run, passcount, total_runs / (passcount));
            }

        }
    sleep_ms(100);
    } 
}

/*
 * primer - function to calculste the first PRIMECOUNT primes
 * param firstpass - if it's the first run - just used to print out largest prime found (i.e. pointless)
 * Returns elapsed runtime in seconds accuracy = microsecond
 */
float primer(bool firstpass) {
    uint32_t primes[PRIMECOUNT]; // Array of PRIMECOUNT 32 bit unsigned primes
    uint32_t searchto, candidate, remainder, count, index; // Assorted variables
    uint64_t start; // Microsecond timer
    bool isprime;   // Is the candidate number prime?
    float elapsed;  // For the return value

    // Get the start time
    start = time_us_64();

    // Initialise a few of the first primes - would work with less, this just skips the easy ones
    primes[0] = 2;
    primes[1] = 3;    
    primes[2] = 5;
    primes[3] = 7;

    // Set current index to last known prime
    index = 3;

    // Set the next candidate to the curent max prime known so far
    candidate = primes[index];

    // Loop until we have PRIMECOUNT primes
    while (index < PRIMECOUNT) {
        // Increase candidate number by to so we can test if it's prime
        candidate += 2;
        // We don't have to check past sqrt(candidate) - this saves time as the size of the array increases as 
        // we only ever check a smaller number of values
        searchto = trunc(sqrt(candidate));
        // Assume the candidate is prime
        isprime = true;
        // Get the remainder created by dividing the candidate by all primes and including up to searchto
        for(count = 1; (primes[count] <= searchto); count++) {
			// Start at 1 rather than 0 to bypass div/2
            remainder = candidate % primes[count];
            // If the remainder was zero then the current candidate is divisible by an already known prime
            // which means that the candidate is not prime
            if(remainder == 0) {
                isprime = false;
                break;
            }
        }
        if(isprime) {
            // If candidate was prime then increment index and store the new prime
            index++;
            primes[index] = candidate;
        }
    }

    // First time around print out max prime found (for the hell of it)
    if(firstpass) {
        printf("Last Prime = %lu\n",candidate);
    }

    // Return the elapsed time in seconds
    elapsed = (time_us_64() - start) / 1000000.0;
    return elapsed;
}
