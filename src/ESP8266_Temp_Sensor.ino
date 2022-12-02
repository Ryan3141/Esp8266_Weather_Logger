/*
*  This sketch sends a message to a TCP server
*
*/

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

//#include <Adafruit_TSL2561_U.h>
#include "Temp_Sensor.h"
#include "Device_Communicator.h"
#include "Handy_Types.h"

#include "C:\Tools\Login_Info\Microcontrollers.h"

const char* who_i_listen_to = "Climate Listener";
const char* intro_message = "Listener Climate Listener\n" \
							"Name Home 0\n" \
							"SQL_Headers temp_in_C\tpressure_in_atm\thumidity_in_percent\n";
const unsigned int localUdpPort = 5555;
Run_Periodically sample_period( Time::Seconds( 60 ) );

Device_Communicator listeners;
Temp_SensorClass Temp_Sensor;

void setup()
{
	Serial.begin( 115200 );

	listeners.Init( ssid, password, who_i_listen_to, intro_message, localUdpPort, Pin(D4) );
	Temp_Sensor.init();
}

void Send_Client_Data( Device_Communicator & listeners, const String & readings )
{
	listeners.Send_Client_Data( "SQL_Data " + readings + '\n' );
	Serial.println( "SQL_Data " + readings );
	//c.client.printf( "%0.2f\t%0.2f\t%0.2f\t%0.2f\n", previous_data.set_temp, readings.temp1, readings.temp2, readings.temp3 );
}


void loop()
{
	listeners.Update();

	if( sample_period.Is_Ready() ) // All temp sensors set to same resolution
	{
		Send_Client_Data( listeners, Temp_Sensor.grab_temp_data() );
		//Send_Client_Data( listeners, "0\t0\t0" );
	}
}
