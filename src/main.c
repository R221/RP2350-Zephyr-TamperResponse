/*
 * Tamper-response firmware for the Raspberry Pi Pico 2 (RP2350), Zephyr version.
 * Stage 5 (final): add a custom "tamper" shell command so you can query the
 *                  device interactively over serial.
 *
 *   tamper status  -> reports ARMED or TAMPERED
 *   tamper secret  -> prints the secret bytes (AB..AB when armed, 00..00 wiped)
 *
 * Threads:
 *   - main            : prints "alive" once a second
 *   - heartbeat_thread: slow blink armed, fast blink tampered
 *   - tamper_thread    : watches the reed switch, zeroizes on tamper
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>

/* ---- Devices ---- */
#define LED0_NODE   DT_ALIAS(led0)
#define TAMPER_NODE DT_NODELABEL(tamper_switch)
static const struct gpio_dt_spec led    = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec tamper = GPIO_DT_SPEC_GET(TAMPER_NODE, gpios);

/* ---- The secret (volatile so the wipe can't be optimized away) ---- */
#define SECRET_LEN 32
static volatile uint8_t secret[SECRET_LEN];

/* ---- Shared state: 0 = armed, 1 = tampered ---- */
static atomic_t tampered = ATOMIC_INIT(0);

static void arm_secret(void)
{
    for (int i = 0; i < SECRET_LEN; i++) {
        secret[i] = 0xAB;
    }
}

static void zeroize_secret(void)
{
    for (int i = 0; i < SECRET_LEN; i++) {
        secret[i] = 0x00;
    }
}

/* ---- Heartbeat thread: LED reflects shared state ---- */
void heartbeat_thread(void)
{
    if (!gpio_is_ready_dt(&led)) {
        printk("heartbeat: LED device not ready!\n");
        return;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(atomic_get(&tampered) ? 80 : 500);
    }
}

/* ---- Tamper-monitor thread ---- */
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

        if (!atomic_get(&tampered) && val == 1) {
            zeroize_secret();
            atomic_set(&tampered, 1);
            printk("*** TAMPER DETECTED -- secret zeroized ***\n");
        }

        k_msleep(20);
    }
}

K_THREAD_DEFINE(heartbeat_id, 512, heartbeat_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(tamper_id,    512, tamper_thread,    NULL, NULL, NULL, 5, 0, 0);

/* ======================================================================
 * Custom shell command: "tamper"
 * ====================================================================== */

/* Handler for "tamper status" */
static int cmd_tamper_status(const struct shell *sh, size_t argc, char **argv)
{
    if (atomic_get(&tampered)) {
        shell_print(sh, "State: TAMPERED (secret has been zeroized)");
    } else {
        shell_print(sh, "State: ARMED (secret intact)");
    }
    return 0;
}

/* Handler for "tamper secret" -- dumps the secret bytes so you can SEE
 * whether it's intact (AB..) or wiped (00..). Proof of the zeroize, live. */
static int cmd_tamper_secret(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Secret bytes:");
    for (int i = 0; i < SECRET_LEN; i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%02X ", secret[i]);
    }
    shell_print(sh, "");   /* newline */
    return 0;
}

/* Build the subcommand set: "status" and "secret" under "tamper". */
SHELL_STATIC_SUBCMD_SET_CREATE(tamper_subcmds,
    SHELL_CMD(status, NULL, "Show armed/tampered state.", cmd_tamper_status),
    SHELL_CMD(secret, NULL, "Dump the secret bytes.",     cmd_tamper_secret),
    SHELL_SUBCMD_SET_END
);

/* Register the top-level "tamper" command with its subcommands. */
SHELL_CMD_REGISTER(tamper, &tamper_subcmds, "Tamper device commands", NULL);

int main(void)
{
    printk("Tamper firmware starting...\n");

    while (1) {
        printk("alive\n");
        k_msleep(1000);
    }

    return 0;
}