#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdint.h>
#include <string.h>
#include <mosquitto.h>
#include <json-c/json.h>
#include <stdbool.h>

const int accelerationButton = 11; //pin interruttore mod. accelerazione

//relay
const int gearUPpin = 12;
const int gearDWpin = 13;

const int CD_B = 25;
const int CD_C = 21;
const int CD_D = 23;
const int CD_A = 24;
const int LE   = 22;

//const int SH_LT = 7;
//#define N_GR 9
#define lapEndButton 26

const int SH_LT = 7;
const int N_GR = 0;
uint8_t gear = 1;
uint8_t gear_old = 0;
uint8_t Rpm = 0;
uint8_t lapNumber = 0;
float Speed = 0;
uint32_t accTime = 0; //millisecondi
uint32_t time0_75m = 0; //millisecondi
uint32_t accStartTime = 0; //millisecondi
uint8_t sogliaRpmUP = 46; //48=12500 rpm ::46=12000 rpm :: 44=11500 rpm :: 42=11000 rpm ::
uint8_t sogliaRpmDW = 42;
uint16_t tempoCieco = 2000; //millisecondi
uint32_t ultimaCambiataTime = 0;
uint32_t lap_time = 0;
uint16_t durataCambiata = 50; //millisecondi di chiusura del relay
uint16_t tempoAggFolle = 20; //tempo aggiuntivo da aggiungere al tempo di cambiata in caso di passaggio dal folle (caso prima marcia)
bool aggiorna0_100time = false; //flag che indica che è stata superata la velocità di 100km/h nella prova di accelerazione
bool acceleration = 0;
struct mosquitto *mosq = NULL;
char *topic = NULL;

const char* host= "localhost";
const int port = 1883;
const char* GEAR = "$SYS/formatted/GEAR";
const char* rpm = "$SYS/formatted/rpm";
const char* VhSpeed = "$SYS/formatted/VhSpeed";

struct can_frame {
    unsigned short int id;
    unsigned int time;
    char data[8];
};

struct can_frame frame750;
struct can_frame frame751;

void send_Frame(struct can_frame frame)
{
	char json[100]; // da fare l'alloc della memoria ad hoc
        sprintf(json, "{\"id\":%u,\"time\":%u,\"data\":[", frame.id, frame.time);
        char tmp[10];
        int i = 0;
        for (i = 0; i < 8; i++) {
		sprintf(tmp, "0x%02hhX,", frame.data[i]);
		strcat(json, tmp);
        }

        strcat(json, "]}");
	// printf("SEND: %s\n", json); // PER DEBUG
        mosquitto_publish(mosq, NULL, "$SYS/raw", 100, &json, 0, false);
}



void accelerationButtonValueChanged()
{
	delayMicroseconds(50000);
	acceleration = !(bool)digitalRead(accelerationButton);

	if (acceleration == 1) {
		printf("Mode Acceleration /n");
        	ultimaCambiataTime = (uint32_t)millis();
        	aggiorna0_100time = true;
        /*
		Serial.println("start timer");
        	delay(500);
    	} else {
    		mandare pacchetto con id 750
    	*/
    	}
}

void shiftLTvalueChanged()
{
	if (!(bool)digitalRead(SH_LT) && !(bool)digitalRead(N_GR))
		sogliaRpmUP = Rpm;
}



//cambio automatico nella prova di accelerazione
void gearUP(uint32_t sh_time)
{
	uint32_t ctrlTime = (uint32_t)millis() - ultimaCambiataTime;

	if (ctrlTime > tempoCieco) {
		uint16_t shift_duration = 0;
		if (gear != 1)
			shift_duration = durataCambiata;
		else
          		shift_duration=durataCambiata + tempoAggFolle;

	        digitalWrite(gearUPpin, LOW);
	        delay(shift_duration);
	        digitalWrite(gearUPpin, HIGH);
		printf("Marcia cambiata /n");
	        ultimaCambiataTime = (uint32_t)millis();
	        frame751.data[2] = shift_duration >> 8;
	        frame751.data[3] = shift_duration;
	        //frame751.data = ctrlTime;
	        frame751.id = 751;
	        frame751.time = (uint32_t)millis();
	        send_Frame(frame751);
	        gear_old = gear;
	        gear = gear + 1;
	}
}

void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	json_object *jobj = json_tokener_parse(message->payload);
	enum json_type type;

	jobj = json_object_object_get(jobj, "data");
	int data = json_object_get_int(jobj);

	if (message->topic == GEAR)
		gear = (uint8_t)data;

	if (message->topic == rpm)
		Rpm = (uint8_t)data;

	if (message->topic == VhSpeed)
		Speed = (float)data;
}

void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	mosquitto_subscribe(mosq, NULL, GEAR, 0);
	mosquitto_subscribe(mosq, NULL, rpm,0);
	mosquitto_subscribe(mosq, NULL, VhSpeed,0);
}

void NgearvalueChanged()
{
	if (!(bool)digitalRead(N_GR))
      		gear=0;
}

int Setup(void)
{
	//inizializzazione interruttori
	pinMode(accelerationButton, INPUT);
	pinMode(lapEndButton, INPUT);
	pinMode(gearUPpin, OUTPUT);
	pinMode(gearDWpin, OUTPUT);

	//è il caso di metterli nel loop?
	acceleration = !(bool)digitalRead(accelerationButton);

	//gestione degli interrupt della libreria wiringPi
	//int wiringPiISR ( int pin, int edgeType, void (*function)(void) )
	wiringPiISR(accelerationButton, INT_EDGE_BOTH, &accelerationButtonValueChanged);

	//inizializzazione CD4511 per codifica display 7 segmenti
	pinMode(CD_A, OUTPUT);
	pinMode(CD_B, OUTPUT);
	pinMode(CD_C, OUTPUT);
	pinMode(CD_D, OUTPUT);
	pinMode(LE,OUTPUT);

	//shift light e folle
	pinMode(SH_LT, INPUT);
	pinMode(N_GR, INPUT);
	wiringPiISR(SH_LT, INT_EDGE_BOTH, &shiftLTvalueChanged);
	wiringPiISR(N_GR, INT_EDGE_BOTH, &NgearvalueChanged);
}

int main(int argc, char *argv[])
{
	wiringPiSetup ();
	Setup();

	mosquitto_lib_init();
	mosquitto_connect(mosq, host, port, 60);
	printf("connessione avvenuta");
	mosq = mosquitto_new(NULL,true,NULL);
	mosquitto_connect_callback_set(mosq, connect_callback);
	mosquitto_message_callback_set(mosq, message_callback);

	while (1) {
		if (acceleration == 1) {
			aggiorna0_100time = true;
			if (Rpm >= sogliaRpmDW && (bool)digitalRead(SH_LT) == 1) {
				uint16_t sh_time = (uint32_t)millis() - accStartTime;
				frame751.data[0] = sogliaRpmUP >> 8;
				frame751.data[1] = sogliaRpmUP;
				// frame751.data[2] = sh_time >> 8;
				//frame751.data[3] = sh_time;
				//frame751.data[4] = sh_time >> 8;
				//frame751.data[5] = sh_time;
				//frame751.data[6] = 0;
				//frame751.data[7] = 0;
				frame751.id=751;
				frame751.time= (uint32_t)millis();
				send_Frame(frame751);

				if(Speed == 0){
					accStartTime = (uint32_t)millis();
				}
				if(Speed > 100 && aggiorna0_100time){
					accTime = (uint32_t)millis() - accStartTime;
					frame751.data[4] = accTime >> 8;
					frame751.data[5] = accTime;
					frame751.id=751;
					frame751.time=(uint32_t)millis();
					send_Frame(frame751);
					aggiorna0_100time = false;
				}
				//cambio marcia
				if ((bool)digitalRead(SH_LT) == 1 && gear != 0 && gear != 6)
					gearUP(sh_time);
	        	}
      		}
     	}
}
