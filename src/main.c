/*
 * Tamper-response firmware for the Raspberry Pi Pico 2 (RP2350), Zephyr version.
 * Stage 4: add the secret, the zeroize-on-tamper response, and a shared
 *          "tampered" flag that the heartbeat thread reads to change the LED.
 *
 * Threads running concurrently:
 *   - main            : prints "alive" once a second
 *   - heartbeat_thread: blinks the LED slowly when armed, rapidly when tampered
 *   - tamper_thread    : watches the reed switch, zeroizes the secret on tamper
 *
 * New RTOS concept: the two worker threads share one piece of state (the
 * tampered flag). We use an atomic variable so they can read/write it safely
 * without corrupting each other.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

/* ---- Devices ---- */
#define LED0_NODE   DT_ALIAS(led0)
#define TAMPER_NODE DT_NODELABEL(tamper_switch)
static const struct gpio_dt_spec led    = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec tamper = GPIO_DT_SPEC_GET(TAMPER_NODE, gpios);

/* ---- The secret ----
 * 32 bytes of 0xAB, exactly like the Rust firmware. 'volatile' tells the
 * compiler this memory can change in ways it can't see, which (combined with
 * the volatile write in zeroize) stops it from optimizing the wipe away --
 * the same dead-store-elimination lesson you saw in Ghidra. */
#define SECRET_LEN 32
static volatile uint8_t secret[SECRET_LEN];

/* ---- Shared state between threads ----
 * atomic_t is a variable multiple threads can safely read and write.
 * 0 = armed, 1 = tampered. The tamper thread writes it; the heartbeat
 * thread reads it. Using atomic access means neither thread can catch the
 * other mid-update. */
static atomic_t tampered = ATOMIC_INIT(0);

/* Fill the secret with 0xAB. */
static void arm_secret(void)
{
    for (int i = 0; i < SECRET_LEN; i++) {
        secret[i] = 0xAB;
    }
}

/* Wipe the secret. Volatile writes so the compiler MUST emit every store
 * (you saw these as the wall of strb instructions in Ghidra). */
static void zeroize_secret(void)
{
    for (int i = 0; i < SECRET_LEN; i++) {
        secret[i] = 0x00;
    }
}

/* ---- Heartbeat thread: LED reflects the shared state ---- */
void heartbeat_thread(void)
{
    if (!gpio_is_ready_dt(&led)) {
        printk("heartbeat: LED device not ready!\n");
        return;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    while (1) {
        /* Read the shared flag. If tampered, blink fast; if armed, blink slow.
         * This is the heartbeat thread REACTING to what the tamper thread
         * wrote -- two threads communicating through shared state. */
        if (atomic_get(&tampered)) {
            gpio_pin_toggle_dt(&led);
            k_msleep(80);    /* frantic fast blink = tampered */
        } else {
            gpio_pin_toggle_dt(&led);
            k_msleep(500);   /* calm slow blink = armed */
        }
    }
}

/* ---- Tamper-monitor thread: detect tamper, zeroize, set shared flag ---- */
void tamper_thread(void)
{
    if (!gpio_is_ready_dt(&tamper)) {
        printk("tamper: switch device not ready!\n");
        return;
    }
    gpio_pin_configure_dt(&tamper, GPIO_INPUT);

    arm_secret();
    printk("tamper: monitoring GPIO7 (armed, secret loaded)\n");

    while (1) {
        int val = gpio_pin_get_dt(&tamper);

        /* Only act on the first tamper (one-way latch). atomic_get checks
         * the current state without a separate local flag. */
        if (!atomic_get(&tampered) && val == 1) {
            zeroize_secret();                 /* destroy the secret */
            atomic_set(&tampered, 1);         /* publish tampered to all threads */
            printk("*** TAMPER DETECTED -- secret zeroized ***\n");
        }

        k_msleep(20);
    }
}

/* heartbeat: low priority (7). tamper: higher priority (5). */
K_THREAD_DEFINE(heartbeat_id, 512, heartbeat_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(tamper_id,    512, tamper_thread,    NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    printk("Tamper firmware starting...\n");

    while (1) {
        printk("alive\n");
        k_msleep(1000);
    }

    return 0;
}