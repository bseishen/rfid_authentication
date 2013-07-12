/*
Copyright (c) 2012 Ben S. Eishen
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include "sqlite3.h"
#include <sys/time.h>
#include "config.h"
#include "gpio.h"

//#define DEBUG_WIEGAND 1

#define MAX_BUF 64
#define TIMEOUT_MS 5000
#define MAX_WIEGAND_PACKET_LENGTH_MS 5
#define WIEGAND_KEY_LENGTH 8
#define RFID_LOG "RFID"


pthread_t tid[1];

struct reader_t {
	char status;				//This is a bit mask field. 0x01-RFID_READY, 0x02-KEYS_READY, 0x04-AUX_OPTIONS_READY
	char userVerified; 			//0- Not verified, 1-verified, 2-verified as admin
	unsigned int rfid; 			//RFID key that was read
	char keys[5]; 				//Key presses stored in this array
	char keyCount; 				//How many keys have been pressed
	char readerCount; 			//Wiegand bits read in the packet.
	unsigned int temp; 			//wiegand buffer
	char lastPacketType; 		//1 if the last detected action was a keypress. 0 if it thinks its an rfid read
	struct timeval tvPacket; 	//time since the last wiegand packet was recivied, used to timeout the session for inactivity.
};

static struct reader_t reader;

/**
 * Nulls the Strut type reader.
 */
void clear_reader(){
	reader.status=0;
	reader.userVerified=0;
	reader.rfid=0;
	reader.keys[0]=0;
	reader.keys[1]=0;
	reader.keys[2]=0;
	reader.keys[3]=0;
	reader.keys[4]=0;
	reader.keyCount=0;
	reader.temp=0;
	reader.readerCount=0;
	reader.lastPacketType = 0;
}

/**
 * Activates the beeper on the reader.
 */
void beep_on(){
	gpio_set_value(BUZZER_GPIO, 0);
}

/**
 * Turns off the beeper on the reader.
 */
void beep_off(){
	gpio_set_value(BUZZER_GPIO, 1);
}

/**
 * Activates the led on the reader.
 */
void led_on(){
	gpio_set_value(STATUS_LED_GPIO, 0);
}

/**
 * Turns off the led on the reader.
 */
void led_off(){
	gpio_set_value(STATUS_LED_GPIO, 1);
}

/**
 * Turns off and on the led on the reader.
 */
void led_blink(int times){
	int i;
	for(i=0; i<times; i++){
		led_on();
		sleep(.5);
		led_off(.5);
		sleep(.5);
	}
}

/**
 * Self Explanatory.
 */
void unlock_door(){
	gpio_set_value(DOOR_GPIO, 0);
}

/**
 * Self Explanatory.
 */
void lock_door(){
	gpio_set_value(DOOR_GPIO, 1);
}

/**
 * Toggles Garage door.
 */
void toggle_garage(){
	gpio_set_value(GARAGE_GPIO, 0);
	sleep(1);
	gpio_set_value(GARAGE_GPIO, 1);
}

/**
 * Poll_wiegand polls D0 and D1 for data and fills the reader structure.
 */
void* poll_wiegand(void *arg){
	struct pollfd fdset[2];
	struct timeval tvEnd, tvDiff;
	int nfds = 2;
	int gpio_d0, gpio_d1, timePacket, rc;
	char *buf[MAX_BUF];
	unsigned char charTemp;

	gpio_d0 = gpio_fd_open(WIEGAND_D0_GPIO);
	gpio_d1 = gpio_fd_open(WIEGAND_D1_GPIO);

	clear_reader();
 
	memset((void*)fdset, 0, sizeof(fdset));

	fdset[0].fd = gpio_d0;
	fdset[0].events = POLLPRI;

	fdset[1].fd = gpio_d1;
	fdset[1].events = POLLPRI;

	read(fdset[0].fd, buf, MAX_BUF);
	read(fdset[1].fd, buf, MAX_BUF);

	fflush(stdout);

	while (1) {
		gettimeofday(&reader.tvPacket, NULL);

		rc = poll(fdset, nfds, -1);

		if (rc < 0) {
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_ERR, "Pin intterupt poll failed, fatal error.");
			closelog();
			return -1;
		}

		gettimeofday(&tvEnd, NULL);
		timersub(&tvEnd, &reader.tvPacket, &tvDiff);
		timePacket = (tvDiff.tv_sec) * 1000 + (tvDiff.tv_usec) / 1000 ;

		//packet data over the wiegand is either 26bits for rfid read or 8 bits for a keypress.
		//if time from the last packet is under 5ms, treat the last packet as an rfid packet.
		if(reader.lastPacketType == 1){
			reader.lastPacketType=0;
			if(timePacket < MAX_WIEGAND_PACKET_LENGTH_MS){
				//delete all key presses
				reader.keyCount=0;
				reader.keys[0]=0;
				reader.keys[1]=0;
				reader.keys[2]=0;
				reader.keys[3]=0;
				reader.keys[4]=0;
			}
			else{
				//last packet was a key press
				reader.temp=0;
				reader.readerCount=0;
				reader.lastPacketType=0;
			}
		}


		if (fdset[0].revents & POLLPRI) {
			read(fdset[0].fd, buf, MAX_BUF);
			reader.temp = reader.temp << 1;
			reader.readerCount++;

		}

		if (fdset[1].revents & POLLPRI) {
			read(fdset[1].fd, buf, MAX_BUF);
			reader.temp = reader.temp << 1;
			reader.temp |= 1;
			reader.readerCount++;
		}


		///Key was pressed, add it to the array
		if(reader.readerCount==WIEGAND_KEY_LENGTH){
			if(reader.keyCount>5){
				reader.keyCount=0;
			}

			switch(reader.temp){
				case 0xE1:
					charTemp = 1;
					break;
				case 0xD2:
					charTemp = 2;
					break;
				case 0xC3:
					charTemp = 3;
					break;
				case 0xB4:
					charTemp = 4;
					break;
				case 0xA5:
					charTemp = 5;
					break;
				case 0x96:
					charTemp = 6;
					break;
				case 0x87:
					charTemp = 7;
					break;
				case 0x78:
					charTemp = 8;
					break;
				case 0x69:
					charTemp = 9;
					break;
				case 0xF0:
					charTemp = 0;
					break;
				case 0x5A:
					charTemp = 10;
					break;
				case 0x4B:
					charTemp = 11;
					break;
				default:
					charTemp = 0xFF; //DATA WAS GARBAGE
			}

			if(charTemp!=0xFF){
				reader.keys[reader.keyCount] = charTemp;
				//set status bit for keyRead is ready
				if(reader.keyCount == 3){
					reader.status = reader.status | 0x02;
				}
				if(reader.keyCount == 4){
					reader.status = reader.status | 0x04;
				}
			}

			reader.keyCount++;
			reader.lastPacketType = 1;
		}

		///RFID data is present, store it.
		if(reader.readerCount >= 26){
			reader.rfid=reader.temp;
			reader.status = reader.status | 0x01;
			reader.readerCount=0;
			reader.temp=0;
		}

#ifdef DEBUG_WIEGAND
		printf("RFID: %X \n", reader.rfid);
		printf("KEYCOUNT: %X \n", reader.keyCount);
		printf("KEY1: %X \n", reader.keys[0]);
		printf("KEY2: %X \n", reader.keys[1]);
		printf("KEY3: %X \n", reader.keys[2]);
		printf("KEY4: %X \n", reader.keys[3]);
		printf("KEY5: %X \n", reader.keys[4]);
		printf("STATUS: %X \n\n", reader.status);
#endif



		fflush(stdout);

	}

	gpio_fd_close(gpio_d0);
	gpio_fd_close(gpio_d1);

}

int main(int argc, char **argv, char **envp)
{

	int retval;
	unsigned int lastKey;
	char *result;
	char buffer[20];
	char hash[40] = "$1$DeaDBeeF$NanaNanaBooBoo//DooDoo";
	char query[200];
	sqlite3_stmt *stmt;
	sqlite3 *handle;
	struct timeval tvNow, tvDifference;
	int timeMS;
	int keyBuff;


	//init ports
	gpio_init();


	//init DB
    retval = sqlite3_open("/var/www/rfid.sqlite",&handle);
    if(retval)
    {
		openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
		syslog (LOG_ERR, "Sqlite DB failed to INIT!");
		closelog();
        return -1;
    }

    sprintf(query,"CREATE TABLE IF NOT EXISTS users (key INTERGER PRIMARY KEY,hash TEXT NOT NULL, ircName TEXT, spokenName TEXT, addedBy TEXT, dateCreated INTERGER, isAdmin INTERGER, lastLogin INTERGER, isActive INTERGER)");
    retval = sqlite3_exec(handle,query,0,0,0);


    //Thread off wiegand poller
	if (pthread_create(&(tid[1]), NULL, &poll_wiegand, NULL)){
		openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
		syslog (LOG_ERR, "Wiegand thread failed to start.");
		closelog();
	}


	fflush(stdout);

	while(1){
		usleep(500000);

		//get time passed since last wiegand packet
		gettimeofday(&tvNow, NULL);
		timersub(&tvNow, &reader.tvPacket, &tvDifference);
		timeMS = (tvDifference.tv_sec) * 1000 + (tvDifference.tv_usec) / 1000 ;

		///Clear if timeout has exceeded threshold. Lock everything!
		if(timeMS>TIMEOUT_MS){
			clear_reader();
			lock_door();
			led_off();
			beep_off();
			gettimeofday(&reader.tvPacket, NULL);
		}


		//Authenticate User
		if(reader.userVerified == 0 && (reader.status == 3 || reader.status == 7)){
			keyBuff = (reader.keys[0]*1000)+(reader.keys[1]*100)+(reader.keys[2]*10)+reader.keys[3];
			sprintf(query, "SELECT * from users WHERE key='%d'" , reader.rfid);
			retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
			if(retval){
				openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
				syslog (LOG_NOTICE, "Query returned and error.");
				closelog();
			}
			else{

				while(1)
				    {
				        retval = sqlite3_step(stmt);

				        if(retval == SQLITE_ROW)
				        {
				        	const char *val = (char *)sqlite3_column_text(stmt,1);
				        	sprintf(hash, "%s", val);
				        }
				        else if(retval == SQLITE_DONE)
				        {
				            // All rows finished
				            break;
				        }
				        else
				        {
				            // Some error encountered
							openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
							syslog (LOG_ERR, "SQLite shit the bed.");
							closelog();
				        }
				    }

				sprintf(buffer, "%d", keyBuff);
				result = crypt(buffer, hash);
				sprintf(query, "SELECT * from users WHERE key='%d'" , reader.rfid);
				//printf(query);
				retval = sqlite3_exec(handle,query,0,0,0);
				if(retval){
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_NOTICE, "Query returned and error.");
					closelog();
				}
				else{
					while(1){
						// fetch a row's status
						retval = sqlite3_step(stmt);

						if(retval == SQLITE_ROW){
				        	const char *val = (char *)sqlite3_column_text(stmt,1);
				        	sprintf(hash, "%s", val);
				        	if(strcmp(hash,result)==0){
				        		//Check if user is active.
				        		if(sqlite3_column_int(stmt,8)){
				        			reader.userVerified = 1;
				        			//Check if user is an admin.
				        			if(sqlite3_column_int(stmt,6)){
				        				reader.userVerified = 2;
				        			}
				        		}
				        	}
						}
						else if(retval == SQLITE_DONE){
							// All rows finished
							break;
						}
						else{
							// Some error encountered
							openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
							syslog (LOG_ERR, "SQLite shit the bed.");
							closelog();
							return -1;
						}
					}
				}


				if(reader.userVerified > 0){
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_NOTICE, "Access granted to user %d", reader.rfid);
					closelog();
					//clear temp buffer and grab last rfid key
					lastKey=reader.rfid;
					reader.temp=0;

					//Update User's last login time
					sprintf(query, "UPDATE users SET lastLogin='%d' WHERE key='%d'" ,(int)tvNow.tv_sec, reader.rfid);
					retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
					if(retval){
						openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
						syslog (LOG_NOTICE, "Query returned and error.");
						closelog();
					}

					retval = sqlite3_step(stmt);


					unlock_door();
					//led_on();

					//TODO:log user in logs, notify irc, play welcome message.

				}
				else{
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_NOTICE, "Access denied to user %d", reader.rfid);
					closelog();
					clear_reader();
					led_blink(2);
				}
			}
		}

		//delete user (using ESC key on keypad)
		if(reader.userVerified == 2 && reader.keys[4]==10){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Delete user mode entered by user ID:%d",reader.rfid);
			closelog();
			clear_reader();
			beep_on();
			sleep(1);
			beep_off();

			//Wait for rfid to be scanned. Cancels if ESC key is pressed.
			while(reader.status!=1){
				if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10)
					break;
				sleep(.1);
			}

			//if ESC key was pressed or if admin key was scanned twice.
			if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10 || reader.rfid == lastKey){
				openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
				syslog (LOG_INFO, "Delete user mode exited. No user deleted.",reader.rfid);
				closelog();
			}
			else{
				sprintf(query, "DELETE FROM users WHERE key='%d'" , reader.rfid, hash);
				retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
				retval = sqlite3_step(stmt);

				if(retval == SQLITE_DONE){
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_INFO, "User %d deleted from DB by User %d",reader.rfid, lastKey);
					closelog();
				}

				beep_on();
				sleep(1);
				beep_off();
			}

			clear_reader();
			fflush(stdout);
		}
		//Comment out the 2 lines below to add a user for the first time!!
		//reader.userVerified = 2;
		//reader.keys[4]=11;
		//add new user (using ENT key on keypad)
		if(reader.userVerified == 2 && reader.keys[4]==11){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Add user mode entered by user %d",reader.rfid);
			closelog();

			clear_reader();
			beep_on();
			sleep(1);
			beep_off();


			//Wait for rfid and keys to be pressed for the new user. Cancels if ESC key is pressed.
			while(reader.status!=3){
				if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10)
					break;
				sleep(.1);

			}

			//if ESC key was pressed
			if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10){
				openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
				syslog (LOG_INFO, "Add user mode exited, no users were added.");
				closelog();
			}
			else{
				unsigned long seed[2];
			    char salt[] = "$1$........";
			    const char *const seedchars ="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
			    int i;

			    /* Generate a (not very) random seed.
			       You should do it better than this... */
			    seed[0] = time(NULL);
			    seed[1] = getpid() ^ (seed[0] >> 14 & 0x30000);

			    /* Turn it into printable characters from `seedchars'. */
			    for (i = 0; i < 8; i++)
			      salt[3+i] = seedchars[(seed[i/5] >> (i%5)*6) & 0x3f];


				keyBuff = (reader.keys[0]*1000)+(reader.keys[1]*100)+(reader.keys[2]*10)+reader.keys[3];

				sprintf(buffer, "%d", keyBuff);
				result = crypt(buffer, salt);

				sprintf(query, "INSERT INTO users VALUES('%d','%s','','','%d','%d',0,0,1)" , reader.rfid, result, lastKey, (int)tvNow.tv_sec);
				//printf(query);
				retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
				retval = sqlite3_step(stmt);

				if(retval == SQLITE_DONE){
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_INFO, "User %d was added to the DB by user %d", reader.rfid, lastKey);
					closelog();
				}

				beep_on();
				sleep(1);
				beep_off();

			}
			clear_reader();
		}

		//OpenGarageDoor (using number 2 on keypad)
		if(reader.userVerified > 0 && reader.keys[4]==2){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Garage door toggled by user %d", reader.rfid);
			closelog();
			clear_reader();
			toggle_garage();
		}


		fflush(stdout);

	}

	pthread_join(tid[1], NULL);

	sqlite3_close(handle);

	return 0;
}

