#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <semaphore.h>

#include <json-c/json.h>
#include <mosquitto.h>
#include <wiringPi.h>

/*
 * Configuration
 */
#define SHIFT_LIGHT_PIN     X
#define NEUTRAL_GEAR_PIN    X
#define GEAR_UP_PIN         X
#define GEAR_SHIFT_TIME_MS  X // milliseconds
#define GEAR_DEAD_TIME_MS   X // milliseconds
#define GEAR_MAX            X

#define MQTT_TOPIC_GEAR     "data/formatted/gear"
#define MQTT_BROKER_ADDR    "localhost"
#define MQTT_BROKER_PORT    1883
#define MQTT_KEEPALIVE      120

#define EXIT_FAILURE_MQTT_CONNECT -2
#define EXIT_FAILURE_MUTEX        -3


/*
 * Globals
 * For safety purposes, let's start assuming neutral gear is active,
 * in order to NOT execute any unwanted mechanical operation.
 */
volatile bool is_neutral = true;
volatile uint8_t current_gear = 0;

volatile sem_t current_gear_mutex;
volatile sem_t powershift_mutex;
volatile sem_t is_neutral_mutex;
struct mosquitto* mosq;

volatile bool running = true;


/**
 * Callback to new mosquitto messages. Runs asynchronously.
 * Update for new gear value.
 *
 * @param mosq    Mosquitto client object
 * @param obj     _unneeded_
 * @param message A struct with _topic_ of provenience and _payload_
 */
void mosquitto_inbox(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    const json_object* json = json_tokener_parse(message->payload);
    const json_object* json_value;

    if ((json != NULL) && (json_object_get_type(json) == json_type_object) {
        json_object_object_get_ex(json, "value", &json_value);

        if ((json_object != NULL) && (json_object_get_type(json_value) == json_type_int)) {
            int32_t new_gear = json_object_get_int(json_value);

            if (new_gear >= 0 && new_gear <= GEAR_MAX) {
                sem_wait(&current_gear_mutex);
                current_gear = (uint8_t) new_gear;
                sem_post(&current_gear_mutex);
            }
        }
    }
}

/**
 * Setup gear input coming from mosquitto.
 */
void gear_input_setup() {
    mosquitto_lib_init();
    mosq = mosquitto_new("ModAccel", true, NULL);

    mosquitto_message_callback_set(mosq, mosquitto_inbox);

    int ret = mosquitto_connect(mosq, MQTT_BROKER_ADDR, MQTT_BROKER_PORT, MQTT_KEEPALIVE);
    if (ret != MOSQ_ERR_SUCCESS)
        return;

    mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_GEAR, 1);
}

/**
 * Callback if shift light has changed. Runs asynchronously.
 * This is the critical part of the program.
 */
void shift_light_changed() {
    // early return on error, saving time with short jumps
    if ((bool) digitalRead(SHIFT_LIGHT_PIN)) //! Signal is ACTIVE if 0
        return;
    
    if (is_neutral || (current_gear > GEAR_MAX) || (current_gear <= 0))
        return;


    /*
     * === CRITICAL SECTION ===
     * Please be aware that PowerShift MUST be used responsibly.
     * DO NOT EVER use the resource improperly!
     */
    sem_wait(&powershift_mutex);
    
    // OK to go, signal is airborne
    digitalWrite(GEAR_UP_PIN, LOW);
    delay(GEAR_SHIFT_TIME_MS);
    digitalWrite(GEAR_UP_PIN, HIGH);
    delay(GEAR_DEAD_TIME_MS);

    int ret = sem_post(&powershift_mutex);
    /* === END CRITICAL SECTION === */

    if (ret == -1)
        exit(EXIT_FAILURE_MUTEX);
}

/**
 * Callback if neutral signal has changed. Runs asynchronously.
 */
void neutral_gear_changed() {
    sem_wait(&is_neutral_mutex);
    
    if ((bool) digitalRead(NEUTRAL_GEAR_PIN))
        is_neutral = true;
    else
        is_neutral = false;

    sem_post(&is_neutral_mutex);
}

/**
 * Setup inputs using wiringPi and MQTT
 */
void input_setup() {
    wiringPiSetup();

    pinMode(SHIFT_LIGH_PIN, INPUT);
    pinMode(NEUTRAL_GEAR_PIN, INPUT);

    wiringPiISR(SHIFT_LIGHT_PIN, INT_EDGE_BOTH, shift_light_changed);
    wiringPiISR(NEUTRAL_GEAR_PIN, INT_EDGE_BOTH, neutral_gear_changed);

    gear_input_setup();
}

/**
 * Cleanup handler upon program termination
 */
inline void program_terminate() {
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    sem_close(&powershift_mutex);
    sem_close(&current_gear_mutex);
    sem_close(&is_neutral_mutex);
}

/**
 * Linux signal handler
 */
inline void terminate_handler() {
    running = false;
}

/**
 * Main program loop until a signal is caught
 * Check and keep alive MQTT connection.
 */
inline void loop() {
    do {
        mosquitto_loop(mosq, -1, 1);
    } while (running);
}

/**
 * Main program logic
 */
int main(int argv, char** argc) {
    sem_init(&powershift_mutex, 0, 1);
    sem_init(&current_gear_mutex, 0, 1);
    sem_init(&is_neutral_mutex, 0, 1);

    input_setup();

    signal(SIGTERM, terminate_handler);
    signal(SIGHUP, terminate_handler);
    signal(SIGINT, terminate_handler);

    loop();

    program_terminate();

    return EXIT_SUCCESS;
}

