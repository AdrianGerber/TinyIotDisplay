/*
 * main.c
 *
 * Created: 25.12.2016
 * Author : Adrian Gerber
 */ 

 #define F_CPU 8000000UL

 #include <avr/io.h>
 #include <util/delay.h>
 #include <avr/interrupt.h>
 #include <avr/pgmspace.h>
 #include <stdlib.h>

 /*Define for UART Baud rate*/
 #define BAUD 9600
 #define UBRRVAL (F_CPU/16/BAUD-1)


 /*Defines for the 4 digit 7 segment display*/

 #define DISP_7_A_BIT PB6
 #define DISP_7_B_BIT PB5
 #define DISP_7_C_BIT PB0
 #define DISP_7_D_BIT PB2
 #define DISP_7_E_BIT PB3
 #define DISP_7_F_BIT PB4
 #define DISP_7_G_BIT PB1

 #define A (1<<DISP_7_A_BIT)
 #define B (1<<DISP_7_B_BIT)
 #define C (1<<DISP_7_C_BIT)
 #define D (1<<DISP_7_D_BIT)
 #define E (1<<DISP_7_E_BIT)
 #define F (1<<DISP_7_F_BIT)
 #define G (1<<DISP_7_G_BIT)

 #define DISP_7_PORT PORTB
 #define DISP_7_DDR DDRB

 #define DISP_7_DSP_MASK 0x7F

 #define DISP_4_1_BIT PD4
 #define DISP_4_1_DDR DDRD
 #define DISP_4_1_PORT PORTD
 
 #define DISP_4_2_BIT PD5
 #define DISP_4_2_DDR DDRD
 #define DISP_4_2_PORT PORTD

 #define DISP_4_3_BIT PB7
 #define DISP_4_3_DDR DDRB
 #define DISP_4_3_PORT PORTB

 #define DISP_4_4_BIT PD6
 #define DISP_4_4_DDR DDRD
 #define DISP_4_4_PORT PORTD


 /*Defines for using the ESP*/
 #define ESP_on() (PORTD |= (1<<PD2))
 #define ESP_off() (PORTD &= ~(1<<PD2))
 #define ESP_puts_P(x) USART_puts_P(x)
 #define ESP_putc(x) USART_putc(x)

 #define ESP_STARTUP PSTR("ATE0\nAT+CIPMODE=1\n")
 #define ESP_CONNECT1 PSTR("AT+CIPSTART=\"TCP\",\"")
 #define ESP_CONNECT2 PSTR("\",80\r\n")
 #define ESP_START_SEND PSTR("AT+CIPSEND\r\n")
 #define ESP_STOP_SEND PSTR("\r\n\r\n+++")
 #define ESP_HTTP_GET PSTR("GET ")
 #define ESP_HTTP_END1 PSTR(" HTTP/1.1\nHost: ")
 #define ESP_HTTP_END2 PSTR("\n")
 #define ESP_DISCONNECT PSTR("AT+CIPCLOSE\r\n\r\n")

 #define THINGSPEAK_HOST PSTR("api.thingspeak.com")
 #define THINGSPEAK_REQUEST_URL PSTR("/apps/thinghttp/send_request?api_key=SuperSecretKey")

 /*Ticks of timer 0 required for a delay of about 1ms*/ 
 #define TIM0_CMPA_VAL 31

 #define TIM1_CMPA_INTERVAL_VAL 31250
 #define TIM1_CMPA_INTERVAL_MS 1000

 /*pattern of segments to be activated for each decimal number*/
 const uint8_t bcd_code[10] = {A|B|C|D|E|F, B|C, A|B|G|E|D, A|B|C|D|G, B|C|F|G, A|F|G|C|D, A|F|G|E|D|C, A|B|C, A|B|C|D|E|F|G, A|B|C|D|F|G};
 /*Other Defines for the Display*/
 #define DISP_BLANK 10
 #define DISP_BLANK_VAL 0x00

 #define DISP_WAIT 11
 #define DISP_WAIT_VAL G


 #define FAIL_LIMIT 3
 #define UPDATE_DELAY_x100s 72

 #define WARN_DDR DDRD
 #define WARN_PORT PORTD
 #define WARN_BIT PD3
 #define WARN_PIN PIND

 /*the current value of each digit*/
 volatile uint8_t digitValues[4] = {0, 0, 0, 0};


 /*Displays 'number' (0-9) on the specified 'digit' (0-3) */
 void show_number(uint8_t digit, uint8_t number);
 
 /*Writes String from Programm Memory to the USART*/
 void USART_puts_P(const char *stringToWrite);
 /*Writes char from RAM to USART*/
 void USART_putc(unsigned char data);

 /*Waits until either "stringToCompare" was received by the USART, ignoring all other leading characters, or the timeout has been reached*/
 uint8_t compReceived(char *stringToCompare, uint32_t timeout);
 /*Extracts a number from USART, ignores leading '\r' or '\n', reads until non number character is found, max number size is 999999999*/
 int32_t getNumber(uint32_t timeout);
 /*Connects the esp to a host (String in Programm Memory)*/
 void esp_connect(const char *host){
	 USART_puts_P(ESP_CONNECT1);
	 USART_puts_P(host);
	 USART_puts_P(ESP_CONNECT2);
 }
 /*Reads monthly views from Socialblade using ThingHTTP Service to extract the relevant number*/
 /* ESP8266 --GET-> Thingspeak ThingHTTP --GET-> Socialblade Views ---> Thingspeak ---> ESP8266*/ 
 int32_t esp_getViews();
 /*Organizes the digits of a number and puts it into the correct global variables, in order to be displayed*/
 void disp_num(int32_t num){

	if(num > 9999) num = 9999;
	if(num < 0){
		for(uint8_t i = 0; i < 4; i++) digitValues[i] = DISP_WAIT;

		return;
	}
	
	digitValues[3] = num % 10;
	digitValues[2] = num / 10 % 10;
	digitValues[1] = num / 100 % 10;
	digitValues[0] = num / 1000 % 10;
 }


 /*Interrupt routine for multiplexing the display*/
 /*Lights each digit for about 1ms*/
 ISR(TIMER0_COMPA_vect){
	static uint8_t currentDigit = 0;

	/*display the stored number on the currently activated digit*/
	show_number(currentDigit, digitValues[currentDigit]);
	/*Increase the variable storing the active digit --> next digit will be displayed on next call*/
	currentDigit++;
	if(currentDigit >= 4) currentDigit = 0;
	/*Add the interval to OCR0A's value, so this routine will be called again with the correct timing*/
	OCR0A += TIM0_CMPA_VAL;
 }

int main(void)
{
	/*Disable Interrupts*/
	cli();

	/*Set up I/O*/
	/*7 Segment Display*/
	DISP_7_DDR |= (1<<DISP_7_A_BIT) | (1<<DISP_7_B_BIT) | (1<<DISP_7_C_BIT) | (1<<DISP_7_D_BIT);
	DISP_7_DDR |= (1<<DISP_7_E_BIT) | (1<<DISP_7_F_BIT) | (1<<DISP_7_G_BIT);

	DISP_4_1_DDR |= (1<<DISP_4_1_BIT);
	DISP_4_2_DDR |= (1<<DISP_4_2_BIT);
	DISP_4_2_DDR |= (1<<DISP_4_2_BIT);
	DISP_4_2_DDR |= (1<<DISP_4_2_BIT);

	/*ESP RESET/CH_PD*/
	DDRD |= (1<<PD2);

	/*Warn LED*/
	WARN_DDR |= (1<<WARN_BIT);

	/*Configure UART*/
	/*Set the baud rate registers to the desired values*/
	UBRRH = (unsigned char)(UBRRVAL>>8);
	UBRRL = (unsigned char) UBRRVAL;
	/*Enable receiver and transmitter*/
	UCSRB = (1<<RXEN)|(1<<TXEN);
	/* Set frame format: 8data, 2stop bit */
	UCSRC = (1<<USBS)|(3<<UCSZ0);

	/*Configure Timer 0*/
	/*Set Start Value for the Output Compare Register A --> Interrupt about every 1ms*/
	OCR0A = TIM0_CMPA_VAL;
	/*Enable Corresponding Interrupt*/
	TIMSK |= (1<<OCIE0A);
	/*Prescaler 1:256*/
	TCCR0B |= (1<<CS02);

	/*Enable Interrupts*/
	sei();

	/*Local variables*/
	int32_t viewsOK = 0;
	int32_t viewsNow = 0;
	uint8_t failedTries = 0;

	uint8_t alarmFlag = 0;

	/*Not enough free flash space for displaying the initial '-- --' (99.8% full)*/
	//disp_num(-1);

	while(1){

		WARN_PORT |= (1<<WARN_BIT);
		viewsNow = esp_getViews();

		/*If the ESP8266 couldn't get the new view count, keep displaying the last one known to be OK*/
		/*If there is no success updating the view count after 3 tries, display an error*/
		if(viewsNow < 0){
			failedTries++;

			if(failedTries > FAIL_LIMIT){
				disp_num(-1);
				alarmFlag = 1;
			}
			else{
				disp_num(viewsOK);
				alarmFlag = 1;
			}


		}
		/*If the views were successfuly updated, store and display them, reset the count of failed tries*/
		else{
			viewsOK = viewsNow;
			alarmFlag = 0;
			failedTries = 0;
			disp_num(viewsOK);
		}


		WARN_PORT &= ~(1<<WARN_BIT);

		if(alarmFlag == 1){
			WARN_PORT |= (1<<WARN_BIT);
		}

		_delay_ms(7200000);
		

// 		/*Wait the specified amount of time before refreshing again*/
// 		for(uint8_t delayCounter = UPDATE_DELAY_x100s; delayCounter > 0; delayCounter--){
// 			_delay_ms(100000);
// 			if(alarmFlag == 1){
// 				WARN_PIN |= (1<<WARN_BIT);
// 			}
// 		}
		WARN_PORT &= ~(1<<WARN_BIT);
	}
		
}
	

void show_number(uint8_t digit, uint8_t number){
	/*Set all I/O Pins connected to the display to 0V*/
	DISP_4_1_PORT &= ~(1<<DISP_4_1_BIT);
	DISP_4_2_PORT &= ~(1<<DISP_4_2_BIT);
	DISP_4_3_PORT &= ~(1<<DISP_4_3_BIT);
	DISP_4_4_PORT &= ~(1<<DISP_4_4_BIT);
	DISP_7_PORT &= ~(0xFF & DISP_7_DSP_MASK);

	/*Return if invalid arguments were received*/
	if(digit >= 4) return;

	/*Output the selected number pattern*/
	if(number < 10) DISP_7_PORT |= bcd_code[number] & DISP_7_DSP_MASK;
	else{
		if(number == DISP_BLANK) DISP_7_PORT |= DISP_BLANK_VAL;
		else if(number == DISP_WAIT) DISP_7_PORT |= DISP_WAIT_VAL;
	}

	/*Activate the selected display*/
	switch(digit){
		case 0:
			DISP_4_1_PORT |= (1<<DISP_4_1_BIT);
		break;
		case 1:
			DISP_4_2_PORT |= (1<<DISP_4_2_BIT);
		break;
		case 2:
			DISP_4_3_PORT |= (1<<DISP_4_3_BIT);
		break;
		case 3:
			DISP_4_4_PORT |= (1<<DISP_4_4_BIT);
		break;
	}

}

void USART_puts_P(const char *stringToWrite){

	uint8_t stringIndex = 0;
	/*Read the first character from the string in program memory*/
	char stringData = pgm_read_byte(&(stringToWrite[stringIndex]));

	/*Print the contents of the string, until the \0 is found*/
	while(stringData){

		/*Send the current character*/
		USART_putc(stringData);

		/*Read the next character from program memory*/
		stringIndex++;
		stringData = pgm_read_byte(&(stringToWrite[stringIndex]));

	}

	return;
}
void USART_putc(unsigned char data){
	/*Wait until the last byte has been sent*/
	while(!(UCSRA & (1<<UDRE)));
	
	/*send data*/
	UDR = data;
}
int32_t getNumber(uint32_t timeout){

	/*counts up to detect when the time limit has been reached*/
	uint32_t timeoutTick = 0;

	/*Convert ms into x*20us*/
	timeout *= 50;

	/*Array to store the number in it's ascii representation*/ 
	char tmp[10];

	for(uint8_t counter = 0; counter < 10; counter++) tmp[counter] = 'z';
	tmp[9] = '\0'; /*Put '\0' in the last field of the array, to cancel the conversion, should a number > 999999999 be received*/

	/*Variable for the current position in the array*/
	uint8_t tmp_index = 0;

	/*Wait for the first digit (Ignore leading '\n' or '\r'*/
	while(tmp_index == 0 && ++timeoutTick <= timeout){
		/*Check if data is available*/
		if(UCSRA & (1<<RXC)){
			//USART_puts_P(PSTR("\nTEST2\n"));
			tmp[0] = UDR;
			//USART_putc(tmp[0]);
			if(tmp[0] >= '0' && tmp[0] <= '9'){
				tmp_index++;
				//USART_puts_P(PSTR("START"));
			}
		}
		_delay_us(20);
	}

	/*Keep reading the number, while the time limit hasn't been reached and no '\0' is seen in the buffer*/
	while(tmp[tmp_index] != '\0' && ++timeoutTick <= timeout){
		//USART_puts_P(PSTR("\nTEST3\n"));

		/*Check if data is available*/
		if(UCSRA & (1<<RXC)){
			tmp[tmp_index] = UDR;
			/*If a valid digit is received, store it and move on to the next position*/
			if(tmp[tmp_index] == ','){

			}
			else if(tmp[tmp_index] >= '0' && tmp[tmp_index] <= '9'){
				tmp_index++;
			}
			/*Otherwise quit searching for new digits*/
			else{
				tmp[tmp_index] = '\0';
			}
		}
		_delay_us(20);
	}
			
	/*Check if a valid string was extracted*/
 	if(tmp[tmp_index] != '\0'){
		return(-1);
	}
	if(tmp[0] == '\0'){
		return(-1);
	}

	
	/*Return the converted string*/
	return(atol(tmp));
}
uint8_t compReceived(char *stringToCompare, uint32_t timeout){
	/*Stores how many chars were correct*/
	uint8_t correctCount = 0;
	/*counts up to detect when the time limit has been reached*/
	uint32_t timeoutTick = 0;

	/*Convert ms into x*50us*/
	timeout *= 20;

	/*Check, as long as the time limit hasn't been reached and the string isn't over*/
	while(*stringToCompare != '\0' && ++timeoutTick <= timeout){
		/*Check if data is available*/
		if(UCSRA & (1<<RXC)){
			
			/*store the data*/
			char tmp = UDR;
			/*compare it with the expected value*/
			if(tmp == *stringToCompare){
				/*data was identical, keep track of how many chars were correct, go to the next char*/
				stringToCompare++;
				correctCount++;
			}
			else for(; correctCount > 0; correctCount--) stringToCompare--;
		}
		_delay_us(50);
	}


	if(*stringToCompare == '\0') return(1);
	else return(0);
}
int32_t esp_getViews(){
	int32_t new_views = -1;

	ESP_on();
	_delay_ms(4000);

	/*Configure the ESP*/
	ESP_puts_P(ESP_STARTUP);
	compReceived("OK", 1000);
	compReceived("OK", 1000);

	/*Connect to the host*/
	esp_connect(THINGSPEAK_HOST);
	compReceived("Linked", 2000);

	ESP_puts_P(ESP_START_SEND);
	compReceived("<", 2000);

	ESP_puts_P(ESP_HTTP_GET);
	ESP_puts_P(THINGSPEAK_REQUEST_URL);
	ESP_puts_P(ESP_HTTP_END1);
	ESP_puts_P(THINGSPEAK_HOST);
	ESP_puts_P(ESP_HTTP_END2);
	ESP_puts_P(ESP_STOP_SEND);


	
	/*Response is OK if it looks like this
	
	...
	Date: Mon, 26 Dec 2016 20:36:35 GMT\r\n
	Server: nginx/1.9.3 + Phusion Passenger 4.0.57\r\n
	\r\n
	\r\n
	9999\r\n
	\r\n
	...

	-->Waiting for "Server" and then for two times '\r' will get us close to the number, getNumber will ignore any remaining "\r\n"s 
	-->in front of the number and read until the next "\r\n".
	*/
	
	/*Check if a valid response was given by the ESP8266, if so, convert and store the number*/
	if(compReceived("Server", 5000) &&  compReceived("\r", 5000) && compReceived("\r", 5000)){
		new_views = getNumber(5000);
	}
	/*If no valid response was given, return -1*/
	else new_views = -1;

	/*Disconnect from the Server*/
	ESP_puts_P(ESP_DISCONNECT);
	_delay_ms(1000);

	/*And put the esp into reset/sleep mode*/
	ESP_off();

	return(new_views);
}