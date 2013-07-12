/*
rfid.h
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

#ifndef _config_h
#define _config_h

/* Define --------------------------------------------------------------------*/
#define DOOR_GPIO          66 //output to relay pull to ground
#define GARAGE_GPIO        67 //output to relay pull to ground
#define WIEGAND_D0_GPIO    68 //open collector
#define WIEGAND_D1_GPIO    69 //open collector
#define STATUS_LED_GPIO    45 //pull to ground for Green, floating$
#define BUZZER_GPIO        44 //pull to ground to activate buzzer $



#endif /*_config_h */
