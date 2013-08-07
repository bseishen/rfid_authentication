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

#define DEBUG_WIEGAND 1

#define STATUS_NULL 		0x00
#define STATUS_RFID_READY	0x01
#define STATUS_KEYS_READY	0x02
#define STATUS_AUX_OPTIONS	0x04


pthread_t tid[1];

struct reader_t {
	unsigned char status;				//This is a bit mask field. 0x01-RFID_READY, 0x02-KEYS_READY, 0x04-AUX_OPTIONS_READY
	unsigned int  rfid; 				//RFID key that was read
	unsigned char keys[5];				//Key presses stored in this array
	struct timeval tvPacket; 			//time since the last wiegand packet was recivied, used to timeout the session for inactivity.
};

struct reader_t reader;

unsigned char clear_reader = 0;  //bit flag used to tell the wiegand routine to flush any temp data.



/**
 * Poll_wiegand polls D0 and D1 for data and fills the reader structure.
 */

void* poll_wiegand(void *arg){
	struct pollfd fdset[2];
	struct timeval tvEnd, tvDiff;
	int nfds = 2;
	int lastPacketType = 0;
	int keyCount=0;
	int temp=0;
	int readerCount=0;
	int gpio_d0, gpio_d1, timePacket, rc;
	char *buf[MAX_BUF];
	char *error[255];
	unsigned char charTemp;

	gpio_d0 = gpio_fd_open(WIEGAND_D0_GPIO);
	gpio_d1 = gpio_fd_open(WIEGAND_D1_GPIO);
 
	memset((void*)fdset, 0, sizeof(fdset));

	fdset[0].fd = gpio_d0;
	fdset[0].events = POLLPRI;

	fdset[1].fd = gpio_d1;
	fdset[1].events = POLLPRI;

	//This is needed to flush the first false interupt
	read(fdset[0].fd, buf, MAX_BUF);
	read(fdset[1].fd, buf, MAX_BUF);

	clear_reader = 1;

	while (1) {
		gettimeofday(&reader.tvPacket, NULL);

		rc = poll(fdset, nfds, -1);

		if (rc < 0) {
			sprintf(error,"Pin interrupt poll failed, fatal error.");
			log_err(error);
		}

		if(clear_reader == 1){
			reader.status = STATUS_NULL;
			reader.rfid = 0;
			reader.keys[0] = 0;
			reader.keys[1] = 0;
			reader.keys[2] = 0;
			reader.keys[3] = 0;
			reader.keys[4] = 0;
			temp = 0;
			readerCount = 0;
			lastPacketType = 0;
			keyCount = 0;
			clear_reader = 0;
		}

		gettimeofday(&tvEnd, NULL);
		timersub(&tvEnd, &reader.tvPacket, &tvDiff);
		timePacket = (tvDiff.tv_sec) * 1000 + (tvDiff.tv_usec) / 1000 ;

		//packet data over the wiegand is either 26bits for rfid read or 8 bits for a keypress.
		//if time from the last packet is under 5ms, treat the last packet as an rfid packet.
		if(lastPacketType == 1){
			lastPacketType = 0;
			if(timePacket < MAX_WIEGAND_PACKET_LENGTH_MS){
				//Delete all key presses. This is a rfid token being read.
				keyCount = 0;
				reader.keys[0] = 0;
				reader.keys[1] = 0;
				reader.keys[2] = 0;
				reader.keys[3] = 0;
				reader.keys[4] = 0;
			}
			else{
				//Last packet was a key press.
				temp = 0;
				readerCount = 0;
			}
		}


		if (fdset[0].revents & POLLPRI) {
			read(fdset[0].fd, buf, MAX_BUF);
			temp = temp << 1;
			readerCount++;
		}

		if (fdset[1].revents & POLLPRI) {
			read(fdset[1].fd, buf, MAX_BUF);
			temp = temp << 1;
			temp |= 1;
			readerCount++;
		}


		///Key was pressed, add it to the array
		if(readerCount==WIEGAND_KEY_LENGTH){
			if(keyCount>5){
				keyCount=0;
				reader.status  &= ~(STATUS_KEYS_READY|STATUS_AUX_OPTIONS);
				printf("KEY BUFFER WAS FLUSHED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
				fflush(stdout);
			}

			switch(temp){
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
				reader.keys[keyCount] = charTemp;
				//set status bit for keyRead is ready
				if(keyCount == 3){
					reader.status |= STATUS_KEYS_READY;
					printf("key status to ready capt'n \n");
				}
				if(keyCount == 4){
					reader.status |= STATUS_AUX_OPTIONS;
					printf("5th key ready!!\n");
				}
			}

			keyCount++;
			lastPacketType = 1;
		}

		///RFID data is present, store it.
		if(readerCount == 26){
			//Mask the MSB off and Shift out the LSB. These are the even and odd parity.
			reader.rfid=(temp>>1) & 0x00FFFFFF;
			reader.status |= STATUS_RFID_READY;
			readerCount = 0;
			temp = 0;
		}

		if(readerCount > 26){
			//something went wrong
			clear_reader = 1;
		}

#ifdef DEBUG_WIEGAND
		printf("RFID: %X \n", reader.rfid);
		printf("KEYCOUNT: %X \n", keyCount);
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
	unsigned char userVerified = 0; 			//0- Not verified, 1-verified, 2-verified as admin
	unsigned int lastKey;
	char *result;
	char buffer[20];
	char hash[40] = "$1$DeaDBeeF$NanaNanaBooBoo//DooDoo";
	char query[200];
	sqlite3_stmt *stmt;
	sqlite3 *handle;
	struct timeval tvNow, tvDifference, tvAux;
	int timeMS;
	int keyBuff;


	//init ports
	gpio_init();


	//init DB
    retval = sqlite3_open(DB_PATH, &handle);
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

		printf("Timer: %d",timeMS);

		///Clear if timeout has exceeded threshold. Lock everything!
		if(timeMS>TIMEOUT_DOORLOCK){
			userVerified = 0;
			clear_reader = 1;
			lock_door();
			led_off();
			beep_off();
			printf("DOOR LOCKING!!!!!!!!!!!!!!\n");
		}

		//printf("READER STATUS: %d",reader.status);
		//Authenticate User
		if(userVerified == 0 && clear_reader == 0 &&(reader.status == (STATUS_RFID_READY|STATUS_KEYS_READY) || reader.status == (STATUS_RFID_READY|STATUS_KEYS_READY|STATUS_AUX_OPTIONS))){
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
				        			userVerified= 1;
				        			//Check if user is an admin.
				        			if(sqlite3_column_int(stmt,6)){
				        				userVerified = 2;
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


				if(userVerified > 0){
					openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
					syslog (LOG_NOTICE, "Access granted to user %d", reader.rfid);
					closelog();
					//clear temp buffer and grab last rfid key
					lastKey=reader.rfid;
					//Possibly need to clear reader temp

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
					clear_reader = 1;
					led_blink(2);
				}
			}
		}

		//delete user (using ESC key on keypad)
		if(userVerified == 2 && reader.keys[4]==10){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Delete user mode entered by user ID:%d",reader.rfid);
			closelog();
			clear_reader = 1;
			beep_on();
			sleep(1);
			beep_off();

			//Wait for rfid to be scanned. Cancels if ESC key is pressed.
			while(reader.status!=STATUS_RFID_READY){
				if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10){
					retval = 1;
					break;
				}

				gettimeofday(&tvAux, NULL);
				timersub(&tvAux, &tvNow, &tvDifference);
				timeMS = (tvDifference.tv_sec) * 1000 + (tvDifference.tv_usec) / 1000 ;
				if(timeMS > 20000){
					retval = 1;
					break;
				}

				sleep(.1);

			}

			//if ESC key was pressed or if admin key was scanned twice.
			if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10 || reader.rfid == lastKey){
				openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
				syslog (LOG_INFO, "Delete user mode exited by %d. No user deleted.",reader.rfid);
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

			clear_reader = 1;
			fflush(stdout);
		}
#ifdef ADD_FIRST_USER
		reader.userVerified = 2;
		reader.keys[4]=11;
#endif
		//add new user (using ENT key on keypad)
		if(userVerified == 2 && reader.keys[4]==11){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Add user mode entered by user %d",reader.rfid);
			closelog();

			clear_reader = 1;
			beep_on();
			sleep(1);
			beep_off();

			retval = 0;
			//Wait for rfid and keys to be pressed for the new user. Cancels if ESC key is pressed or takes longer than 20sec.
			while(reader.status!=(STATUS_RFID_READY|STATUS_KEYS_READY)){
				if(reader.keys[0]==10 || reader.keys[1]==10 ||reader.keys[2]==10 ||reader.keys[3]==10 ||reader.keys[4]==10){
					retval = 1;
					break;
				}

				gettimeofday(&tvAux, NULL);
				timersub(&tvAux, &tvNow, &tvDifference);
				timeMS = (tvDifference.tv_sec) * 1000 + (tvDifference.tv_usec) / 1000 ;
				if(timeMS > 20000){
					retval = 1;
					break;
				}

				sleep(.1);

			}

			//if ESC key was pressed
			if(retval){
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

#ifdef ADD_FIRST_USER
				sprintf(query, "INSERT INTO users VALUES('%d','%s','','','%d','%d',1,0,1)" , reader.rfid, result, lastKey, (int)tvNow.tv_sec);
#else
				sprintf(query, "INSERT INTO users VALUES('%d','%s','','','%d','%d',0,0,1)" , reader.rfid, result, lastKey, (int)tvNow.tv_sec);
#endif
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
			clear_reader = 1;
		}

		//OpenGarageDoor (using number 2 on keypad)
		if(userVerified > 0 && reader.keys[4]==2){
			openlog (RFID_LOG, LOG_AUTH, LOG_NOTICE);
			syslog (LOG_INFO, "Garage door toggled by user %d", reader.rfid);
			closelog();
			clear_reader = 1;
			toggle_garage();
		}


		fflush(stdout);

	}

	pthread_join(tid[1], NULL);

	sqlite3_close(handle);

	return 0;
}

