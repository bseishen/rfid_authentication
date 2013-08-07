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
#include <sys/time.h>
#include "config.h"
#include "gpio.h"
#include "log.h"
#include "sqlite3.h"

#define DEBUG_WIEGAND 1
#define DEBUG	1

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

unsigned char clear_reader = 0;  		//bit flag used to tell the wiegand routine to flush any temp data.



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
	char error[255];
	unsigned char charTemp;

	gpio_d0 = gpio_fd_open(WIEGAND_D0_GPIO);
	gpio_d1 = gpio_fd_open(WIEGAND_D1_GPIO);
 
	memset((void*)fdset, 0, sizeof(fdset));

	fdset[0].fd = gpio_d0;
	fdset[0].events = POLLPRI;

	fdset[1].fd = gpio_d1;
	fdset[1].events = POLLPRI;

	//This is needed to flush the first false interrupt
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

		gettimeofday(&tvEnd, NULL);

		//packet data over the wiegand is either 26bits for rfid read or 8 bits for a key press.
		//if time from the last packet is under 5ms, treat the last packet as an rfid packet.
		if(lastPacketType == 1){
			timersub(&tvEnd, &reader.tvPacket, &tvDiff);
			timePacket = (tvDiff.tv_sec) * 1000 + (tvDiff.tv_usec) / 1000 ;
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
				//If an esc key was pressed, clear reader.
				if(reader.keys[0] == 10 || reader.keys[1] == 10 || reader.keys[2] == 10 || reader.keys[3] == 10){
					clear_reader = 1;
				}
			}
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

		//this has been added to flush wiegand trash that shows up from EMI from the door latch.
		if(clear_reader == 2){
			temp = 0;
			readerCount = 0;
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
#ifdef DEBUG
				printf("KEY PRESSES EXCEEDED 5 PRESSES, FLUSHING KEY BUFFER\n");
#endif
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
#ifdef DEBUG
					printf("4 Keys in Buffer \n");
#endif
				}
				if(keyCount == 4){
					reader.status |= STATUS_AUX_OPTIONS;
#ifdef DEBUG
					printf("5 Keys in Buffer \n");
#endif
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
			keyCount = 0;
			reader.keys[0] = 0;
			reader.keys[1] = 0;
			reader.keys[2] = 0;
			reader.keys[3] = 0;
			reader.keys[4] = 0;
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
	char error[255];
	sqlite3_stmt *stmt;
	sqlite3 *handle;
	struct timeval tvNow, tvDifference, tvAux, tvUnlock;
	int timeMS;
	int keyBuff;


	//init ports
	gpio_init();

	//init tvUnlock, last time of door unlock.
	gettimeofday(&tvUnlock, NULL);

	//init DB
    retval = sqlite3_open(DB_PATH, &handle);
    if(retval)
    {
		sprintf(error,"Sqlite DB failed to INIT!");
		log_err(error);
        return -1;
    }

    sprintf(query,"CREATE TABLE IF NOT EXISTS users (key INTERGER PRIMARY KEY,hash TEXT NOT NULL, ircName TEXT, spokenName TEXT, addedBy TEXT, dateCreated INTERGER, isAdmin INTERGER, lastLogin INTERGER, isActive INTERGER)");
    retval = sqlite3_exec(handle,query,0,0,0);


    //Thread off wiegand poller
	if (pthread_create(&(tid[1]), NULL, &poll_wiegand, NULL)){
		sprintf(error,"Wiegand thread failed to start.");
		log_err(error);
	}


	fflush(stdout);

	while(1){
		usleep(500000);

		//get time passed since last wiegand packet
		gettimeofday(&tvNow, NULL);
		timersub(&tvNow, &tvUnlock, &tvDifference);
		timeMS = (tvDifference.tv_sec) * 1000 + (tvDifference.tv_usec) / 1000 ;
#ifdef DEBUG
		printf("Timer: %d",timeMS);
#endif
		///Clear if timeout has exceeded threshold. Lock everything!
		if(timeMS>TIMEOUT_DOORLOCK && userVerified > 0){
			lock_door();
			led_off();
			beep_off();
			printf("DOOR LOCKING!!!!!!!!!!!!!!\n");
			clear_reader = 1;
			userVerified = 0;
		}
#ifdef DEBUG
		//printf("READER STATUS: %d \n",reader.status);
#endif
		//Authenticate User
		if(userVerified == 0 && clear_reader == 0 &&(reader.status == (STATUS_RFID_READY|STATUS_KEYS_READY) || reader.status == (STATUS_RFID_READY|STATUS_KEYS_READY|STATUS_AUX_OPTIONS))){
			keyBuff = (reader.keys[0]*1000)+(reader.keys[1]*100)+(reader.keys[2]*10)+reader.keys[3];
			sprintf(query, "SELECT * from users WHERE key='%d'" , reader.rfid);
			retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
			if(retval){
				sprintf(error,"Query returned and error.");
				log_err(error);
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
							sprintf(error,"SQLite shit the bed.");
							log_err(error);
				        }
				    }

				sprintf(buffer, "%d", keyBuff);
				result = crypt(buffer, hash);
				sprintf(query, "SELECT * from users WHERE key='%d'" , reader.rfid);
				retval = sqlite3_exec(handle,query,0,0,0);
				if(retval){
					sprintf(error,"Query returned and error.");
					log_err(error);
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
							sprintf(error,"SQLite shit the bed.");
							log_err(error);
							return -1;
						}
					}
				}


				if(userVerified > 0){
					sprintf(error,"Access granted to user %d", reader.rfid);
					log_err(error);
					//clear temp buffer and grab last rfid key
					lastKey=reader.rfid;
					//Possibly need to clear reader temp

					//Update User's last login time
					sprintf(query, "UPDATE users SET lastLogin='%d' WHERE key='%d'" ,(int)tvNow.tv_sec, reader.rfid);
					retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
					if(retval){
						sprintf(error,"Query returned and error.");
						log_err(error);
					}

					retval = sqlite3_step(stmt);

					gettimeofday(&tvUnlock, NULL);

					unlock_door();
					usleep(500);
					clear_reader = 2;
					//led_on();

					//TODO:log user in logs, notify irc, play welcome message.

				}
				else{
					sprintf(error,"ACCESS DENIED to user %d", reader.rfid);
					log_err(error);
					clear_reader = 1;
					led_blink(2);
				}
			}
		}

		//delete user (using 4 key on keypad)
		if(userVerified == 2 && reader.keys[4]==4){
			lastKey = reader.rfid;
			sprintf(error,"Delete user mode entered by user ID: %d", reader.rfid);
			log_err(error);
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
			if(retval || reader.rfid == lastKey){
				sprintf(error,"Delete user mode exited by %d. No user deleted.",reader.rfid);
				log_err(error);
			}
			else{
				sprintf(query, "DELETE FROM users WHERE key='%d'" , reader.rfid, hash);
				retval = sqlite3_prepare_v2(handle,query,-1,&stmt,0);
				retval = sqlite3_step(stmt);

				if(retval == SQLITE_DONE){
					sprintf(error,"User %d deleted from DB by User %d",reader.rfid, lastKey);
					log_err(error);
				}

				beep_on();
				sleep(1);
				beep_off();
			}

			clear_reader = 1;
			fflush(stdout);
		}
#ifdef ADD_FIRST_USER
		userVerified = 2;
		reader.keys[4]=11;
#endif
		//add new user (using 3 key on keypad)
		if(userVerified == 2 && reader.keys[4]==3){
			lastKey = reader.rfid;

			sprintf(error,"Add user mode entered by user %d",reader.rfid);
			log_err(error);

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
			if(retval || reader.rfid == lastKey){
				sprintf(error,"Add user mode exited, no users were added.");
				log_err(error);
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
					sprintf(error,"User %d was added to the DB by user %d", reader.rfid, lastKey);
					log_err(error);
				}

				beep_on();
				sleep(1);
				beep_off();

			}
			clear_reader = 1;
		}

		//OpenGarageDoor (using number 2 on keypad)
		if(userVerified > 0 && reader.keys[4]==2){
			sprintf(error,"Garage door toggled by user %d", reader.rfid);
			log_err(error);
			clear_reader = 1;
			toggle_garage();
		}


		fflush(stdout);

	}

	pthread_join(tid[1], NULL);

	sqlite3_close(handle);

	return 0;
}

