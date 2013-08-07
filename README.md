Overview

Midsouthmakers RFID access based on a Beagle Bone Black and a cheap Chinese wiegand reader.


How To

Add user
You must be a part of the administrator group to add users.

Via the Keypad
Scan rfid token and enter your 4 digit pin.
Press the 3 key. Light will flash green and beep once completed. This will enter add user mode. Esc can be hit to exit this mode.
Scan the new RFID token, and have user enter a 4 digit pin.
Light will flash green and beep once completed successfully.

Via the Web Interface

Delete user
You must be a part of the administrator group to add users.

Via the Keypad
Scan rfid token and enter your 4 digit pin.
Press the 4 key. Light will flash green and beep once completed. This will enter add user mode. Esc can be hit to exit this mode.
Scan the new RFID token, and have user enter a 4 digit pin.
Light will flash green and beep once completed successfully.
Technical Details

Source Code
All Code is located on a github repository located at: https://github.com/bseishen/rfid_authentication/

Wiegand Reader
Reader is a 26bit Chinese special. MSB and LSB are parity bits, the remainging 24bits are the LSB of the RFID token. Couple of gotcha's with this reader. Numeric key presses are sent without parity and are only 16bits. Also buzzer lines or status led cannot be held low when trying to read data from the reader.

Panel
Pin Assignements
Relay 1 - GPIO_67 - Garage Door
Relay 2 - GPIO_66 - Front Door
Relay 3 - GPIO_44 - Buzzer
Relay 4 - GPIO_45 - Status LED
Relay 5 - GPIO_26 - UNASSIGNED
Relay 6 - GPIO_23 - UNASSIGNED
Relay 7 - GPIO_46 - UNASSIGNED
Relay 8 - GPIO_47 - Strobe
GPIO_68 - Wiegand D0
GPIO_69 - Wiegand D1
GPIO_02 - 1 wire for temperature sensor