#include <CurieIMU.h>
#include <MadgwickAHRS.h>
#include <MedianFilter.h>
#include <PID_v1.h>
#include <Servo.h>

#define SAMPLE_FREQUENCY 100

#define IMU_CALIB_PITCH  4.75
#define IMU_CALIB_ROLL   -3.75
#define IMU_CALIB_YAW    180.0

#define MOTOR_POWER_OFF  900

#define THROTTLE_CUTOFF  1100

#define PITCH_MIN        1000
#define ROLL_MIN         1015
#define YAW_MIN          1000
#define PITCH_MID        1475
#define ROLL_MID         1475
#define YAW_MID          1475
#define PITCH_MAX        1920
#define ROLL_MAX         1935
#define YAW_MAX          1910

#define PIN_MOTOR1       4
#define PIN_MOTOR2       5
#define PIN_MOTOR3       6
#define PIN_MOTOR4       7
#define PIN_KNOB         8
#define PIN_SWITCH       9
#define PIN_YAW          10
#define PIN_PITCH        11
#define PIN_ROLL         12
#define PIN_THROTTLE     13

// Clock vars
unsigned long microsLastImuRead, microsPerImuRead;
unsigned long microsLastRcFilter, microsPerRcFilter;
unsigned long microsLastYawUpdate, microsPerYawUpdate;

// IMU vars
Madgwick imuFilter;

// Motor vars
Servo motor1, motor2, motor3, motor4;

// PID vars
double pidTuneP = 1.0;
double pidTuneI = 0.0;
double pidTuneD = 0.0;
double pidPitchSetpoint, pidPitchInput, pidPitchOutput;
double pidRollSetpoint, pidRollInput, pidRollOutput;
double pidYawSetpoint, pidYawInput, pidYawOutput;
PID pidPitch(&pidPitchInput, &pidPitchOutput, &pidPitchSetpoint, 1.0, 0.75, 0.15, REVERSE);
PID pidRoll(&pidRollInput, &pidRollOutput, &pidRollSetpoint, 1.0, 0.75, 0.15, REVERSE);
PID pidYaw(&pidYawInput, &pidYawOutput, &pidYawSetpoint, 1.0, 0.0, 0.1, REVERSE);

// RC vars
MedianFilter rcThrottleFilter(8, 0);
MedianFilter rcPitchFilter(8, 0);
MedianFilter rcRollFilter(8, 0);
MedianFilter rcYawFilter(8, 0);
MedianFilter rcKnobFilter(8, 0);
MedianFilter rcSwitchFilter(8, 0);
volatile unsigned long rcThrottleRiseTime;
volatile unsigned long rcThrottleValue;
volatile unsigned long rcPitchRiseTime;
volatile unsigned long rcPitchValue;
volatile unsigned long rcRollRiseTime;
volatile unsigned long rcRollValue;
volatile unsigned long rcYawRiseTime;
volatile unsigned long rcYawValue;
volatile unsigned long rcKnobRiseTime;
volatile unsigned long rcKnobValue;
volatile unsigned long rcSwitchRiseTime;
volatile unsigned long rcSwitchValue;

void setup() {
//  Serial.begin(9600);

  // init clocks
  microsLastImuRead = micros();
  microsPerImuRead = 1000000 / SAMPLE_FREQUENCY;
  microsLastRcFilter = micros();
  microsPerRcFilter = 1000000 / SAMPLE_FREQUENCY;
  microsLastYawUpdate = micros();
  microsPerYawUpdate = 1000000 / 10;

  // init IMU
  CurieIMU.begin();
  CurieIMU.setGyroRate(SAMPLE_FREQUENCY);
  CurieIMU.setAccelerometerRate(SAMPLE_FREQUENCY);
  imuFilter.begin(SAMPLE_FREQUENCY);
  // set the accelerometer range to 2G
  CurieIMU.setAccelerometerRange(2);
  // set the gyroscope range to 250 degrees/second
  CurieIMU.setGyroRange(250);

  // init motors
  motor1.attach(PIN_MOTOR1);
  motor2.attach(PIN_MOTOR2);
  motor3.attach(PIN_MOTOR3);
  motor4.attach(PIN_MOTOR4);
  writeMotorsOff();

  // init PIDs
  pidPitchSetpoint = IMU_CALIB_PITCH;
  pidRollSetpoint = IMU_CALIB_ROLL;
  pidYawSetpoint = IMU_CALIB_YAW;
  pidPitchInput = 0;
  pidRollInput = 0;
  pidYawInput = 0;
  pidPitch.SetOutputLimits(-200.0, 200.0);
  pidRoll.SetOutputLimits(-200.0, 200.0);
  pidYaw.SetOutputLimits(-100.0, 100.0);
  pidPitch.SetSampleTime(1000 / SAMPLE_FREQUENCY);
  pidRoll.SetSampleTime(1000 / SAMPLE_FREQUENCY);
  pidYaw.SetSampleTime(1000 / SAMPLE_FREQUENCY);
  pidPitch.SetMode(AUTOMATIC);
  pidRoll.SetMode(AUTOMATIC);
  pidYaw.SetMode(AUTOMATIC);

  // init RC
  rcThrottleValue = 0;
  attachInterrupt(PIN_THROTTLE, rcThrottleRising, RISING);
  attachInterrupt(PIN_PITCH, rcPitchRising, RISING);
  attachInterrupt(PIN_ROLL, rcRollRising, RISING);
  attachInterrupt(PIN_YAW, rcYawRising, RISING);
  attachInterrupt(PIN_KNOB, rcKnobRising, RISING);
  attachInterrupt(PIN_SWITCH, rcSwitchRising, RISING);

  delay(1000);
}

void loop() {
  filterRC();
  readIMU();
  updatePIDs();
  writeMotors();
}

void filterRC() {
  if (micros() - microsLastRcFilter >= microsPerRcFilter) {
    rcThrottleFilter.in(rcThrottleValue);
    rcPitchFilter.in(rcPitchValue);
    rcRollFilter.in(rcRollValue);
    rcYawFilter.in(rcYawValue);
    rcKnobFilter.in(rcKnobValue);
    rcSwitchFilter.in(rcSwitchValue);
    microsLastRcFilter = micros();
  }

//  Serial.print(rcThrottleFilter.out());
//  Serial.print(" ");
//  Serial.print(rcPitchFilter.out());
//  Serial.print(" ");
//  Serial.print(rcRollFilter.out());
//  Serial.print(" ");
//  Serial.println(rcYawFilter.out());
}

void readIMU() {
  int aix, aiy, aiz;
  int gix, giy, giz;
  float ax, ay, az;
  float gx, gy, gz;

  if (micros() - microsLastImuRead >= microsPerImuRead) {
    CurieIMU.readMotionSensor(aix, aiy, aiz, gix, giy, giz);
    ax = convertRawAcceleration(aix);
    ay = convertRawAcceleration(aiy);
    az = convertRawAcceleration(aiz);
    gx = convertRawGyro(gix);
    gy = convertRawGyro(giy);
    gz = convertRawGyro(giz);
    imuFilter.updateIMU(gx, gy, gz, ax, ay, az);
    microsLastImuRead = microsLastImuRead + microsPerImuRead;
  }
}

void updatePIDs() {
  int rcThrottle = rcThrottleFilter.out();
  int rcPitch = rcPitchFilter.out();
  int rcRoll = rcRollFilter.out();
  int rcYaw = rcYawFilter.out();
  int rcKnob = rcKnobFilter.out();
  int rcSwitch = rcSwitchFilter.out();

  if (rcSwitch > 1000 && rcSwitch < 1100) {
    pidTuneP = map(rcKnob, 1000, 2000, 0, 200) / 100.0;
  } else if (rcSwitch > 1450 && rcSwitch < 1550) {
    pidTuneI = map(rcKnob, 1000, 2000, 0, 100) / 100.0;
  } else if (rcSwitch > 1900 && rcSwitch < 2000) {
    pidTuneD = map(rcKnob, 1000, 2000, 0, 200) / 1000.0;
  }

//  pidPitch.SetTunings(pidTuneP, pidTuneI, pidTuneD);
//  pidRoll.SetTunings(pidTuneP, pidTuneI, pidTuneD);
//  pidYaw.SetTunings(pidTuneP, pidTuneI, pidTuneD);

  float pitch = imuFilter.getPitch();
  float roll = imuFilter.getRoll();
  float yaw = imuFilter.getYaw();
  pidPitchInput = pitch;
  pidRollInput = roll;
  pidYawInput = 360 - yaw;

  pidPitchSetpoint = IMU_CALIB_PITCH;
  if (rcPitch < PITCH_MID - 40 || rcPitch > PITCH_MID + 40) {
    pidPitchSetpoint += map(rcPitch, PITCH_MIN, PITCH_MAX, -30, 30);
  }

  pidRollSetpoint = IMU_CALIB_ROLL;
  if (rcRoll < ROLL_MID - 40 || rcRoll > ROLL_MID + 40) {
    pidRollSetpoint += map(rcRoll, ROLL_MIN, ROLL_MAX, -30, 30);
  }

  if (rcThrottle <= THROTTLE_CUTOFF) {
    pidYawSetpoint = pidYawInput;
  } else if (micros() - microsLastYawUpdate >= microsPerYawUpdate) {
    if (rcYaw < YAW_MID - 40 || rcYaw > YAW_MID + 40) {
      pidYawSetpoint += map(rcYaw, YAW_MIN, YAW_MAX, -10, 10);
    }
    if (pidYawSetpoint < 0) {
      pidYawSetpoint += 360;
    }
    if (pidYawSetpoint >= 360) {
      pidYawSetpoint -= 360;
    }
    microsLastYawUpdate = micros();
  }

  // adjust yaw input such that error is within [-180,180]
  double pidYawError = pidYawSetpoint - pidYawInput;
  if (pidYawError > 180) {
    pidYawInput += 360;
  }
  if (pidYawError < -180) {
    pidYawInput -= 360;
  }

  pidPitch.Compute();
  pidRoll.Compute();
  pidYaw.Compute();

//  Serial.print(pidPitchInput);
//  Serial.print(" ");
//  Serial.print(pidRollInput);
//  Serial.print(" ");
//  Serial.println(pidYawInput);

//  Serial.print(pidPitchSetpoint);
//  Serial.print(" ");
//  Serial.print(pidRollSetpoint);
//  Serial.print(" ");
//  Serial.println(pidYawSetpoint);

//  Serial.print(pidPitchOutput);
//  Serial.print(" ");
//  Serial.print(pidRollOutput);
//  Serial.print(" ");
//  Serial.println(pidYawOutput);
}

void writeMotors() {
  int rcThrottle = rcThrottleFilter.out();

  if (rcThrottle > THROTTLE_CUTOFF) {
    int m1Power = rcThrottle + pidPitchOutput + pidRollOutput - pidYawOutput; // front right
    int m2Power = rcThrottle + pidPitchOutput - pidRollOutput + pidYawOutput; // front left
    int m3Power = rcThrottle - pidPitchOutput - pidRollOutput - pidYawOutput; // back left
    int m4Power = rcThrottle - pidPitchOutput + pidRollOutput + pidYawOutput; // back right
    motor1.writeMicroseconds(m1Power);
    motor2.writeMicroseconds(m2Power);
    motor3.writeMicroseconds(m3Power);
    motor4.writeMicroseconds(m4Power);
  } else {
    writeMotorsOff();
  }
}

void writeMotorsOff() {
  motor1.writeMicroseconds(MOTOR_POWER_OFF);
  motor2.writeMicroseconds(MOTOR_POWER_OFF);
  motor3.writeMicroseconds(MOTOR_POWER_OFF);
  motor4.writeMicroseconds(MOTOR_POWER_OFF);
}

void rcThrottleRising() {
  rcThrottleRiseTime = micros();
  attachInterrupt(PIN_THROTTLE, rcThrottleFalling, FALLING);
}

void rcThrottleFalling() {
  rcThrottleValue = micros() - rcThrottleRiseTime;
  attachInterrupt(PIN_THROTTLE, rcThrottleRising, RISING);
}

void rcPitchRising() {
  rcPitchRiseTime = micros();
  attachInterrupt(PIN_PITCH, rcPitchFalling, FALLING);
}

void rcPitchFalling() {
  rcPitchValue = micros() - rcPitchRiseTime;
  attachInterrupt(PIN_PITCH, rcPitchRising, RISING);
}

void rcRollRising() {
  rcRollRiseTime = micros();
  attachInterrupt(PIN_ROLL, rcRollFalling, FALLING);
}

void rcRollFalling() {
  rcRollValue = micros() - rcRollRiseTime;
  attachInterrupt(PIN_ROLL, rcRollRising, RISING);
}

void rcYawRising() {
  rcYawRiseTime = micros();
  attachInterrupt(PIN_YAW, rcYawFalling, FALLING);
}

void rcYawFalling() {
  rcYawValue = micros() - rcYawRiseTime;
  attachInterrupt(PIN_YAW, rcYawRising, RISING);
}

void rcKnobRising() {
  rcKnobRiseTime = micros();
  attachInterrupt(PIN_KNOB, rcKnobFalling, FALLING);
}

void rcKnobFalling() {
  rcKnobValue = micros() - rcKnobRiseTime;
  attachInterrupt(PIN_KNOB, rcKnobRising, RISING);
}

void rcSwitchRising() {
  rcSwitchRiseTime = micros();
  attachInterrupt(PIN_SWITCH, rcSwitchFalling, FALLING);
}

void rcSwitchFalling() {
  rcSwitchValue = micros() - rcSwitchRiseTime;
  attachInterrupt(PIN_SWITCH, rcSwitchRising, RISING);
}

float convertRawAcceleration(int aRaw) {
  // since we are using 2G range
  // -2g maps to a raw value of -32768
  // +2g maps to a raw value of 32767

  float a = (aRaw * 2.0) / 32768.0;
  return a;
}

float convertRawGyro(int gRaw) {
  // since we are using 250 degrees/seconds range
  // -250 maps to a raw value of -32768
  // +250 maps to a raw value of 32767

  float g = (gRaw * 250.0) / 32768.0;
  return g;
}
