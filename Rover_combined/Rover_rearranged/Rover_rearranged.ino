
#include <Wire.h>
#include <INA219_WE.h>
#include "SPI.h"


INA219_WE ina219; // this is the instantiation of the library for the current sensor

// these pins may be different on different boards
// this is for the uno
#define PIN_SS        10
#define PIN_MISO      12
#define PIN_MOSI      11
#define PIN_SCK       13

#define PIN_MOUSECAM_RESET     8
#define PIN_MOUSECAM_CS        7

#define ADNS3080_PIXELS_X                 30
#define ADNS3080_PIXELS_Y                 30


#define ADNS3080_PRODUCT_ID            0x00
#define ADNS3080_REVISION_ID           0x01
#define ADNS3080_MOTION                0x02
#define ADNS3080_DELTA_X               0x03
#define ADNS3080_DELTA_Y               0x04
#define ADNS3080_SQUAL                 0x05
#define ADNS3080_PIXEL_SUM             0x06
#define ADNS3080_MAXIMUM_PIXEL         0x07
#define ADNS3080_CONFIGURATION_BITS    0x0a
#define ADNS3080_EXTENDED_CONFIG       0x0b
#define ADNS3080_DATA_OUT_LOWER        0x0c
#define ADNS3080_DATA_OUT_UPPER        0x0d
#define ADNS3080_SHUTTER_LOWER         0x0e
#define ADNS3080_SHUTTER_UPPER         0x0f
#define ADNS3080_FRAME_PERIOD_LOWER    0x10
#define ADNS3080_FRAME_PERIOD_UPPER    0x11
#define ADNS3080_MOTION_CLEAR          0x12
#define ADNS3080_FRAME_CAPTURE         0x13
#define ADNS3080_SROM_ENABLE           0x14
#define ADNS3080_FRAME_PERIOD_MAX_BOUND_LOWER      0x19
#define ADNS3080_FRAME_PERIOD_MAX_BOUND_UPPER      0x1a
#define ADNS3080_FRAME_PERIOD_MIN_BOUND_LOWER      0x1b
#define ADNS3080_FRAME_PERIOD_MIN_BOUND_UPPER      0x1c
#define ADNS3080_SHUTTER_MAX_BOUND_LOWER           0x1e
#define ADNS3080_SHUTTER_MAX_BOUND_UPPER           0x1e
#define ADNS3080_SROM_ID               0x1f
#define ADNS3080_OBSERVATION           0x3d
#define ADNS3080_INVERSE_PRODUCT_ID    0x3f
#define ADNS3080_PIXEL_BURST           0x40
#define ADNS3080_MOTION_BURST          0x50
#define ADNS3080_SROM_LOAD             0x60
#define ADNS3080_PRODUCT_ID_VAL        0x17

//float vref = 2.5; // value chosen by me

float open_loop, closed_loop; // Duty Cycles
float vpd,vref,vb,iL,dutyref,current_mA; // Measurement Variables // removed vref from this list
unsigned int sensorValue0,sensorValue1,sensorValue2,sensorValue3;  // ADC sample values declaration

float ev=0,cv=0,ei=0,oc=0; //internal signals
float Ts=0.0008; //1.25 kHz control frequency. It's better to design the control period as integral multiple of switching period.

float kpv=0.05024,kiv=15.78,kdv=0; // voltage pid.
float kpi=0.02512,kii=39.4,kdi=0; // current pid.

float u0v,u1v,delta_uv,e0v,e1v,e2v; // u->output; e->error; 0->this time; 1->last time; 2->last last time
float u0i,u1i,delta_ui,e0i,e1i,e2i; // Internal values for the current controller
float uv_max=4, uv_min=0; //anti-windup limitation
float ui_max=1, ui_min=0; //anti-windup limitation
float current_limit = 1.0;
boolean Boost_mode = 0;
boolean CL_mode = 0;

unsigned int loopTrigger;
unsigned int com_count=0;   // a variables to count the interrupts. Used for program debugging.

//**************************Communication variables ****************//
char received_char = 'S';
boolean new_data = false;

//************************** Rover Constants / Variables ************************//
  //Measured diameter of Rover complete rotation wrt pivot point positioned on wheel axis: 260 mm
  const float pi = 3.14159;
  float r = 140.0;
  float C = 2*pi*r;
  float arc_length;
  float increment;
  float x1 = 0;
  float y1 = 0;

//*********************** Angle variable *****************************//
  bool angle_flag = false;
  bool target_flag = false;
  float O_to_coord_measured;
  float O_to_coord;
  float angle = 0;
  float beta = 0;
  int coord_after_rotation;
  float dummy_angle=0;
  
//************************** Motor Constants **************************//
   
int DIRRstate = LOW;              //initializing direction states
int DIRLstate = HIGH;

int DIRL = 20;                    //defining left direction pin
int DIRR = 21;                    //defining right direction pin

int pwmr = 5;                     //pin to control right wheel speed using pwm
int pwml = 9;                     //pin to control left wheel speed using pwm

//********************** Camera sensor variables ********************//
// NOTE: int is 2 bytes on this thing
int total_x = 0;
int total_y = 0;
int total_x1 = 0;
int total_y1 = 0;
int x=0;
int y=0;
int a=0;
int b=0;
int distance_x=0;
int distance_y=0;
volatile byte movementflag=0;
volatile int xydat[2];
int tdistance = 0;


//***************************Globals*******************************
bool halted = 0;                  // in simple coordinate mode with timed 90°
bool done = 0;
bool finished_turning=false;
unsigned long haltTime;
bool rover_stopped;
bool reached_x_position=0;
bool ydone1=0;
bool xdone=0;
int y_after_rotation;
bool stopped_rover = false;

float anglechanged = 0;
float sumchanged = 0;
float target;
float sensor;
float sum_dist = 0;
float current_angle = 0;
int current_x = 0;
int current_y = 0;


//bool angle_flag = true;

bool destination_reached; // in COORDINATE mode
//************************ Function declarations *********************//
int convTwosComp(int b);
void mousecam_reset();
int mousecam_init();
void mousecam_write_reg(int reg, int val);
int mousecam_read_reg(int reg);

struct MD
{
 byte motion;
 char dx, dy;
 byte squal;
 word shutter;
 byte max_pix;
};

void mousecam_read_motion(struct MD *p);
int mousecam_frame_capture(byte *pdata);
void sampling();
float saturation( float sat_input, float uplim, float lowlim);
void pwm_modulate(float pwm_input);
float pidv( float pid_input);
float pidi(float pid_input);

void stop_Rover();
void go_forwards(int command_forwards_des_dist, int sensor_forwards_distance); // -ve values to go back
//void go_backwards(int command_backwards_des_dist, int sensor_backwards_distance);
void turn_90left(unsigned long haltTime);
void turn_90right(); // Update if needed later
void angle_90_timedLeft();
void angle_90_timedRight(unsigned long startTime);
void compensate_x(float);
float toDegrees(float angleRadians);
void goLeft();
void goRight();
void goBacwards();
void goForwards();
//***********************Receiving data part ****************//
  void rec_one_char();
  void show_new_data();

//*************************** Setup ****************************//

void setup() {
  // Camera Pins Defining: 
  pinMode(PIN_SS,OUTPUT);
  pinMode(PIN_MISO,INPUT);
  pinMode(PIN_MOSI,OUTPUT);
  pinMode(PIN_SCK,OUTPUT);
  
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV32);
  SPI.setDataMode(SPI_MODE3);
  SPI.setBitOrder(MSBFIRST);
  
  Serial.begin(38400);
  
  // Communication with ESP32
  Serial1.begin(9600);

  if(mousecam_init()==-1)
  {
    Serial.println("Mouse cam failed to init");
    while(1);
  }

  // Motor Pins Defining: 
  pinMode(DIRR, OUTPUT);
  pinMode(DIRL, OUTPUT);
  pinMode(pwmr, OUTPUT);          //pin to control right wheel speed using pwm - pwmr is pin 5
  pinMode(pwml, OUTPUT);          //pin to control left wheel speed using pwm - pwml is pin 9
  
  digitalWrite(pwmr, HIGH);       //setting right motor speed at maximum
  digitalWrite(pwml, HIGH);       //setting left motor speed at maximum

  // Basic pin setups:
  noInterrupts(); //disable all interrupts
  pinMode(13, OUTPUT);  //Pin13 is used to time the loops of the controller
  pinMode(3, INPUT_PULLUP); //Pin3 is the input from the Buck/Boost switch
  pinMode(2, INPUT_PULLUP); // Pin 2 is the input from the CL/OL switch
  analogReference(EXTERNAL); // We are using an external analogue reference for the ADC

  //*************** TimerA0 initialization for control-loop interrupt.***********//
  
  TCA0.SINGLE.PER = 999; //
  TCA0.SINGLE.CMP1 = 999; //
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc | TCA_SINGLE_ENABLE_bm; //16 prescaler, 1M.
  TCA0.SINGLE.INTCTRL = TCA_SINGLE_CMP1_bm; 
  
  //********************** TimerB0 initialization for PWM output ****************//
  
  pinMode(6, OUTPUT);
  TCB0.CTRLA =TCB_CLKSEL_CLKDIV1_gc | TCB_ENABLE_bm; //62.5kHz
  analogWrite(6,120); 

  interrupts();  //enable interrupts.
  Wire.begin(); // We need this for the i2c comms for the current sensor
  ina219.init(); // this initiates the current sensor
  Wire.setClock(700000); // set the comms speed for i2c
}

//***************************** Loop **************************//

void loop() {
  
  // main code here runs repeatedly:

rec_one_char();
show_new_data();
//******************* Camera loop part: ********************//

#if 0
/*
    if(movementflag){
    tdistance = tdistance + convTwosComp(xydat[0]);
    Serial.println("Distance = " + String(tdistance));
    movementflag=0;
    delay(3);
    }
  */
// if enabled this section grabs frames and outputs them as ascii art  
  if(mousecam_frame_capture(frame)==0)
  {
    int i,j,k;
    for(i=0, k=0; i<ADNS3080_PIXELS_Y; i++) 
    {
      for(j=0; j<ADNS3080_PIXELS_X; j++, k++) 
      {
        Serial.print(asciiart(frame[k]));
        Serial.print(' ');
      }
      Serial.println();
    }
  }
  Serial.println();
  delay(250);
  
  #else
  
  // if enabled this section produces a bar graph of the surface quality that can be used to focus the camera
  // also drawn is the average pixel value 0-63 and the shutter speed and the motion dx,dy.
 
  int val = mousecam_read_reg(ADNS3080_PIXEL_SUM);
  MD md;
  mousecam_read_motion(&md);
  for(int i=0; i<md.squal/4; i++)
    Serial.print('*');
  Serial.print(' ');
  Serial.print((val*100)/351);
  Serial.print(' ');
  Serial.print(md.shutter); Serial.print(" (");
  Serial.print((int)md.dx); Serial.print(',');
  Serial.print((int)md.dy); Serial.println(')');

  // Serial.println(md.max_pix);
  delay(100);
  
    distance_x = md.dx; //convTwosComp(md.dx);
    distance_y = md.dy; //convTwosComp(md.dy);

total_x1 = (total_x1 + distance_x);
total_y1 = (total_y1 + distance_y);

total_x = (float)(total_x1/157.0) * 10; //Conversion from counts per inch to mm (400 counts per inch)
total_y = (float)(total_y1/157.0) * 10; //Conversion from counts per inch to mm (400 counts per inch)

//defined by me
float dx_mm = (float)(distance_x/157.0) * 10;
float dy_mm = (float)(distance_y/157.0) * 10;

Serial.print('\n');
Serial.println("dx = "+String(distance_x));
Serial.println("dy = "+String(distance_y));

Serial.println("Distance_x = " + String(total_x));
Serial.println("Distance_y = " + String(total_y));
Serial.print('\n');
Serial.println("dx (mm) = "+String(dx_mm));
Serial.println("dy (mm) = "+String(dy_mm));


  delay(100);

  #endif

//************************** Motor Loop part: ****************************//

  if(loopTrigger) { // This loop is triggered, it wont run unless there is an interrupt
    
    digitalWrite(13, HIGH);   // set pin 13. Pin13 shows the time consumed by each control cycle. It's used for debugging.
    
    // Sample all of the measurements and check which control mode we are in
    sampling();
    CL_mode = digitalRead(3); // input from the OL_CL switch
    Boost_mode = digitalRead(2); // input from the Buck_Boost switch
     
      if (!Boost_mode && CL_mode) { // Closed Loop Buck
          // The closed loop path has a voltage controller cascaded with a current controller. The voltage controller
          // creates a current demand based upon the voltage error. This demand is saturated to give current limiting.
          // The current loop then gives a duty cycle demand based upon the error between demanded current and measured
          // current
          current_limit = 3; // Buck has a higher current limit
          ev = vref - vb;  //voltage error at this time
          cv=pidv(ev);  //voltage pid
          cv=saturation(cv, current_limit, 0); //current demand saturation
          ei=cv-iL; //current error
          closed_loop=pidi(ei);  //current pid
          closed_loop=saturation(closed_loop,0.99,0.01);  //duty_cycle saturation
          pwm_modulate(closed_loop); //pwm modulation - closed_loop is a duty      
      }
    digitalWrite(13, LOW);   // reset pin13.
    loopTrigger = 0;
  }

 
  //************************** Rover Modes of Operation **************************//
  
  unsigned long now = millis();

// REMOTE CONTROLLER MODE: DIRECT INPUT FROM USER
//make a register that only changes if the received character becomes different (call it change_char)
bool haschanged = false;
if (received_char != change_char){
  change_char = received_char;
  haschanged = true; 
  }
 if (haschanged){
  if (sumchanged == 0 && anglechanged != 0){
    //add anglechanged to ur angle facing
    current_angle = anglechanged + current_angle;
    Serial.println("angle has changed to " + String(current_angle));
  }
  else if (anglechanged == 0 && sumchanged != 0 )
    //sumchanged multiplied by cos(current angle) + i sin(current angle) 
    //cos is y axis sin is x axis
    current_y = sumchanged * cos(current_angle);
    current_x = sumchanged * sin(current_angle);
    Serial.println("x and y have changed to " + String(current_x) + " " + String(current_y));
    sumchanged = 0;
    anglechanged = 0;
  // M: current_x and current_y coordinates must be provided
 
 //if(received_char == 'M'){
 
  if(received_char == 'F' && haschanged == false){
    goForwards();
    Serial.println(received_char);
    sumchanged = sumchanged + dy_mm;
    anglechanged = 0;
    //accumulate the distance}
    
  else if(received_char == 'B' && haschanged == false){
    goBackwards();
    Serial.println(received_char);
    sumchanged = sumchanged + dy_mm;      // need negative?
    anglechanged = 0;
    } 
    
  else if(received_char == 'L' && haschanged == false){
    goLeft();
    float O_to_coord_measured = sqrt(pow(dy_mm,2) + pow(dx_mm,2));
    float alpha = toDegrees(asin(O_to_coord_measured/(2*r))) * 4 ; 
    anglechanged = (anglechanged + alpha);
    Serial.println(received_char);
    sumchanged = 0;
    } 
    
  else if(received_char == 'R' && haschanged == false){
    goRight();
    float O_to_coord_measured = sqrt(pow(dy_mm,2) + pow(dx_mm,2));
    alpha = toDegrees(asin(O_to_coord_measured/(2*r))) * 4 ; 
    anglechanged = (anglechanged + alpha);
    Serial.println(received_char);
    sumchanged = 0;
    }
    
  else if(received_char == 'S'){
    //pwm_modulate(0);
    stop_Rover();
    anglechanged = 0;
    sumchanged = 0;
    Serial.println(received_char);} // stops
/*    
  else if(received_char == 'N'){      //rotate 90°
    O_to_coord_measured = sqrt(pow(dy_mm,2) + pow(dx_mm,2));
    alpha = toDegrees(asin(O_to_coord_measured/(2*r))) * 4 ; 
    alphaSummed = (alphaSummed + alpha);
    beta = 90; // given by Control
    
    if(angle_flag == true){
     stop_Rover();
     alphaSummed = 0;
     stopped_rover = true;
     Serial.println("Rover has reached the Destination YAY!");

    }else if(((-alphaSummed+beta)<=2)&& (angle_flag == false)){
     coord_after_rotation = total_y;
     dummy_angle = angle; // angle of the Rover when destination is reached

    //send dummy_angle to Control
     
     stop_Rover(); Serial.println("FINISHED ROTATING");
     angle_flag = true;}
     
    if(((-alphaSummed+beta) >= 2)&& angle_flag == false){
     Serial.println("inside the rotation loop ");
     Serial.println("total_y = " + String(total_y));

    // current_x and current_y must be provided
    
     if((target_x > current_x)){   
        Serial.println("target_x has a greater x coord. than the current x coord.");
      // pwm_modulate(0.25);
         goRight();    //rotate clockwise
    }else if((target_x < current_x)){
        Serial.println("target_x has a smaller x coord. than the current x coord.");
        //pwm_modulate(0.25);
        goLeft();   // rotate anticlockwise
    }else if ((target_x == current_x)){
      //do not rotate - no angle difference between current coordinates and destination coordinates
      Serial.println("target_x has the same x coord. than the current x coord.");
      goForwards();}
  }
  
  else{
    //pwm_modulate(0);
    stop_Rover();
    Serial.println("default stop");
    Serial.println(received_char);} // by default stop 
 }
*/

// COORDINATE MODE: REACHING SET OF COORDINATES SET BY THE USER
/*
  O_to_coord_measured = sqrt((dy_mm * dy_mm) + (dx_mm * dx_mm));

  sum_dist = sum_dist + O_to_coord_measured;
  float current_ang = (sum_dist / 968)* 360;
  Serial.print("total dist: ");
  Serial.println(sum_dist);
  Serial.println("total angle: " + String(current_ang));
  float target_angle = 180;
  //put a target angle called target_angle which can be from 0 to 360 degrees
  //a flag should be set to true when rotating assume flag is called angle_flag
  if(angle_flag && (current_ang > target_angle)){

    angle_flag = false;
    sum_dist = 0;
    //go_forwards(200, total_y);
      digitalWrite(DIRR, DIRRstate);
      digitalWrite(DIRL, DIRLstate);
    Serial.print("done rotating");
    stop_Rover();
    }    
    else if (angle_flag){
    Serial.print("Start rotation");
    DIRRstate = HIGH;
    DIRLstate = HIGH;
         digitalWrite(DIRR, DIRRstate);
         digitalWrite(DIRL, DIRLstate);
    }
*/















/*  if(received_char == 'C'){

      // 1st received int = target_x
      // 2nd received int = target_y
*/ 

  // Coordinates are provided by the ESP32 from Command
  // here they are just manually set - for now
  int target_y = 100;
  int target_x = 100;

  go_forwards(target_y, total_y);
/*
  O_to_coord = sqrt(pow(target_y,2) + pow(target_x,2));
  O_to_coord_measured = sqrt(pow(dy_mm,2) + pow(dx_mm,2));
  
  alpha = toDegrees(asin(O_to_coord_measured/(2*r)))*4 ; // angle of each distance increment measured by the sensor
  alphaSummed = (alphaSummed + alpha); // tot angle measured by sensor from 0°
  angle = alphaSummed; // angle between 2 set of coordinates
  Serial.println("dummy_angle ° = " + String(dummy_angle));
  
 // if( (dx_mm == 0 && dy_mm == 0) || (dx_mm == 0) ){
 //  alpha = 0;}
  
  //desired angle computed with trigonometry based on target coordinates:
  beta = 90 - toDegrees((acos(target_x/O_to_coord))) + (current_angle); // I need the complementary angle.
  
  Serial.print('\n');
  Serial.println("O_to_coord = " + String(O_to_coord) + "mm");
  Serial.println("O_to_coord_measured = " + String(O_to_coord_measured));
  Serial.println("measured angle in radians of single increment = " + String((asin(O_to_coord_measured/2/r))*2));
  Serial.println("alpha in ° = " + String(alpha));
  Serial.println("alphaSummed in ° = " + String(alphaSummed));
  Serial.println("current_angle in ° = " + String(current_angle));
  Serial.println("beta in ° = " + String(beta));
  Serial.println("dummy_angle ° = " + String(dummy_angle));
  
// must save the coordinates of the previous reached point
// use flag to signal the reaching of the given coordinates
// insert angle conditions

    if(angle_flag == true && target_flag == true){

     stop_Rover();
     alphaSummed = 0;
     stopped_rover = true;
     Serial.println("Rover has reached the Destination YAY!");
  
     Serial.println("IN RDL : total_y - coord_after_rotation = " + String(total_y - coord_after_rotation));
     Serial.println("IN RDL : target_y - coord_after_rotation = " + String(target_y - coord_after_rotation));

  //a margin of +/-2°
    }else if(((-alphaSummed+beta)<=2)&& (angle_flag == false)){
    
      coord_after_rotation = total_y;
      dummy_angle = angle; // angle of the Rover when destination is reached
      stop_Rover();
    
      Serial.println("FINISHED ROTATING");
    
      Serial.println("coord_after_rotation = " + String(coord_after_rotation));
      Serial.println("alphaSummed-beta " + String(alphaSummed-beta));
      Serial.println("angle_flag = " + String(angle_flag));
      Serial.println("Angle is precisely reached = " + String(angle));
      Serial.println("total_y - coord_after_rotation = " + String(total_y - coord_after_rotation));
      Serial.println("target_y - coord_after_rotation = " + String(target_y - coord_after_rotation));

      Serial.println("abs distanza = " + String(abs((total_y - coord_after_rotation) - (target_y - coord_after_rotation))));
    
    angle_flag = true;
    
    }
    if(((-alphaSummed+beta) >= 2)&& angle_flag == false && target_flag == false){
      Serial.println("inside the rotation loop ");
      Serial.println("total_y = " + String(total_y));
    
      if((target_x > current_x)){   
        Serial.println("target_x has a greater x coord. than the current x coord.");
      // pwm_modulate(0.25);
        goRight();    //rotate clockwise
       }
    
      else if((target_x < current_x)){
        Serial.println("target_x has a smaller x coord. than the current x coord.");
        //pwm_modulate(0.25);
        goLeft();   // rotate anticlockwise
       }
      else if ((target_x == current_x)){
      //do not rotate - no angle difference between current coordinates and destination coordinates
      Serial.println("target_x has the same x coord. than the current x coord.");
      goForwards();
      }
  }

    if(angle_flag == true && stopped_rover== false){
     Serial.println("coord_after_rotation in angle_flag loop = " + String(coord_after_rotation));
     Serial.println("Going forwards to the destination!");
     Serial.println("total_y - coord_after_rotation = " + String(total_y - coord_after_rotation));
     Serial.println("target_y - coord_after_rotation = " + String(target_y - coord_after_rotation));
     target = target_y - coord_after_rotation;
     sensor = total_y - coord_after_rotation;
    go_forwards(target,total_y - coord_after_rotation);

  Serial.println("abs distanza = " + String(abs((total_y - coord_after_rotation) - (target_y - coord_after_rotation))));
 }
 
 //Serial.println("sensor = " + String(sensor));
 //Serial.println("target = " + String(target));
 
    if((abs(-target + (total_y - coord_after_rotation)) <= 10) && angle_flag == true){
      target_flag = true;
      current_x = target_x;  // x coordinate value sent to Control - keeps track of Rover position
      current_y = target_y;  // y coordinate value sent to Control - keeps track of Rover position
      current_angle = dummy_angle; // angle of the Rover when destination is reached
      alphaSummed = 0;
      //// stop_Rover();
      Serial.println("Rover has obtained current_x and current_y");
    }
*/
 
/*
 * COORDINATE MODE WITH 90° timed FIXED ROTATIONS
   if((ydone1==1) && (xdone==1)){
      done = 1;
      stop_Rover();
      rover_stopped=1;
      Serial.println("ROVER STOPPED");
    }
  if(!halted){
    if(finished_turning==false){
      Serial.println("going to target y");
      go_forwards(target_y, total_y);
      if(abs(-target_y + total_y) <= 5){
      ydone1=1;
      Serial.println("ydone1="); Serial.print(ydone1);
      Serial.print("\n");}
    } 
    if(finished_turning==true && ydone1==1 && reached_x_position==0 && rover_stopped!=1){
      //Serial.println(abs(target_x + total_y));
      Serial.println("going to target x");
      go_forwards((y_after_rotation + target_x), total_y); 
      
      // The sensor perceives as the y-direction wherever the front of the rover points.
      // After the rotation has finished, to reach target_x, the y position must be taken into account 
     if(abs(-(y_after_rotation + target_x) + total_y) <= 5){
      Serial.println("Rover reached the x coordinate");
      xdone=1;
      reached_x_position=1;
      Serial.println("xdone="); Serial.print(xdone);
      }
    }
  }
  if(halted && !done)
  {
    if((abs(-(y_after_rotation + target_x) + total_y) <= 5) && ydone1==1){
      xdone=1;
      ydone1=1;
      stop_Rover();
      Serial.print("\n");
      Serial.println("Reached the coordinates while rotating");
    }else{
      Serial.println("rotating");
      // turn 90 to work on other direction
      turn_90left(haltTime);}
   }
  */
 
//} // curly bracket of the if received_char== 'C'

// EXPLORE MODE 
  // like REMOTE CONTROLLER BUT THE COMMANDS COME FROM VISION
  // receives range of ints. Rotate Rover slowly until the int=0 (probs will need a range for this)
  // if int<0 rotate right
  // if int>0 rotate left
  // when the int=0 , Rover moves forwards (for how much? bool? or fixed distance?)

/*  if(received_char == 'E'){

    //EXPLORE
    
    }
*/
   
 }

//*************************** Loop end ***********************

// Timer A CMP1 interrupt. Every 800us the program enters this interrupt. 
// This, clears the incoming interrupt flag and triggers the main loop.

ISR(TCA0_CMP1_vect){
  TCA0.SINGLE.INTFLAGS |= TCA_SINGLE_CMP1_bm; //clear interrupt flag
  loopTrigger = 1;
}

//************************** Functions ***************************************//

int convTwosComp(int b){
  //Convert from 2's complement
  if(b & 0x80){
    b = -1 * ((b ^ 0xff) + 1);
    }
  return b;
  }

void mousecam_reset(){
  digitalWrite(PIN_MOUSECAM_RESET,HIGH);
  delay(1); // reset pulse >10us
  digitalWrite(PIN_MOUSECAM_RESET,LOW);
  delay(35); // 35ms from reset to functional
}

int mousecam_init(){
  pinMode(PIN_MOUSECAM_RESET,OUTPUT);
  pinMode(PIN_MOUSECAM_CS,OUTPUT);
  
  digitalWrite(PIN_MOUSECAM_CS,HIGH);
  
  mousecam_reset();
  
  int pid = mousecam_read_reg(ADNS3080_PRODUCT_ID);
  if(pid != ADNS3080_PRODUCT_ID_VAL)
    return -1;

  // turn on sensitive mode
  mousecam_write_reg(ADNS3080_CONFIGURATION_BITS, 0x19);

  return 0;
 }

void mousecam_write_reg(int reg, int val){
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(val);
  digitalWrite(PIN_MOUSECAM_CS,HIGH);
  delayMicroseconds(50);
}

int mousecam_read_reg(int reg){
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(reg);
  delayMicroseconds(75);
  int ret = SPI.transfer(0xff);
  digitalWrite(PIN_MOUSECAM_CS,HIGH); 
  delayMicroseconds(1);
  return ret;
}

void mousecam_read_motion(struct MD *p){
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(ADNS3080_MOTION_BURST);
  delayMicroseconds(75);
  p->motion =  SPI.transfer(0xff);
  p->dx =  SPI.transfer(0xff);
  p->dy =  SPI.transfer(0xff);
  p->squal =  SPI.transfer(0xff);
  p->shutter =  SPI.transfer(0xff)<<8;
  p->shutter |=  SPI.transfer(0xff);
  p->max_pix =  SPI.transfer(0xff);
  digitalWrite(PIN_MOUSECAM_CS,HIGH); 
  delayMicroseconds(5);
}

int mousecam_frame_capture(byte *pdata){            // pdata must point to an array of size ADNS3080_PIXELS_X x ADNS3080_PIXELS_Y
  mousecam_write_reg(ADNS3080_FRAME_CAPTURE,0x83);  // you must call mousecam_reset() after this if you want to go back to normal operation
  
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(ADNS3080_PIXEL_BURST);
  delayMicroseconds(50);
  
  int pix;
  byte started = 0;
  int count;
  int timeout = 0;
  int ret = 0;
  for(count = 0; count < ADNS3080_PIXELS_X * ADNS3080_PIXELS_Y; ){
    pix = SPI.transfer(0xff);
    delayMicroseconds(10);
    if(started==0){
      if(pix&0x40)
        started = 1;
      else{
        timeout++;
        if(timeout==100){
          ret = -1;
          break;
        }
      }
    }
    if(started==1){
     pdata[count++] = (pix & 0x3f)<<2; // scale to normal greyscale byte range
     }
  }
  digitalWrite(PIN_MOUSECAM_CS,HIGH); 
  delayMicroseconds(14);
  return ret;
}

void sampling(){

  // Make the initial sampling operations for the circuit measurements
  
  sensorValue0 = analogRead(A0); //sample Vb
  sensorValue2 = analogRead(A2); //sample Vref
 // sensorValue2 = vref *(1023.0/ 4.096);  
  sensorValue3 = analogRead(A3); //sample Vpd
  current_mA = ina219.getCurrent_mA(); // sample the inductor current (via the sensor chip)
  
  /* Process the values so they are a bit more usable/readable
     The analogRead process gives a value between 0 and 1023 
     representing a voltage between 0 and the analogue reference which is 4.096V
  */
  vb = sensorValue0 * (4.096 / 1023.0); // Convert the Vb sensor reading to volts
  vref = sensorValue2 * (4.096 / 1023.0); // Convert the Vref sensor reading to volts
  // now vref is set at the top of the code
  vpd = sensorValue3 * (4.096 / 1023.0); // Convert the Vpd sensor reading to volts

 /* The inductor current is in mA from the sensor so we need to convert to amps.
    We want to treat it as an input current in the Boost, so its also inverted
    For open loop control the duty cycle reference is calculated from the sensor
    differently from the Vref, this time scaled between zero and 1.
    The boost duty cycle needs to be saturated with a 0.33 minimum to prevent high output voltages
 */ 
  if (Boost_mode == 1){
    iL = -current_mA/1000.0;
    dutyref = saturation(sensorValue2 * (1.0 / 1023.0),0.99,0.33);
  }else{
    iL = current_mA/1000.0;
    dutyref = sensorValue2 * (1.0 / 1023.0);
  } 
}

float saturation( float sat_input, float uplim, float lowlim){ // Saturation function
  if (sat_input > uplim) sat_input=uplim;
  else if (sat_input < lowlim ) sat_input=lowlim;
  else;
  return sat_input;
}

void pwm_modulate(float pwm_input){ // PWM function
  analogWrite(6,(int)(255-pwm_input*255)); 
}

// This is a PID controller for the voltage
float pidv( float pid_input){
  float e_integration;
  e0v = pid_input;
  e_integration = e0v;
 
  //anti-windup, if last-time pid output reaches the limitation, this time there won't be any intergrations.
  if(u1v >= uv_max) {
    e_integration = 0;
  } else if (u1v <= uv_min) {
    e_integration = 0;
  }

  delta_uv = kpv*(e0v-e1v) + kiv*Ts*e_integration + kdv/Ts*(e0v-2*e1v+e2v); //incremental PID programming avoids integrations.there is another PID program called positional PID.
  u0v = u1v + delta_uv;  //this time's control output

  //output limitation
  saturation(u0v,uv_max,uv_min);
  
  u1v = u0v; //update last time's control output
  e2v = e1v; //update last last time's error
  e1v = e0v; // update last time's error
  return u0v;
}

// This is a PID controller for the current
float pidi(float pid_input){
  float e_integration;
  e0i = pid_input;
  e_integration=e0i;
  
  //anti-windup
  if(u1i >= ui_max){
    e_integration = 0;
  } else if (u1i <= ui_min) {
    e_integration = 0;
  }
  
  delta_ui = kpi*(e0i-e1i) + kii*Ts*e_integration + kdi/Ts*(e0i-2*e1i+e2i); //incremental PID programming avoids integrations.
  u0i = u1i + delta_ui;  //this time's control output

  //output limitation
  saturation(u0i,ui_max,ui_min);
  
  u1i = u0i; //update last time's control output
  e2i = e1i; //update last last time's error
  e1i = e0i; // update last time's error
  return u0i;
}


void go_forwards(int command_forwards_des_dist, int sensor_forwards_distance){
  int distance_error = sensor_forwards_distance - command_forwards_des_dist;
  halted = 0;
  if(abs(distance_error) <  3){
     // stop rover
     //pwm_modulate(0);
     stop_Rover();
     halted = 1;
     haltTime = millis();
     Serial.print("\n");
     Serial.print("\n");
     Serial.print("Halting at ");
     Serial.println(haltTime);
     return;
     }
  if(distance_error <= -15){
    // goes forwards
    goForwards();
    return;
  }else if(distance_error >-15 && distance_error <0){
    pwm_modulate(0.25);
    //goes forwards
    goForwards();
    return;
  }else if(distance_error >= 15){
    // goes backwards
    goBackwards();
    return;
  }else if(distance_error >0 && distance_error <15){
    pwm_modulate(0.25);
    //goes backwards
    goBackwards();
    return;
   }  
 }

void turn_90left(unsigned long haltTime){
    unsigned long now = millis();
    angle_flag = 1;
    if(now-haltTime < 4500){
      goLeft();
    }
    else{
      Serial.print("We have stopped rotating ");
      Serial.println(now);
      Serial.println(haltTime);
      stop_Rover();
      halted = 0;
      finished_turning=true;
      y_after_rotation= total_y;
      Serial.println(reached_x_position);
    }
    return; 
}

// same as turn_90left
//IMPLEMENT
void turn_90right(unsigned long haltTime){
  unsigned long now = millis();
    if(now-haltTime < 4500){
      goRight();
    }
  else{
      Serial.print("We have stopped rotating ");
      Serial.println(now);
      Serial.println(haltTime);
      stop_Rover();
      halted = 0;
      finished_turning=true;
      y_after_rotation= total_y;
      Serial.println(reached_x_position);
    }
    return;
}

void stop_Rover(){
  digitalWrite(pwmr, LOW);
  digitalWrite(pwml, LOW);
  //pwm_modulate(0);
  Serial.println("Stop function activated");
}

void angle_90_timedRight(unsigned long startTime){
  unsigned long now = millis();
  if(now - startTime < 4500){
      goRight();
  }else{
    stop_Rover();}
    return;
 }

void angle_90_timedLeft(){
  unsigned long now = millis();
  if(now < 4500){
      goLeft();
  }else{
    stop_Rover();}
    return;
 }

float toDegrees(float angleRadians){
    return (angleRadians/pi)*180;
}


  void goForwards(){
    digitalWrite(pwmr, HIGH);       
    digitalWrite(pwml, HIGH);         
    DIRRstate = LOW;
    DIRLstate = HIGH;
    digitalWrite(DIRR, DIRRstate);
    digitalWrite(DIRL, DIRLstate);    
    }

   void goBackwards(){
    digitalWrite(pwmr, HIGH);       
    digitalWrite(pwml, HIGH);         
    DIRRstate = HIGH;
    DIRLstate = LOW;   
    digitalWrite(DIRR, DIRRstate);
    digitalWrite(DIRL, DIRLstate);
   }

  void goRight(){
    digitalWrite(pwmr, HIGH);       
    digitalWrite(pwml, HIGH);          
    DIRRstate = HIGH;
    DIRLstate = HIGH;  
    digitalWrite(DIRR, DIRRstate);
    digitalWrite(DIRL, DIRLstate); 
    }

   void goLeft(){
    digitalWrite(pwmr, HIGH);       
    digitalWrite(pwml, HIGH);          
    DIRRstate = LOW;
    DIRLstate = LOW;
    digitalWrite(DIRR, DIRRstate);
    digitalWrite(DIRL, DIRLstate);   
    }

 


//***** ESP32 related functions***********//
void rec_one_char() {
  if(Serial1.available()){
    received_char = Serial1.read();
    new_data = true;
  }
}

void show_new_data() {
  if(new_data == true) {
    Serial.print("An ");
    Serial.print((byte)received_char);
    Serial.println("has arrived");
    new_data = false;
  }
}

/*end of the program.*/
