#include "main.h"

#include <math.h>
#include <stdio.h>

#include "subsystems/Drive.h"
#include "subsystems/Shooter.h"
#include "subsystems/OperatorControl.h"
#include "subsystems/Pid.h"
#include "subsystems/PidCommander.h"
#include "util/Functions.h"
#include "util/Logger.h"
#include "util/PidTuner.h"
#include "vision/BackboardFinder.h"

MainRobot::MainRobot() {
  // Constants
  constants_ = Constants::GetInstance();

  // Motors
  leftDriveMotorA_ = new Victor((int)constants_->leftDrivePwmA);
  leftDriveMotorB_ = new Victor((int)constants_->leftDrivePwmB);
  rightDriveMotorA_ = new Victor((int)constants_->rightDrivePwmA);
  rightDriveMotorB_ = new Victor((int)constants_->rightDrivePwmB);
  intakeMotor_ = new Victor((int)constants_->intakePwm);
  conveyorMotor_ = new Victor((int)constants_->conveyorPwm);
  leftShooterMotor_ = new Victor((int)constants_->leftShooterPwm);
  rightShooterMotor_ = new Victor((int)constants_->rightShooterPwm);

  // Sensors
  leftEncoder_ = new Encoder((int)constants_->leftEncoderPortA, (int)constants_->leftEncoderPortB);
  leftEncoder_->Start();
  rightEncoder_ = new Encoder((int)constants_->rightEncoderPortA, (int)constants_->rightEncoderPortB);
  rightEncoder_->Start();
  shooterEncoder_ = new Encoder((int)constants_->shooterEncoderPortA, (int)constants_->shooterEncoderPortB);
  shooterEncoder_->Start();
  gyro_ = new Gyro((int)constants_->gyroPort);
  gyro_->SetSensitivity(1.0);
  double accelerometerSensitivity = 1.0;
  accelerometerX_ = new Accelerometer((int)constants_->accelerometerXPort);
  accelerometerY_ = new Accelerometer((int)constants_->accelerometerYPort);
  accelerometerZ_ = new Accelerometer((int)constants_->accelerometerZPort);
  accelerometerX_->SetSensitivity(accelerometerSensitivity);
  accelerometerY_->SetSensitivity(accelerometerSensitivity);
  accelerometerZ_->SetSensitivity(accelerometerSensitivity);
  bumpSensor_ = new DigitalInput((int)constants_->bumpSensorPort);

  // Pneumatics
  compressor_ = new Compressor((int)constants_->compressorPressureSwitchPort,(int)constants_->compressorRelayPort);
  compressor_->Start();
  shiftSolenoid_ = new Solenoid((int)constants_->shiftSolenoidPort);
//  hoodSolenoid_ = new Solenoid((int)constants_->hoodSolenoidPort);
  pizzaWheelSolenoid_ = new DoubleSolenoid((int)constants_->pizzaWheelSolenoidDownPort, (int)constants_->pizzaWheelSolenoidUpPort);
//  intakeSolenoid_ = new DoubleSolenoid((int)constants_->intakeSolenoidHighPort,(int)constants_->intakeSolenoidLowPort);

  // Subsystems
  drivebase_ = new Drive(leftDriveMotorA_, leftDriveMotorB_, rightDriveMotorA_, rightDriveMotorB_,
                         shiftSolenoid_, pizzaWheelSolenoid_, leftEncoder_,
                         rightEncoder_, gyro_, accelerometerX_, accelerometerY_,
                         accelerometerZ_);
//  shooter_ = new Shooter(intakeMotor_, conveyorMotor_, leftShooterMotor_, rightShooterMotor_,
//                         shooterEncoder_, hoodSolenoid_, intakeSolenoid_);

  // Control Board
  leftJoystick_ = new Joystick((int)constants_->leftJoystickPort);
  rightJoystick_ = new Joystick((int)constants_->rightJoystickPort);
  operatorControl_ = new OperatorControl((int)constants_->operatorControlPort);

  testPid_ = new Pid(constants_->driveKP, constants_->driveKI, constants_->driveKD);
  baseLockPid_ = new Pid(constants_->baseLockKP, constants_->baseLockKI, constants_->baseLockKD);
  testTimer_ = new Timer();
  testLogger_ = new Logger("/test.log", 2);
  oldPizzaWheelsButton_ = rightJoystick_->GetRawButton((int)constants_->pizzaSwitchPort);
  pizzaWheelsDown_ = false;

  // Watchdog
  GetWatchdog().SetExpiration(100);

  // Get a local instance of the Driver Station LCD
  lcd_ = DriverStationLCD::GetInstance();
  lcd_->PrintfLine(DriverStationLCD::kUser_Line1,"***Teleop Ready!***");
}

void MainRobot::DisabledInit() {
}

void MainRobot::AutonomousInit() {
  constants_->LoadFile();
  delete testPid_;
  testPid_ = new Pid(constants_->driveKP, constants_->driveKI, constants_->driveKD);
  drivebase_->ResetGyro();
  drivebase_->ResetEncoders();
  testPid_->ResetError();
  testTimer_->Reset();
  testTimer_->Start();
  GetWatchdog().SetEnabled(false);
}

void MainRobot::TeleopInit() {
  constants_->LoadFile();
  drivebase_->ResetGyro();
  drivebase_->ResetEncoders();
  //baseLockPosition_ = drivebase_->GetLeftEncoderDistance();
  //baseLockPid_->ResetError();

  // Reset Pizza Wheels
  oldPizzaWheelsButton_ = rightJoystick_->GetRawButton((int)constants_->pizzaSwitchPort);
  pizzaWheelsDown_= false;
  drivebase_->SetPizzaWheelDown(pizzaWheelsDown_);

  // Sometimes drive will be stuck at linearCoeffE when enabling teleop
  drivebase_->SetLinearPower(0.0,0.0);
  GetWatchdog().SetEnabled(true);
}

void MainRobot::DisabledPeriodic() {
  oldPizzaWheelsButton_ = rightJoystick_->GetRawButton((int)constants_->pizzaSwitchPort);
  pizzaWheelsDown_ = false;
  drivebase_->SetPizzaWheelDown(pizzaWheelsDown_);
  lcd_->UpdateLCD();
}

void MainRobot::AutonomousPeriodic() {
  double time = testTimer_->Get();
  double inputValue = Functions::SineWave(time, 5, 2);
  double position = drivebase_->GetLeftEncoderDistance();
  double signal = testPid_->Update(inputValue, position);
  drivebase_->SetLinearPower(signal, signal);
  testLogger_->Log("%f,%f,%f,%f\n", time, inputValue, signal, position);
  PidTuner::PushData(inputValue, position);
}

void MainRobot::TeleopPeriodic() {
  GetWatchdog().Feed();
  // Operator drive control
  bool wantHighGear = leftJoystick_->GetRawButton((int)constants_->highGearPort);
  bool quickTurning = rightJoystick_->GetRawButton((int)constants_->quickTurnPort);
  drivebase_->SetHighGear(wantHighGear);
  double leftPower, rightPower;
  double straightPower = HandleDeadband(-leftJoystick_->GetY(), 0.1);
  double turnPower = HandleDeadband(rightJoystick_->GetX(), 0.1);

  /*  if(quickTurning) {
      leftPower = turnPower;
      rightPower = -turnPower;
  } else if(straightPower) {
      leftPower = straightPower + turnPower;
      rightPower = straightPower - turnPower;
  }
  drivebase_->SetLinearPower(leftPower, rightPower); */

  lcd_->PrintfLine(DriverStationLCD::kUser_Line1, "lj:%.4f rj:%.4f", HandleDeadband(-leftJoystick_->GetY(), 0.1), HandleDeadband(rightJoystick_->GetX(), 0.1));
  lcd_->PrintfLine(DriverStationLCD::kUser_Line2, "sp:%.4f tp:%.4f", straightPower, turnPower);

  //double position = drivebase_->GetLeftEncoderDistance();

  // Pizza wheel control
  bool currPizzaWheelsButton = rightJoystick_->GetRawButton((int)constants_->pizzaSwitchPort);

  // If the switch has toggled, flip the pizza wheels
  if (currPizzaWheelsButton != oldPizzaWheelsButton_) {
    pizzaWheelsDown_ = !pizzaWheelsDown_;
  }
  // Update the button
  oldPizzaWheelsButton_ = rightJoystick_->GetRawButton((int)constants_->pizzaSwitchPort);

  // If we've run over the bump
  if (pizzaWheelsDown_ && bumpSensor_->Get()) {
    pizzaWheelsDown_ = false;
  }
  drivebase_->SetPizzaWheelDown(pizzaWheelsDown_);


  // Drive
  if (pizzaWheelsDown_)
    turnPower = 0;
  drivebase_->CheesyDrive(straightPower, turnPower, quickTurning);
  /*
  if (operatorControl_->GetBaseLockSwitch()) {
    // Activate closed-loop base lock mode.
    baseLockPosition_ += straightPower * .1;
    double signal = baseLockPid_->Update(baseLockPosition_, position);
    drivebase_->SetLinearPower(signal, signal);
  }

  // Set the position to lock the base to upon initial activation.
  if (!oldBaseLockSwitch_ && operatorControl_->GetBaseLockSwitch()) {
    baseLockPosition_ = position;
  }
  oldBaseLockSwitch_ = operatorControl_->GetBaseLockSwitch();
  */
  lcd_->UpdateLCD();
  static PidCommander* pc = new PidCommander(NULL, 100, .1, 3);
  static int i = 0;
  if ( i <= 100) {
    pc->Update(i);
  }
  i++;
}

double MainRobot::HandleDeadband(double val, double deadband) {
  return (fabs(val) > fabs(deadband)) ? val : 0.0;
}
