//
//
//

#include "Device_Communicator.h"

#include <algorithm>
#include <map>

void Device_Communicator::Init( const char* SSID, const char* password, const char* who_i_listen_to, const char* initial_message, unsigned int localUdpPort, Pin indicator_led_pin )
{
	this->SSID = SSID;
	this->password = password;
	local_udp_port = localUdpPort;
	Udp.begin( local_udp_port );
	Udp.setTimeout( 100 );

	device_listener = who_i_listen_to;
	header_data = initial_message;
	indicator_led = indicator_led_pin;
	indicator_led.Set_To_Output();
	indicator_led.Set( HIGH ); // Turn light off until it's connected

	Attempt_Connect_To_Router( SSID, password );
}

void Device_Communicator::Update()
{
	//Serial.print( "B" );
	if( !update_wait.Is_Ready() )
		return;

	//Serial.print( "C" );
	if( !Check_Wifi_Status() )
		return;

	Send_Client_Data( "PING\n" );

	//Serial.print( "D" );
	Check_For_New_Clients();
	delay(0);

	//Serial.print( "E" );
	Check_For_Disconnects();
	delay(0);

	//Serial.print( "F" );
	for( auto & c : active_clients )
	{
		// If client is saying something
		if( c.client.available() )
		{
			Read_Client_Data( c );
			c.timeout.Reset();
		}
	}
	delay(0);
	//Serial.print( "G" );
}

void Device_Communicator::Check_For_New_Clients()
{
	int packetSize = Udp.parsePacket();
	if( !packetSize )
		return;

	IPAddress incoming_ip = Udp.remoteIP();
	Serial.printf( "Received %d bytes from %s, port %d\n", packetSize, incoming_ip.toString().c_str(), Udp.remotePort() );
	char incomingPacket[ 256 ];
	int len = Udp.read( incomingPacket, 255 );
	if( len >= 0 )
	{
		incomingPacket[ len ] = '\0';
	}
	Serial.printf( "UDP packet contents: %s\n", incomingPacket );

	if( device_listener == incomingPacket )
		Serial.println( "Confirmed " + device_listener );
	else
		return;

	// Don't add duplicates
	bool already_connected = false;
	for( auto c_iterator = active_clients.begin(); c_iterator != active_clients.end(); ++c_iterator )
	{
		Connection & c = *c_iterator;
		if( c.ip == incoming_ip )
		{
			//Serial.println( c.ip.toString() + ": disconnected" );
			//c_iterator = active_clients.erase( c_iterator );
			already_connected = true;
		}
		//else
		//{
		//	++c_iterator;
		//}
	}
	if( already_connected )
	{
		Serial.println( "Avoiding adding duplicate: " + device_listener );
		return;
	}
	// Attempt to connect to ip that just pinged us
	active_clients.push_back( Connection{ incoming_ip } );
	Connection & c = active_clients.back();
	c.client.connect( incoming_ip, local_udp_port );
	if( c.client.connected() )
	{
		Serial.println( "Connected" );
		c.client.print( header_data );
		c.client.setTimeout( 10 ); // Only grab the data if it's ready already
	}
	else
	{
		c.client.stop();
		active_clients.pop_back();
		Serial.printf( "Failed to connect to %s\n", incoming_ip.toString().c_str() );
	}
}

void Device_Communicator::Check_For_Disconnects()
{
	// Find any connections to be removed
	auto to_be_removed =
		std::remove_if( active_clients.begin(), active_clients.end(),
		[]( Connection & c )
	{
		return !c.client.connected() || c.client.status() == CLOSED || c.timeout.Is_Ready();
	} );

	// Perform finishing functions on closing connections
	for( auto c = to_be_removed; c != active_clients.end(); ++c )
	{
		c->client.stop();
		if( !c->client.connected() )
			Serial.println( c->ip.toString() + ": disconnected" );
		else if( c->client.status() == CLOSED )
			Serial.println( c->ip.toString() + ": closed" );
		else
			Serial.println( c->ip.toString() + ": timed out" );
	}

	// Finally delete the connection entries
	active_clients.erase( to_be_removed, active_clients.end() );
}

void Device_Communicator::Connect_Controller_Listener( std::function<void( const Connection & c, const String & command )> callback )
{
	controller_callbacks.push_back( callback );
}

void Device_Communicator::Read_Client_Data( Connection & c )
{
	{ // Update partial message with new data coming in
		const int buffer_size = 256;
		char data_buffer[ buffer_size ];
		size_t bytes_read = c.client.readBytes( data_buffer, buffer_size - 1 );
		data_buffer[ bytes_read ] = '\0';
		int bytes_too_many = c.partial_message.length() + bytes_read - this->maximum_buffer_size;
		if( bytes_too_many > 0 ) // If we are too big, drop the oldest bytes
			c.partial_message = c.partial_message.substring( bytes_too_many, this->maximum_buffer_size );

		c.partial_message = c.partial_message + data_buffer;// +c.client.readString();
	}

	// Parse partial message as much as possible
	for( int end_of_line = c.partial_message.indexOf( '\n' );
		 end_of_line != -1;
		 end_of_line = c.partial_message.indexOf( '\n' ) )
	{ // wait for end of client's request, that is marked with an empty line
		String command = c.partial_message.substring( 0, end_of_line );
		c.partial_message = c.partial_message.substring( end_of_line + 1, c.partial_message.length() );

		for( auto callback : controller_callbacks )
			callback( c, command );
	}


}

void Device_Communicator::Send_Client_Data( const String & message )
{
	//Serial.println( "Clients listening = " + String(active_clients.size()) );
	for( auto & c : active_clients )
	{
		//Serial.print( "Sending data to this client: " );
		Send_Client_Data( c, message );
		//Serial.println( c.ip.toString() );
	}
}

void Device_Communicator::Send_Client_Data( Connection & c, const String & message )
{
	//Serial.print( "Sending to " + c.client.localIP().toString() + ": " + message );
	c.client.print( message );
}

std::map<int, const char*> error_to_string{
	{ WL_CONNECT_FAILED, "Failed to connect" },
	{ WL_CONNECTION_LOST, "Lost connection" },
	{ WL_DISCONNECTED, "Chose to disconnect" },
	{ WL_NO_SSID_AVAIL, "No connection to gateway" }
};
bool Device_Communicator::Check_Wifi_Status()
{
	bool wifi_is_connected = false;
	int wifi_status = WiFi.status();
	switch( wifi_status )
	{
		case WL_CONNECTED:
			if( !wifi_was_connected )
			{
				indicator_led.Set( LOW ); // Turn light on to show it's connected
				Serial.println( "WiFi connected" );
				Serial.println( "IP address: " );
				Serial.println( WiFi.localIP() );
				wifi_was_connected = true;
			}
			wifi_is_connected = true;
		break;
		case WL_CONNECT_FAILED:
		case WL_CONNECTION_LOST:
		case WL_DISCONNECTED:
		case WL_NO_SSID_AVAIL:
			if( wifi_was_connected )
			{
				Serial.print( "WiFi disconnected: " );
				Serial.println( error_to_string[ wifi_status ] );
				wifi_was_connected = false;
				retry_wifi = true;
				retry_wifi_timer.Reset();
			}
			if( disconnected_light_flash.Is_Ready() )
			{
				disconnected_light_flash.Reset();
				indicator_led.Toggle(); // Turn light on to show it's connected
			}
			if( retry_wifi && retry_wifi_timer.Is_Ready() )
				Attempt_Connect_To_Router( this->SSID, this->password );
			wifi_is_connected = false;
		break;
		default:
		Serial.print( "Wifi status not dealt with: " );
		Serial.println( WiFi.status() );
		break;
	}

	return wifi_is_connected;
}

void Device_Communicator::Attempt_Connect_To_Router( const char* router_ssid, const char* router_password )
{
	WiFi.disconnect( true );  //disconnect from wifi to set new wifi connection
	// We start by connecting to a WiFi network
	WiFi.begin( router_ssid, router_password );

	Serial.println();
	Serial.print( "Attempting to connect to WiFi: " );
	Serial.println( router_ssid );
	delay( 500 );
}


