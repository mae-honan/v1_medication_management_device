
// includes
#include <Arduino.h>
#include <Wire.h>
// Lets Arduino communicate using I2C.
#include <RTClib.h>
// RTC
#include <Stepper.h>
// STEPPER
#include <Adafruit_GFX.h>
// OLED
#include <Adafruit_SSD1306.h>
// OLED

RTC_DS3231 rtc;
// Creates an object named "rtc" to reference the RTC module.

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// screen dimensions

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// creates OLED object named display
// uses the screen width and height
// uses I2C communication through Wire
// -1 means there is no reset pin connected

// DRV8833 motor pins
const int AIN1 = 8;
const int AIN2 = 9;
const int BIN1 = 10;
const int BIN2 = 11;

// Button, buzzer, LEDs (dose alert / confirmation)
const int BUTTON_PIN = 30;
// "Button 1" on the schematic -- dose-taken confirm
const int BUZZER_PIN = 24;
const int RED_LED = 23;
const int GREEN_LED = 22;

// schedule buttons
const int PLUS_BUTTON = 31;
// "Button 2"
const int MINUS_BUTTON = 32;
// "Button 3"
const int CONFIRM_BUTTON = 33;
// "Button 4"

// 17HS19-2004S1: 1.8 deg/step, full-step mode
const int STEPS_PER_REVOLUTION = 200;

//  16 alternating sectors: 8 pill-holding + 8 blocking.
// A blocking sector normally sits over the drop hole; dispensing rotates
// onto the next pill sector (drop), then onto the following blocking
// sector (reseal) -- see dispenseMedication().
const int NUMBER_OF_SECTORS = 16;
const int NUMBER_OF_PILL_SLOTS = NUMBER_OF_SECTORS / 2;


// anchor time for the first dose of the day; later doses are this time
// plus the configured hours-between-doses offsets
const int MED_HOUR = 8;
const int MED_MINUTE = 0;

// testing
// When true, the dispenser fires a dose alert on a short repeating interval
// instead of following the real daily schedule above...for quickly
// verifying the LED/buzzer/OLED/motor without waiting on the clock
// Set to false to switch to the real MED_HOUR/MED_MINUTE + activeHoursBetween schedule.
const bool TESTING_MODE = true;
const unsigned long TEST_INTERVAL_SECONDS = 30;

// Maximum number of doses supported (can be changed, rotates infinitely CLOCKWISE)
const int MAX_DOSES = 10;

// settings - active
int activeDosesPerDay = 1;
int activeHoursBetween[MAX_DOSES - 1] = {8, 8, 8};

// settings - pending
// Buttons 1 and 2 change these values.
int pendingDosesPerDay = 1;
int pendingHoursBetween[MAX_DOSES - 1] = {8, 8, 8};

// 0 = editing number of doses
// 1 = editing dose 1 -> dose 2
// 2 = editing dose 2 -> dose 3
// 3 = editing dose 3 -> dose 4
int currentSetting = 0;

// Used for schedule button edge detection
bool previousPlusState = HIGH;
bool previousMinusState = HIGH;
bool previousConfirmState = HIGH;

// debounce timing
unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 200;

Stepper motor(STEPS_PER_REVOLUTION, AIN1, AIN2, BIN1, BIN2);

// variables
bool dispensedToday = false;
int currentPillSlot = 0;
bool waitingForConfirmation = false;
// true when medication is dispensed and we are waiting for user to press button

double idealSectorPosition = 0.0;
// running fractional position (in ideal, non-integer steps) -- see stepOneSector()
long actualStepsCommanded = 0;
// how many real steps have actually been sent to the motor so far

DateTime todaysDoseTimes[MAX_DOSES];
// computed each day from MED_HOUR:MED_MINUTE + activeHoursBetween offsets
int dosesGivenToday = 0;
// index into todaysDoseTimes of the next dose still owed today
int lastScheduleDay = -1;
// used to detect day or a settings change and recompute the schedule

DateTime nextDoseTime;
// the next time an alert will fire (real schedule or test interval)
// kept up to date so the idle screen can show a countdown


void printTime(DateTime now);
void showCurrentTime(DateTime now);
void printAmPmTime(int hour24, int minute);
void takeDoseScreen();
void nextDoseScreen(int hour, int minute);
void stepOneSector();
void dispenseMedication();
void triggerDoseAlert();
void computeTodaysDoseSchedule(DateTime now);
void checkMedicationTime(DateTime now);
void checkButton();
void checkScheduleButtons();
bool buttonWasPressed(bool currentState, bool previousState);
void increaseCurrentSetting();
void decreaseCurrentSetting();
void confirmCurrentSetting();
void savePendingSettings();
void printPendingSetting();
void printActiveSettings();


void setup()
{
    Serial.begin(9600);
    // Starts Serial Monitor.

    Wire.begin();
    // starts I2C communication

    //button,buzzer,led setup
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    // Sets button pin as an input.
    // Uses Arduino's internal pull-up resistor.
    // HIGH = not pressed, LOW = pressed.

    pinMode(BUZZER_PIN, OUTPUT);
    // Sets buzzer pin as an output.

    pinMode(RED_LED, OUTPUT);
    // Sets red LED pin as an output.

    pinMode(GREEN_LED, OUTPUT);
    // Sets green LED pin as an output.

    digitalWrite(RED_LED, LOW);
    // Makes sure red LED starts OFF.

    digitalWrite(GREEN_LED, LOW);
    // Makes sure green LED starts OFF.

    // schedule buttons use internal pull-ups too -- LOW means pressed,
    // connect one side of each button to its pin and the other to GND.
    pinMode(PLUS_BUTTON, INPUT_PULLUP);
    pinMode(MINUS_BUTTON, INPUT_PULLUP);
    pinMode(CONFIRM_BUTTON, INPUT_PULLUP);

    motor.setSpeed(25);

    stepOneSector();
    // The carousel is currently resting on an OPEN (pill) sector, not a
    // blocked one -- seal it immediately at boot so the drop hole isn't
    // left open, and so the rest of the code's "starts blocked" assumption
    // (see dispenseMedication()) 

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
    //rtc is now connected

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    if (TESTING_MODE)
    {
        nextDoseTime = rtc.now() + TimeSpan(0, 0, 0, (int)TEST_INTERVAL_SECONDS);
    }
    else
    {
        computeTodaysDoseSchedule(rtc.now());
        nextDoseTime = todaysDoseTimes[0];
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    //"Is OLED connected?"
    {
        Serial.println("OLED not found");
        // Prints error message

        while (1)
        {
            // Stops program if OLED is not found
        }
    }
    // OLED is connected now

    display.setTextColor(SSD1306_WHITE);
    // Without this, printed text renders in the same color as the
    // background (invisible) even though the display itself works fine.

    display.clearDisplay();
    // Clears anything currently on the OLED.

    display.display();
    // Sends the cleared screen to the actual display.

    Serial.println("Medication schedule setup");
    printPendingSetting();
}


void loop()
{
    DateTime now = rtc.now();
    // Gets the current time from RTC.

    printTime(now);
    // Prints time to Serial Monitor.

    if (!waitingForConfirmation)
    {
        showCurrentTime(now);
        // Displays time on OLED -- but not while a dose alert is on screen,
        // otherwise this overwrites "TAKE DOSE NOW" 
    }

    checkMedicationTime(now);
    // Checks if medication should be dispensed.

    checkButton();
    // Checks if user pressed "Dose Taken".

    checkScheduleButtons();
    // Checks PLUS/MINUS/CONFIRM for schedule configuration.

    delay(100);
    // Short delay so buttons respond quickly.
}


// FUNCTIONS

void printTime(DateTime now)
//prints current time
{
    Serial.print(now.hour());
    Serial.print(":");
    Serial.println(now.minute());
}

void showCurrentTime(DateTime now)
{
    display.clearDisplay();
    //clears before displaying new info
    display.setTextSize(2);
    //makes text size bigger
    display.setCursor(0, 0);
    display.print(now.hour());
    display.print(":");
    if (now.minute() < 10)
    {
        display.print("0");
        //prints 0 in tens so it's 8:05 instead of 8:5
    }
    display.print(now.minute());

    long secondsUntil = nextDoseTime.unixtime() - now.unixtime();
    if (secondsUntil < 0)
    {
        secondsUntil = 0;
    }
    int minutesUntil = secondsUntil / 60;

    display.setCursor(0, 22);
    display.print("In ");
    display.print(minutesUntil);
    display.println(" min");

    display.setCursor(0, 44);
    display.print("@ ");
    printAmPmTime(nextDoseTime.hour(), nextDoseTime.minute());

    display.display();
    //sends information to OLED
}

void printAmPmTime(int hour24, int minute)
{
    int displayHour = hour24 % 12;
    if (displayHour == 0)
    {
        displayHour = 12;
    }

    display.print(displayHour);
    display.print(":");
    if (minute < 10)
    {
        display.print("0");
    }
    display.print(minute);
    display.print(hour24 < 12 ? " AM" : " PM");
}

void takeDoseScreen()
{
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(5, 10);
    // may need to be adjusted
    display.println("TAKE");
    display.setCursor(5, 35);
    display.println("DOSE NOW");
    display.display();
    //updates OLED screen
}

void nextDoseScreen(int hour, int minute)
{
    display.clearDisplay();

    display.setTextSize(2);

    display.setCursor(0, 0);
    display.println("Taken!");
    // Confirms user pressed button.

    display.setCursor(0, 22);
    display.println("Next:");

    display.setCursor(0, 44);

    printAmPmTime(hour, minute);

    display.display();
}

void stepOneSector()
// Advances the carousel by exactly one sector (200/16 = 12.5 steps -- not a
// whole step). Uses a rounding accumulator instead of a fixed step count:
// idealSectorPosition tracks the true fractional target, and each call only
// sends however many real steps are needed to catch up to the nearest whole
// step. This keeps every sector landing within +/-0.5 step of true center,
// with no fixed bias and no accumulation over repeated calls.
{
    idealSectorPosition += (double)STEPS_PER_REVOLUTION / NUMBER_OF_SECTORS;

    long targetStep = (long)(idealSectorPosition + 0.5);
    long stepsToMove = targetStep - actualStepsCommanded;

    motor.step(stepsToMove);

    actualStepsCommanded = targetStep;
}

void dispenseMedication()
// The carousel rests with a BLOCK sector over the drop hole. Each dispense:
//   1. rotates one sector onto the next PILL sector -- pill drops through the hole
//   2. pauses briefly so the pill actually falls
//   3. rotates one more sector onto the following BLOCK sector -- reseals the hole
{
    stepOneSector();
    // rotates onto the pill sector; pill drops through the hole

    delay(500);
    // gives the pill time to actually fall before resealing

    stepOneSector();
    // rotates onto the next block sector; reseals the hole

    currentPillSlot++;
    //updates variable

    if (currentPillSlot >= NUMBER_OF_PILL_SLOTS)
    {
        currentPillSlot = 0;
        //reset back to beginning
    }
    Serial.print("Current pill slot: ");
    Serial.println(currentPillSlot);
}

void computeTodaysDoseSchedule(DateTime now)
// builds today's list of dose times from MED_HOUR:MED_MINUTE plus the
// configured hours-between-doses, and resets how many have been given
{
    todaysDoseTimes[0] = DateTime(now.year(), now.month(), now.day(), MED_HOUR, MED_MINUTE, 0);

    for (int i = 1; i < activeDosesPerDay; i++)
    {
        todaysDoseTimes[i] = todaysDoseTimes[i - 1] + TimeSpan(0, activeHoursBetween[i - 1], 0, 0);
    }

    dosesGivenToday = 0;
    lastScheduleDay = now.day();
}

void triggerDoseAlert()
// shared alert sequence: OLED, LED, buzzer, dispense, wait for confirmation
{
    takeDoseScreen();
    // OLED displays:
    // TAKE
    // DOSE NOW

    digitalWrite(RED_LED, HIGH);
    // Turns red LED ON to show medication is ready.

    digitalWrite(GREEN_LED, LOW);
    // Makes sure green LED is OFF.

    tone(BUZZER_PIN, 1000);
    // Turns buzzer ON at 1000 Hz.

    dispenseMedication();
    // Rotates carousel one compartment.

    waitingForConfirmation = true;
    // Arduino now waits for the user
    // to press the "Dose Taken" button.

    dispensedToday = true;
    // Prevents the motor from rotating again for this same dose.
}

void checkMedicationTime(DateTime now)
{
    if (TESTING_MODE)
    {
        if (now >= nextDoseTime)
        {
            triggerDoseAlert();
            nextDoseTime = now + TimeSpan(0, 0, 0, (int)TEST_INTERVAL_SECONDS);
            // repeats every TEST_INTERVAL_SECONDS, regardless of the real schedule
        }
        return;
    }

    if (lastScheduleDay == -1 || now.day() != lastScheduleDay)
    {
        computeTodaysDoseSchedule(now);
    }

    if (dosesGivenToday >= activeDosesPerDay)
    {
        return;
        // every dose scheduled for today has already been given
    }

    DateTime targetTime = todaysDoseTimes[dosesGivenToday];
    nextDoseTime = targetTime;

    if (now.hour() == targetTime.hour() &&
        now.minute() == targetTime.minute() &&
        !dispensedToday)
        // Checks if current time matches the next owed dose time.
        // Also checks that this dose has not already happened.
    {
        triggerDoseAlert();

        dosesGivenToday++;
        // this dose is done; the next loop pass targets the next one

        if (dosesGivenToday < activeDosesPerDay)
        {
            nextDoseTime = todaysDoseTimes[dosesGivenToday];
        }
    }
    else if (now.minute() != targetTime.minute())
    {
        dispensedToday = false;
        // Allows this dose to dispense once its minute comes around.
    }
}

void checkButton()
{
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        // Button is pressed.

        if (waitingForConfirmation == true)
        {

            noTone(BUZZER_PIN);
            // Turns buzzer off.

            digitalWrite(RED_LED, LOW);
            // Turns red LED off.

            digitalWrite(GREEN_LED, HIGH);
            // Turns green LED on.

            nextDoseScreen(nextDoseTime.hour(), nextDoseTime.minute());
            // Shows confirmation screen with the next dose time
            // (nextDoseTime is kept current by checkMedicationTime() either way).

            waitingForConfirmation = false;
            // User has confirmed taking medication.

            delay(4000);
            // Gives the confirmation screen time to actually be read.
        }
    }
}

// Returns true once when a button is pressed
bool buttonWasPressed(bool currentState, bool previousState)
{
    if (previousState == HIGH &&
        currentState == LOW &&
        millis() - lastButtonTime >= debounceDelay)
    {
        lastButtonTime = millis();
        return true;
    }

    return false;
}

void increaseCurrentSetting()
{
    if (currentSetting == 0)
    {
        // Increase doses per day, up to MAX_DOSES
        if (pendingDosesPerDay < MAX_DOSES)
        {
            pendingDosesPerDay++;
        }
    }
    else
    {
        int intervalIndex = currentSetting - 1;

        // Maximum interval is 24 hours
        if (pendingHoursBetween[intervalIndex] < 24)
        {
            pendingHoursBetween[intervalIndex]++;
        }
    }
}

void decreaseCurrentSetting()
{
    if (currentSetting == 0)
    {
        // At least one dose per day
        if (pendingDosesPerDay > 1)
        {
            pendingDosesPerDay--;
        }
    }
    else
    {
        int intervalIndex = currentSetting - 1;

        // Minimum interval is one hour
        if (pendingHoursBetween[intervalIndex] > 1)
        {
            pendingHoursBetween[intervalIndex]--;
        }
    }
}

void confirmCurrentSetting()
{
    // For N doses, the user needs N - 1 intervals.
    //
    // Example:
    // 1 dose  = no intervals
    // 2 doses = one interval
    // 3 doses = two intervals
    // 4 doses = three intervals

    int numberOfIntervals = pendingDosesPerDay - 1;

    if (currentSetting < numberOfIntervals)
    {
        // Move to the next interval
        currentSetting++;
        printPendingSetting();
    }
    else
    {
        // All settings have been entered, so save them
        savePendingSettings();

        // Return to editing the number of doses
        currentSetting = 0;

        Serial.println();
        Serial.println("Settings confirmed and activated.");
        printActiveSettings();
        Serial.println();
        Serial.println("Press buttons to enter new settings.");
        printPendingSetting();
    }
}

void savePendingSettings()
{
    activeDosesPerDay = pendingDosesPerDay;

    for (int i = 0; i < MAX_DOSES - 1; i++)
    {
        activeHoursBetween[i] = pendingHoursBetween[i];
    }

    lastScheduleDay = -1;
    // forces checkMedicationTime() to rebuild today's dose times right away
    // instead of waiting for the next day
}

void printPendingSetting()
{
    Serial.println();

    if (currentSetting == 0)
    {
        Serial.print("Set doses per day: ");
        Serial.println(pendingDosesPerDay);
    }
    else
    {
        int intervalIndex = currentSetting - 1;

        Serial.print("Set hours between dose ");
        Serial.print(currentSetting);
        Serial.print(" and dose ");
        Serial.print(currentSetting + 1);
        Serial.print(": ");
        Serial.print(pendingHoursBetween[intervalIndex]);
        Serial.println(" hours");
    }
}

void printActiveSettings()
{
    Serial.print("Active doses per day: ");
    Serial.println(activeDosesPerDay);

    for (int i = 0; i < activeDosesPerDay - 1; i++)
    {
        Serial.print("Hours between dose ");
        Serial.print(i + 1);
        Serial.print(" and dose ");
        Serial.print(i + 2);
        Serial.print(": ");
        Serial.println(activeHoursBetween[i]);
    }
}

void checkScheduleButtons()
{
    bool plusState = digitalRead(PLUS_BUTTON);
    bool minusState = digitalRead(MINUS_BUTTON);
    bool confirmState = digitalRead(CONFIRM_BUTTON);

    // Button 1: increase current value
    if (buttonWasPressed(plusState, previousPlusState))
    {
        increaseCurrentSetting();
        printPendingSetting();
    }

    // Button 2: decrease current value
    if (buttonWasPressed(minusState, previousMinusState))
    {
        decreaseCurrentSetting();
        printPendingSetting();
    }

    // Button 3: confirm current value
    if (buttonWasPressed(confirmState, previousConfirmState))
    {
        confirmCurrentSetting();
    }

    previousPlusState = plusState;
    previousMinusState = minusState;
    previousConfirmState = confirmState;
}
