//current MVP behavior is:
    //Arduino knows current time
    //Arduino has a medication schedule
    //Arduino checks schedule automatically
    //Arduino rotates carousel at the correct times
    //Arduino remembers carousel position

//Still needed:
    //OLED display buttons for user input
    //LEDs/buzzer
    //calibration of exact slot movement (needs to be tested)
    //way to reset carousel


//RTC includes
#include <Wire.h>
// Lets Arduino communicate using I2C.
#include <RTClib.h>
// Provides simple functions for using the RTC.
#include <Stepper.h>
//control the 28BYJ-48 stepper motor through the ULN2003

RTC_DS3231 rtc;
// Creates an object named "rtc" to reference the RTC module.


// motor pins
//ex: 
    //Arduino Pin 8  → ULN2003 IN1
    //Arduino Pin 9  → ULN2003 IN2
    //Arduino Pin 10 → ULN2003 IN3
    //Arduino Pin 11 → ULN2003 IN4
const int IN1 = 8;
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;

//MEDICATION SCHEDULE MANAGER
const int NUMBER_OF_DOSES = 3;
int doseHours[NUMBER_OF_DOSES] = {8, 13, 20};
int doseMinutes[NUMBER_OF_DOSES] = {0, 0, 0};
// dispenses at 8am,1pm,8pm

// Motor Settings
const int STEPS_PER_REVOLUTION = 2048;
// Number of steps for one complete revolution of the output shaft.
// The gearbox increases the total to about 2048 steps.
const int NUMBER_OF_SLOTS = 16;
// slots in the carousel
const int STEPS_PER_SLOT = 128;
// steps to reach next slot


Stepper motor(STEPS_PER_REVOLUTION, IN1, IN3, IN2, IN4);
//creates motor object to reference later takes in parameter input of all 4 pins

//variables
bool dispensedToday = false;
//"Have I already dispensed this dose?"
int currentSlot = 0;
//tracks where carousel is currently


void setup()
{

    Serial.begin(9600);
    // Starts Serial Monitor.
    
    motor.setSpeed(3);
    // Sets motor speed to 3 RPM.
    
    if (!rtc.begin())
    //"Is rtc connected?"
    {
    Serial.println("RTC not found");
    //prints error
    while (1)
    {
        // Stop here if the RTC isn't connected.
        //freezes program
    }
    }    

}


void loop()
{
    DateTime now = rtc.now();
    //gets current date and time
    printTime(now);
    //displays time
    checkMedicationTime(now);
    //"Is it time to dispense?"
    delay(1000);

}


// FUNCTIONS

void printTime(DateTime now)
//prints current time
{
    Serial.print(now.hour());
    Serial.print(":");
    Serial.println(now.minute());
}


void dispenseMedication()
//
{
    motor.step(STEPS_PER_SLOT);
    //moves current compartment to next compartment

    currentSlot++;
    //updates variable 

    if(currentSlot >= NUMBER_OF_SLOTS)
    {
        currentSlot = 0;
        //reset back to beginning
    }
    Serial.print("Current Slot: ");
    Serial.println(currentSlot);
}


void checkMedicationTime(DateTime now)
//checks if its time to dispense medication
{
    for(int i = 0; i < NUMBER_OF_DOSES; i++)
    {
        if(now.hour() == doseHours[i] &&
           now.minute() == doseMinutes[i] &&
           !dispensedToday)
        {
            dispenseMedication();

            dispensedToday = true;
        }
    }
     if(now.minute() != 0)
    {
        dispensedToday = false;
    }
}


