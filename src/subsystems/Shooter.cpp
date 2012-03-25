#include "subsystems/Shooter.h"

#include <cmath>
#include "utils.hpp"

#include "util/PidTuner.h"

Shooter::Shooter(Victor* conveyorMotor, Victor* leftShooterMotor, Victor* rightShooterMotor,
                Encoder* shooterEncoder, Solenoid* hoodSolenoid, AnalogChannel* ballSensor,
                AnalogChannel* poofMeter, AnalogChannel* ballRanger) {
  constants_ = Constants::GetInstance();
  conveyorMotor_ = conveyorMotor;
  leftShooterMotor_ = leftShooterMotor;
  rightShooterMotor_ = rightShooterMotor;
  shooterEncoder_ = shooterEncoder;
  hoodSolenoid_ = hoodSolenoid;
  prevEncoderPos_ = shooterEncoder->Get();
  ballSensor_ = ballSensor;
  targetVelocity_ = 0.0;
  velocity_ = 0.0;
  timer_ = new Timer();
  timer_->Reset();
  timer_->Start();
  pid_ = new Pid(&constants_->shooterKP, &constants_->shooterKI, &constants_->shooterKD);
  for (int i = 0; i < FILTER_SIZE; i++) {
    velocityFilter_[i] = 0;
  }
  filterIndex_ = 0;
  outputValue_ = 0;
  for (int i = 0; i < OUTPUT_FILTER_SIZE; i++) {
    outputFilter_[i] = 0;
  }
  outputFilterIndex_ = 0;
  poofMeter_ = poofMeter;
  poofCorrectionFactor_ = 1.0;
  prevBallSensor_ = false;
  ballRanger_ = ballRanger;
  filter_ = DaisyFilter::SinglePoleIIRFilter(0.5f);
  pidGoal_ = 0.0;
  
  m_y = init_matrix(1,1);
    m_r = init_matrix(2,1);
    flash_matrix(m_y, 0.0);
    flash_matrix(m_r, 0.0, 0.0);
    ssc_.reset();
    
}

void Shooter::SetLinearPower(double pwm) {
  SetPower(Linearize(pwm));
}

void Shooter::SetTargetVelocity(double velocity) {
  targetVelocity_ = velocity;
  pid_->ResetError();
  outputValue_ = 0;
  if (velocity > 44) {
    SetHoodUp(true);
  } else if (velocity > 0) {
    SetHoodUp(false);
  }

}

bool Shooter::PIDUpdate() {
	  //int currEncoderPos = shooterEncoder_->Get();
  double currEncoderPos = shooterEncoder_->GetRaw() / 128.0 * 2 * 3.1415926;
  double velocity_goal = 2 * 3.1415926 * targetVelocity_;
  struct matrix* outputs;
  outputs = init_matrix(num_outputs, 1);
  flash_matrix(m_y, (double)currEncoderPos);
  const double velocity_weight_scalar = 0.35;
  //const double max_reference = (U_max[0] - velocity_weight_scalar * (velocity_goal - X_hat[1]) * K[1]) / K[0] + X_hat[0];
  //const double min_reference = (U_min[0] - velocity_weight_scalar * (velocity_goal - X_hat[1]) * K[1]) / K[0] + X_hat[0];
  double u_min = ssc_.U_min->data[0];
  double u_max = ssc_.U_max->data[0];
  double x_hat1 = ssc_.X_hat->data[1];
  double k1 = ssc_.K->data[1];
  double k0 = ssc_.K->data[0];
  double x_hat0 = ssc_.X_hat->data[0];
  const double max_reference = (u_max - velocity_weight_scalar * (velocity_goal - x_hat1) * k1) / k0 + x_hat0;
  const double min_reference = (u_min - velocity_weight_scalar * (velocity_goal - x_hat1) * k1) / k0 + x_hat0;
  //pidGoal_ = max(min(pidGoal_, max_reference), min_reference);
  double minimum = pidGoal_ < max_reference ? pidGoal_ : max_reference;
  pidGoal_ = minimum > min_reference ? minimum : min_reference;
  flash_matrix(m_r, pidGoal_, velocity_goal);
  ssc_.update(outputs, m_r, m_y);
  //printf("r: %f %f\n", m_r->data[0], m_r->data[1]);
  //printf("y: %f\n", m_y->data[0]);
  //printf("u: %f\n", ssc_.U->data[0]);
  if (velocity_goal < 1.0) {
	  //printf("minning out\n");
	  SetLinearPower(0.0);
	  pidGoal_ = currEncoderPos;
  } else {
	  printf("Power: %f\n", ssc_.U->data[0] / 12.0);
	  SetLinearPower(ssc_.U->data[0] / 12.0);
  }
  PidTuner::PushData(x_hat0, x_hat1, x_hat1);
  double dt = timer_->Get();
    double instantVelocity = (float)(shooterEncoder_->GetRaw() - prevEncoderPos_) / TICKS_PER_REV / 4 / dt * 2 * 3.1415926;
    //velocity_ = UpdateFilter(instantVelocity);
    prevEncoderPos_ = shooterEncoder_->GetRaw();
  DriverStationLCD* lcd_ = DriverStationLCD::GetInstance();
  lcd_->PrintfLine(DriverStationLCD::kUser_Line4, "x0: %f", x_hat0);
  lcd_->PrintfLine(DriverStationLCD::kUser_Line5, "x1: %f", x_hat1);
  lcd_->PrintfLine(DriverStationLCD::kUser_Line6, "v: %f", instantVelocity);
  lcd_->UpdateLCD();
  
  return false;
  /*
  double increment = targetVelocity_ * dt * TICKS_PER_REV;
  timer_->Reset();
   pidGoal_ += increment;
   double error = pidGoal_ - currEncoderPos;
   double gain = constants_->shooterKP;
   if (error*gain > 1.0) {
	   pidGoal_ = currEncoderPos + 1.0/gain;
   } else if(error*gain < -1.0) {
	   pidGoal_ = currEncoderPos - 1.0/gain;
   }
   error = pidGoal_ - currEncoderPos;
   SetLinearPower(1.0);
   //PidTuner::PushData(targetVelocity_, velocity_, error * gain);
   PidTuner::PushData(pidGoal_, (double)currEncoderPos, (double)currEncoderPos);
   static int i = 0;
   if(i++ % 100) {
	   printf("pidGoal_: %f currEncoderPos: %d\n", pidGoal_, currEncoderPos);
   }*/
   return false;
  
  /*
  double instantVelocity = (float)(currEncoderPos - prevEncoderPos_) / TICKS_PER_REV / timer_->Get();
  velocity_ = UpdateFilter(instantVelocity);
  prevEncoderPos_ = currEncoderPos;

  double correctedTargetVelocity_ = targetVelocity_ * poofCorrectionFactor_;
  outputValue_ += pid_->Update(correctedTargetVelocity_, velocity_);

  timer_->Reset();
  double correctedOutputValue = targetVelocity_ * (1.0/72.0);
  correctedOutputValue += outputValue_;
  double filteredOutput = UpdateOutputFilter(correctedOutputValue);
  SetLinearPower(filteredOutput);
  //double t = GetTime();

  atTarget_ = fabs(correctedTargetVelocity_ - velocity_) < VELOCITY_THRESHOLD;
  static int op = 0;
  if (++op % 10 == 0)
    printf("target: %f vel: %f \n",correctedTargetVelocity_,velocity_);
  //PidTuner::PushData(correctedTargetVelocity_, velocity_, 0.0);
  return atTarget_ == true;
  */
}

void Shooter::SetLinearConveyorPower(double pwm) {
  conveyorMotor_->Set(pwm);
}

bool Shooter::AtTargetVelocity() {
	return atTarget_;
}

void Shooter::SetHoodUp(bool up) {
  hoodSolenoid_->Set(up);
}

double Shooter::UpdateFilter(double value) {
  return filter_->Calculate(value); 
}

double Shooter::UpdateOutputFilter(double value) {
  outputFilter_[outputFilterIndex_] = value;
  outputFilterIndex_++;
  if (outputFilterIndex_ == OUTPUT_FILTER_SIZE) {
    outputFilterIndex_ = 0;
  }
  double sum = 0;
  for (int i = 0; i < OUTPUT_FILTER_SIZE; i++) {
    sum += outputFilter_[i];
  }
  return sum / (double)OUTPUT_FILTER_SIZE;
}

double Shooter::GetVelocity() {
  return velocity_;
}

void Shooter::SetPower(double power) {
  // The shooter should only ever spin in one direction.
  if (power < 0 || targetVelocity_ == 0) {
    power = 0;
  }
  leftShooterMotor_->Set(PwmLimit(-power));
  rightShooterMotor_->Set(PwmLimit(-power));
}

double Shooter::Linearize(double x) {
  if (x >= 0.0) {
    return constants_->shooterCoeffA * pow(x, 4) + constants_->shooterCoeffB * pow(x, 3) +
        constants_->shooterCoeffC * pow(x, 2) + constants_->shooterCoeffD * x;
  } else {
    // Rotate the linearization function by 180.0 degrees to handle negative input.
    return -Linearize(-x);
  }
}

double Shooter::ConveyorLinearize(double x) {
  if (fabs(x) < 0.01 ) {
    return 0;
  } else if (x > 0) {
    return constants_->conveyorCoeffA * pow(x, 3) + constants_->conveyorCoeffB * pow(x, 2) +
        constants_->conveyorCoeffC * x + constants_->conveyorCoeffD;
  } else {
    // Rotate the linearization function by 180.0 degrees to handle negative input.
    return -Linearize(-x);
  }
}
