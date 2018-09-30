/***********************************************************************
 *  Arduino PinProm:
 *      Copyright (c) 2018 Christoph Schmid
 *
 ***********************************************************************
 *  This file is part of the FlipBits pinball EPROM programmer project:
 *  https://github.com/smyp/flipbits
 *
 *  FlipBits is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  FlipBits is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with afterglow.
 *  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************/
 
//------------------------------------------------------------------------------
/* This code assumes following pin layout for the Arduino Nano v3:
 *
 *  +----------+---------------+-----------+---------------+--------------+
 *  | Name     | Function      | Nano Pin# | Register, Bit | Mode         |
 *  +----------+---------------+-----------+---------------+--------------+
 *  | DATA     | 74HC595 Data  | A3        | DDRC, 3       | Output       |
 *  | LATCH    | 74HC595 Latch | A4        | DDRC, 4       | Output       |
 *  | CLOCK    | 74HC595 Clock | A5        | DDRC, 5       | Output       |
 *  | D0       | Data Bit 0    | D2        | DDRD, 2       | Input/Output |
 *  | D1       | Data Bit 1    | D3        | DDRD, 3       | Input/Output |
 *  | D2       | Data Bit 2    | D4        | DDRD, 4       | Input/Output |
 *  | D3       | Data Bit 3    | D5        | DDRD, 5       | Input/Output |
 *  | D4       | Data Bit 4    | D6        | DDRD, 6       | Input/Output |
 *  | D5       | Data Bit 5    | D7        | DDRD, 7       | Input/Output |
 *  | D6       | Data Bit 6    | D8        | DDRB, 0       | Input/Output |
 *  | D7       | Data Bit 7    | D9        | DDRB, 1       | Input/Output |
 *  | Pin32    | Vpp HighVolt  | D13       | DDRB, 5       | Output       |
 *  
 *  
 *  +----------+---------------+-----------+---------------+--------------+
*/

//-------------------------------------------------------------------------------------
// setup

// special pins
#define EPROM_PIN_A9 26
#define EPROM_PIN_A15 3
#define EPROM_PIN_OE 24
#define EPROM_PIN_VPP32 10
#define EPROM_VIRT_PIN_VPP 255    // PIN alias, variable physical assignment


// data buffer size [bytes]
#define BUF_SIZE 512


//-------------------------------------------------------------------------------------
// type definitions

typedef enum SUPPORTED_EPROMS_s
{
    EPROM_UNKNOWN = 0,    // unknown EPROM
    EPROM_W27C512 = 1,    // Winbond W27C512
    EPROM_W27E040 = 2,    // Winbond W27E040
    EPROM_W27C257 = 3,    // Winbond W27C257
    EPROM_LAST            // dummy to mark enum end
} SUPPORTED_EPROMS_t;

// manufacturer ids
typedef enum SUPPORTED_MIDS_s
{
    EPROM_MID_UNKNOWN = 0,     // unknown manufacturer
    EPROM_MID_WINBOND = 0xDA   // Winbond
} SUPPORTED_MIDS_t;

// product ids
typedef enum SUPPORTED_PIDS_s
{
    EPROM_PID_UNKNOWN = 0,     // unknown product id
    EPROM_PID_W27C257 = 0x02,  // Winbond W27C257
    EPROM_PID_W27C512 = 0x08,  // Winbond W27C512
    EPROM_PID_W27E040 = 0x86,  // Winbond W27E040
} SUPPORTED_PIDS_t;


//-------------------------------------------------------------------------------------
// data storage

uint32_t sAddress = 0;    // data address
bool sOE = true;          // output enable state
bool sCE = true;          // chip enable state
bool sVccOnP30 = false;   // enable power supply on pin 30 flag for 28 pin EPROMs
SUPPORTED_EPROMS_t sEpromType = EPROM_UNKNOWN;
uint32_t sEpromSize = 0;
SUPPORTED_MIDS_t sEpromMid = EPROM_MID_UNKNOWN;  // manufacturer id
SUPPORTED_PIDS_t sEpromPid = EPROM_PID_UNKNOWN;  // device id
char sEpromTypeName[16];

// data buffer
static byte sBuf[BUF_SIZE];


//-------------------------------------------------------------------------------------
void setup()
{   
    // initialize serial
    Serial.begin(115200); 
    
    // setup data pins
    DDRD &= B00000011;
    DDRB &= B11111100;

    // setup output pins
    pinMode(A2, OUTPUT);   // A15/Vpp high voltage enable
    pinMode(A3, OUTPUT);   // shift register data
    pinMode(A4, OUTPUT);   // shift register latch
    pinMode(A5, OUTPUT);   // shift register clock
    pinMode(10, OUTPUT);   // A9 high voltage enable
    pinMode(11, OUTPUT);   // OE high voltage enable
    pinMode(12, OUTPUT);   // Vpp high voltage enable

    // memory initialization
    memset(sBuf,0, sizeof(sBuf));

    // initialize all pins
    initPins();
}

//-------------------------------------------------------------------------------------
void readChip()
{
    // disable all high voltage lines
    disableHighVoltages();

    // configure the setup for the current EPROM
    configureEPROM();
    if (sEpromSize > 0)
    {       
        // read the full EPROM
        uint32_t bufPos = 0;
        for (uint32_t i=0; i<sEpromSize; i++)
        {
            // pull CE and OE high
            setCE(true);
            setOE(true);
            updatePins();
            delayMicroseconds(10);

            // program the address
            setAddress(i);
            updatePins();
            delayMicroseconds(10);

            // pull CE low
            setCE(false);
            updatePins();
            delayMicroseconds(10);

            // pull OE low
            setOE(false);
            updatePins();
            delayMicroseconds(10);

            // read the data
            sBuf[bufPos] = readData();
            bufPos++;

            // transmit the data once the buffer is full
            if (bufPos >= BUF_SIZE)
            {
                // serial output
                Serial.write(sBuf, bufPos);
                bufPos = 0;
            }
        }
    }

    // reset state
    initPins();
}

//-------------------------------------------------------------------------------------
bool writeChip()
{
    bool res = false;

    // configure the setup for the current EPROM
    // this powers up <32 pin devices
    configureEPROM();

    if (sEpromSize > 0)
    {
        // configure data pins as output
        DDRD |= B11111100;
        DDRB |= B00000011;
        
        // disable all high voltage lines
        disableHighVoltages();

        // pull CE and OE high
        setCE(true);
        setOE(true);
        updatePins();
        delay(10);

        // activate 12V on Vpp
        setHighVoltage(12);
        enableHighVoltage(EPROM_VIRT_PIN_VPP, true);

        // flush serial data
        while (Serial.available())
        {
            Serial.read();
        }
        
        // ready for data
        Serial.println("WRITE READY");
        Serial.setTimeout(4000);

        uint32_t a = 0;
        while (a < (sEpromSize-1))
        {
            // fill the write buffer
            int br = 0;
            br = Serial.readBytes(sBuf, BUF_SIZE);
            if (br == BUF_SIZE)
            {
                // burn the buffer to the EPROM
                for (uint32_t bi=0; bi<br; bi++)
                {
                    uint8_t b = sBuf[bi];

                    // skip 0xff bytes, an empty EPROM is all 0xff
                    if (b != 0xff)
                    {
                        // set the address
                        setAddress(a);
                        updatePins();
                            
                        // set the data byte
                        PORTD = (PORTD & B00000011) | (b << 2);
                        PORTB = (PORTB & B11111100) | (b >> 6);
                            
                        // program the byte
                        delayMicroseconds(2); 
                        setCE(false);
                        updatePins();
                        delayMicroseconds(100);  // TPWP
                        setCE(true);
                        updatePins();
                        delayMicroseconds(2);    // TDH
                    }

                    // next address
                    a++;
                }
            }

            // acknowledge the data reception, ready for more
            Serial.println("READY");
        }

        // disable all high voltage lines
        disableHighVoltages();

        res = true;
    }

    // success
    Serial.println(res ? "WRITE COMPLETE" : "WRITE FAILED");

    // reset state
    initPins();

    return res;
}

//-------------------------------------------------------------------------------------
bool eraseChip()
{
    bool res = false;

    // configure the setup for the current EPROM
    // this powers up <32 pin devices
    configureEPROM();

    // erase is only supported for Winbond EEPROMs
    if ((sEpromSize > 0) && (sEpromMid == EPROM_MID_WINBOND))
    {
        // disable all high voltage lines
        disableHighVoltages();

        // activate 14V on Vpp and A9
        setHighVoltage(14);
        enableHighVoltage(EPROM_PIN_A9, true);
        enableHighVoltage(EPROM_VIRT_PIN_VPP, true);

        // pull CE and OE high
        setCE(true);
        setOE(true);

        // pull all adress lines low
        setAddress(0);
        updatePins();

        // pull data lines high
        DDRD |= B11111100;
        DDRB |= B00000011;
        PORTD |= B11111100;
        PORTB |= B00000011;

        delayMicroseconds(2);

        // pull CE low for 100ms
        setCE(false);
        updatePins();
        delay(100);
        setCE(true);
        updatePins();

        // disable all high voltage lines again
        disableHighVoltages();
      
        res = true;
    }

    // reset state
    initPins();

    return res;
}

//-------------------------------------------------------------------------------------
void identifyEprom(void)
{
    sEpromType = EPROM_UNKNOWN;

    // disable all high voltage lines
    disableHighVoltages();

    // pull CE and OE low
    setOE(false);
    setCE(false);

    // activate Vcc on pin 30 in case this is a 28 pin EPROM
    sVccOnP30 = true;

    // set A0 low for manufacturer id
    setAddress(0);

    // apply
    updatePins();
    delay(10);

    // activate 12V on A9
    setHighVoltage(12);
    enableHighVoltage(EPROM_PIN_A9, true);

    // read the manufacturer id
    delay(10);
    byte mid = readData();

    // set A0 high for product id
    setAddress(0x00000001);
    updatePins();

    // read the product id
    delay(10);
    byte pid = readData();

    // evaluate
    if (mid == EPROM_MID_WINBOND)
    {
        switch (pid)
        {
            case EPROM_PID_W27C257:
                sEpromMid = (SUPPORTED_MIDS_t)mid;
                sEpromPid = (SUPPORTED_PIDS_t)pid;
                sEpromType = EPROM_W27C257;
                break;
            case EPROM_PID_W27C512:
                sEpromMid = (SUPPORTED_MIDS_t)mid;
                sEpromPid = (SUPPORTED_PIDS_t)pid;
                sEpromType = EPROM_W27C512;
                break;
            case EPROM_PID_W27E040:
                sEpromMid = (SUPPORTED_MIDS_t)mid;
                sEpromPid = (SUPPORTED_PIDS_t)pid;
                sEpromType = EPROM_W27E040;
                break;
            default:
                sEpromMid = EPROM_MID_UNKNOWN;
                sEpromPid = EPROM_PID_UNKNOWN;
                break;
        }
    }

    // reset state
    initPins();
}

//-------------------------------------------------------------------------------------
void loop()
{
    // los!
    Serial.println("FlipBits 1.0 - ready");

    // wait for command
    String cmd = "";
    bool complete = false;
    while(true)
    { 
        char character;
    
        // read data
        while(Serial.available())
        {
            character = Serial.read();
            if (character != ':')
            {
                cmd.concat(character);
            }
            else
            {
                complete = true;
            }
        }
        
        if (complete)
        {
            // help
            if (cmd == "h")
            {
                Serial.println("r: - read complete EPROM");
                Serial.println("w: - write complete EPROM");
                Serial.println("e: - electrically erase a W27Cxxx chip");
                Serial.println("s<bytes>: - set the EPROM size in bytes");
                Serial.println("ve: - Verify that the EPROM is empty");
            }

            // identify the EPROM chip
            if (cmd == "i")
            {
                identifyEprom();
                configureEPROM();
                Serial.print("EPROM mid ");
                Serial.print(sEpromMid);
                Serial.print(" pid ");
                Serial.print(sEpromPid);
                Serial.print(" type ");
                Serial.print(sEpromType);
                Serial.print(" name ");
                Serial.print(sEpromTypeName);
                Serial.print(" size ");
                Serial.println(sEpromSize);
            }
            
            // read chip
            if (cmd == "r")
            {
                // read the EPROM
                readChip();
            }
            
            // write chip
            if (cmd == "w")
            {
                // program the EPROM
                writeChip();
            }
            
            // erase chip
            if (cmd == "e")
            {
                // erase the EPROM
                Serial.println("ERASING THE WHOLE EPROM!");
                if (eraseChip())
                {
                    Serial.println("ERASE COMPLETE");
                }
                else
                {
                    Serial.println("ERASE FAILED");
                }
            }
            
            // set the EPROM type
            if (cmd[0] == 't')
            {
                // parse the EPROM size
                uint32_t type = strtol(&cmd[1], NULL, 10);
                if (type < EPROM_LAST)
                {
                    sEpromType = (SUPPORTED_EPROMS_t)type;
                    Serial.print("EPROM type set to ");
                    Serial.println(type);
                }
                else
                {
                    Serial.print("ERR: Invalid EPROM type: ");
                    Serial.println(type);
                }
            }

            // reset the command
            complete = false;
            cmd = "";
        }
        
        delay(20);
    } 
}

//-------------------------------------------------------------------------------------
void setAddress(uint32_t a)
{
    // remember the address written
    sAddress = a;
}

//-------------------------------------------------------------------------------------
void setOE(bool state)
{
    sOE = state;
}

//-------------------------------------------------------------------------------------
void setCE(bool state)
{
    sCE = state;
}

//-------------------------------------------------------------------------------------
void updatePins()
{
    // this assumes data on A3, latch on A4 and clock on A5

    // data is composed of address + auxiliary pins
    uint32_t data = sAddress;
    if (sOE)
    {
        // OE on shift register output 19
        data |= (1UL << 19);
    }
    if (sCE)
    {
        // CE on shift register output 20
        data |= (1UL << 20);
    }

    // pin 30 (A17) Vcc handling (enable 5V supply for 28 pin EPROMs)
    if (sVccOnP30)
    {
        data |= (1UL << 17);
    }

    // A17 needs to be inverted as it is driving a PNP transistor in order to supply
    // enough power when A17 is in fact VCC of a 27512 EPROM
    data ^= (1UL << 17);

    // pull RCLK and CLK low to start sending data
    PORTC &= B11001111;
   
    // clock out all 24 bits of data
    for (uint32_t bitMask=0x800000; bitMask>0; bitMask>>=1)
    {
        PORTC &= B11011111; // CLK low
        if (data & bitMask)
        {
            PORTC |= B00001000; // set data bit
        }
        else
        {
            PORTC &= B11110111; // clear data bit
        }
        PORTC |= B00100000; // CLK high
    }

    PORTC &= B11011111; // CLK low

    // pull RCLK high to latch the data
    PORTC |= B00010000;
}

//-------------------------------------------------------------------------------------
byte readData(void)
{
    // Q0-7 on D2-D9
    byte data;
    data = ((PIND & B11111100) >> 2);
    data |= ((PINB & B00000011) << 6);
    return data;
}

//-------------------------------------------------------------------------------------
bool setHighVoltage(uint8_t voltage)
{
    // voltage switch on D10
    if (voltage == 12)
    {
        PORTB |= B00000100; // set data bit
        return true;
    }
    else if (voltage == 14)
    {
        PORTB &= B11111011; // clear data bit
        return true;
    }

    Serial.print("ERR: Invalid voltage: ");
    Serial.println(voltage);
    return false;
}

//-------------------------------------------------------------------------------------
bool enableHighVoltage(byte epromPin, bool state)
{
    if (epromPin == EPROM_PIN_VPP32)
    {
        // Vpp for 32 pin EPROMs
        // Vpp high voltage control switch on D13
        if (state)
        {
            PORTB |= B00100000; // set data bit
        }
        else
        {
            PORTB &= B11011111; // clear data bit
        }
        return true;
    }
    else if (epromPin == EPROM_PIN_OE)
    {
        // Vpp on OE for 28 pin EPROMs
        // OE high voltage control switch on D12
        if (state)
        {
            PORTB |= B00010000; // set data bit
        }
        else
        {
            PORTB &= B11101111; // clear data bit
        }
        return true;
    }
    else if (epromPin == EPROM_PIN_A9)
    {
        // A9 program/erase voltage switch
        // A9 high voltage control on D11
        if (state)
        {
            PORTB |= B00001000; // set data bit
        }
        else
        {
            PORTB &= B11110111; // clear data bit
        }
        return true;
    }
    else if (epromPin == EPROM_PIN_A15)
    {
        // A15 Vpp switch
        // A15 high voltage control on A2
        if (state)
        {
            PORTC |= B00000100; // set data bit
        }
        else
        {
            PORTC &= B11111011; // clear data bit
        }
        return true;
    }
    else if (epromPin == EPROM_VIRT_PIN_VPP)
    {
        // virtual pin, check EPROM configuration for assignment
        switch (sEpromType)
        {
            case EPROM_W27C257: return enableHighVoltage(EPROM_PIN_A15, state);    // Vpp on pin 3
            case EPROM_W27C512: return enableHighVoltage(EPROM_PIN_OE, state);    // Vpp on OE
            case EPROM_W27E040: return enableHighVoltage(EPROM_PIN_VPP32, state); // Vpp on pin 32
            default: break;
        }
    }
    Serial.print("ERR: Invalid high voltage pin: ");
    Serial.println(epromPin);
    return false;
}

//-------------------------------------------------------------------------------------
void disableHighVoltages()
{
    enableHighVoltage(EPROM_PIN_A9, false);
    enableHighVoltage(EPROM_PIN_A15, false);
    enableHighVoltage(EPROM_PIN_OE, false);
    enableHighVoltage(EPROM_PIN_VPP32, false);
}

//-------------------------------------------------------------------------------------
void configureEPROM()
{
    if (sEpromType == EPROM_UNKNOWN)
    {
        // try to autodetect the EPROM
        identifyEprom();
    }

    // configure the EPROM
    uint32_t size = 0;
    switch (sEpromType)
    {
        case EPROM_W27C257:
            sEpromSize = 32768;
            sVccOnP30 = true;
            strcpy(sEpromTypeName, "W27C257");
            break;
        case EPROM_W27C512:
            sEpromSize = 65536;
            sVccOnP30 = true;
            strcpy(sEpromTypeName, "W27C512");
            break;
        case EPROM_W27E040:
            sEpromSize = 524288;
            sVccOnP30 = false;
            strcpy(sEpromTypeName, "W27E040");
            break;
        default: 
            sEpromSize = 0;
            strcpy(sEpromTypeName, "Unknown");
            break;
    }

    // initialize the pins
    setOE(true);
    setCE(true);
    setAddress(0);
    updatePins();
}

//-------------------------------------------------------------------------------------
void initPins()
{
    // disable all high voltage lines
    disableHighVoltages();

    // activate the 'safer' 12V
    setHighVoltage(12);

    // initialize data pins
    setAddress(0);
    setCE(true);
    setOE(true);
    updatePins();

    // configure data pins as output
    DDRD &= B00000011;
    DDRB &= B11111100;
}

