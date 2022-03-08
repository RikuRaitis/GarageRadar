#include <Ultrasonic.h>
#include <FastLED.h>
#include <arduino-timer.h>

//FastLED defines
#define LED_PIN                       2
#define NUM_LEDS                      256
#define BRIGHTNESS                    255
#define LED_TYPE                      WS2811
#define COLOR_ORDER                   GRB
#define UPDATES_PER_SECOND            1000

//Distance defines
#define DISTANCE_AVERAGE_LENGTH       20

//Screen sixe
#define SCREEN_WIDTH                  16
#define SCREEN_HEIGHT                 16

//Business logic
#define STOP_DISTANCE                 23 //cm
#define STOP_SOON_DISTANCE            28 //cm
#define ALLOWED_DISTANCE              40 //cm
#define START_DISTANCE                200 //cm

//Animation
#define ARROW_ANIMATION_FRAME_TIME    150
#define SHUTDOWN_TIME                 (30 * (1000 / ARROW_ANIMATION_FRAME_TIME))  //Shutdown after SHUTDOWN_TIME seconds if last FILTER_LENGTH samples has been inside


// Numbers from 0, 1, 2, ... 9, [space], <
static uint32_t font5x5[] = {
  0xFCEB9F8, /* 0 */
  0x2308470, /* 1 */
  0xF05D0F8, /* 2 */
  0xF85C1F8, /* 3 */
  0x8429F20, /* 4 */
  0xFC3C1F0, /* 5 */
  0xFC3F1F8, /* 6 */
  0xF844420, /* 7 */
  0xFC7F1F8, /* 8 */
  0xFC7E1F8, /* 9 */
  0x0000000, /*   */ //10
  0xCCCF3C8, /* < */ //11
  0x001C000, /* - */ //12
  0x7577570, /* X */ //13
};


enum States {
    OFF,
    DRIVE,
    STOP_SOON,
    STOP_VERY_SOON,
    STOP,
};

//Locals
enum States state = OFF;

//Colors
//                    OFF               DRIVE             STOP_SOON           STOP_VERY_SOON      STOP
CRGB state_color[] =  {CRGB::Black,     CRGB::Green,      CRGB::Yellow,       CRGB::Yellow,       CRGB::Red};
int state_distace[] = {START_DISTANCE,  ALLOWED_DISTANCE, STOP_SOON_DISTANCE, STOP_DISTANCE,      0}; 
CRGB foreground_color = CRGB::Red;
CRGB background_color = CRGB::Black;

//"Frame buffer"
CRGB leds[NUM_LEDS];

//Flags
volatile bool one_second_passed = false; 
bool distance_updated = false; 
bool state_updated = false;
int inactivity_time = 0; //Seconds

//For average calc
int distance_average = 0;
int distance_array[DISTANCE_AVERAGE_LENGTH];
int distance_array_index = 0;

//Timer
auto timer = timer_create_default(); // create a timer with default settings

//Ultrasound
Ultrasonic ultrasonic(12, 11);

/* Deafaul Arduino setup, init leds and timer */
void setup() {
    delay( 1000 ); // power-up safety delay
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
    FastLED.setBrightness(  BRIGHTNESS );

    timer.every(ARROW_ANIMATION_FRAME_TIME, tick_update);
}

/* Deafaul Arduino loop */
void loop()
{
    //Get & filter distance
    distance_updated = calculate_average(ultrasonic.read());

    //Calculate inactivity   
    check_inactivity();

    //Check & Update state
    state_updated = update_state();
    
    //Update numbers and distance bar (if needed)
    if (distance_updated || state_updated) {
      draw_numbers();
      draw_distance_bar();
    }

    //Update arrows
    if (one_second_passed || state_updated) {
      draw_arrows();
      one_second_passed = one_second_passed ? false : true;
    }
    
    FastLED.show();
    FastLED.delay(1000 / UPDATES_PER_SECOND);
    
    timer.tick();
}

void check_inactivity() { //TBD: Fucking ugly, but works.
    if (one_second_passed) { //1 second has passed
      if (distance_updated) {
        inactivity_time = 0;
      }
      else {
        inactivity_time = inactivity_time + 1;
      }
      
      if ( inactivity_time > SHUTDOWN_TIME) {
        inactivity_time = SHUTDOWN_TIME;
      }
    }
}
  
bool update_state() {
    enum States oldState = state;

    if (inactivity_time == SHUTDOWN_TIME) {
      state = OFF;
    }
    else {
      for (int i = 0; i < 5; i++) {
        if (distance_average > state_distace[i]) {
          state = (States) i;
          break;
        }
      }
    }

    foreground_color = state_color[state];  

    if (state != oldState) return true;
    return false;
}

/* Called once per second */
bool tick_update(void *) {
  one_second_passed = true;
}

/* Calculate  average of distance, return true if value changed*/
bool calculate_average(int distance) {
  int old_distance_average = distance_average;
  
  distance_array[distance_array_index] = distance;
  distance_array_index = (distance_array_index + 1) % DISTANCE_AVERAGE_LENGTH;
  int distance_sum = 0;
  for (int i = 0 ; i < DISTANCE_AVERAGE_LENGTH; i++) {
    distance_sum += distance_array[i];
  }

  distance_average = distance_sum / DISTANCE_AVERAGE_LENGTH;
  return (distance_average != old_distance_average);
}

/*Return the digit of n-th position of num absolute value. Position starts from 0*/
int get_digit(int num, int n, int* str_length)
{
  char snum[5] = {0};
  itoa(num, snum, 10);

  for (int i = 0; i < 5; i++) {
    if (snum[i] == 0) {
      *str_length = i;
      break;
    }
  }

  if (snum[n] == '-') return 12; //Ugly hack - yes
  return snum[n]-'0';
}

void draw_numbers() {
  int distance_to_stop = distance_average - STOP_DISTANCE;
  int str_len;
  int number_x_position[3] = {11, 5, 0};

  draw_box(0, 0, SCREEN_WIDTH, 6, background_color);
  get_digit(distance_to_stop, 0, &str_len); //Just to get str_len
  for (int i = str_len ; i > 0; i--) {
    uint8_t digit = get_digit(distance_to_stop, i-1, &str_len);
    draw_digit(digit, number_x_position[str_len-i], 0);
  }
}

void draw_distance_bar() {
  int distance_bar_value = ((distance_average - STOP_DISTANCE) * SCREEN_WIDTH) / (ALLOWED_DISTANCE - STOP_DISTANCE); //Scale bar between stop and allowed distance

  draw_box(0, 14, SCREEN_WIDTH, 2, background_color);
  if (distance_bar_value > 0) { //Do not draw for negative values
    draw_box(0, 14, distance_bar_value, 2, foreground_color);
  }
}

bool draw_arrows() {
  static int step = 0;
  switch (state) {
    case OFF:
      draw_box(0, 6, 16, 7, background_color);
      break;
    case DRIVE:
      draw_arrows(0, 6, 16, 7, step/2);
      break;
    case STOP_SOON:
      draw_arrows(0, 6, 16, 7, step/2);
      break;
    case STOP_VERY_SOON:
       draw_stop(0, 6, 16, 7, step/2);
      break;
    case STOP:
      draw_stop(0, 6, 16, 7, step/2);
      break;
  }
  step = (step + (state == STOP_SOON ? 1 : 2)) % 10; //Slow down animation when STOP_SOON
}

void draw_stop(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t animation_step) {
  draw_box(x, y, width, height, foreground_color); //Clear background (inverted)
  draw_digit(13 /* X */, 6 , y+1);
}

void draw_arrows(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t animation_step) {
  draw_box(x, y, width, height, foreground_color); //Clear background (inverted)
  draw_digit(11, 0 - animation_step, y+1);
  draw_digit(11, 5 - animation_step, y+1);
  draw_digit(11, 10 - animation_step, y+1);
  draw_digit(11, 15 - animation_step, y+1);
}

void draw_digit(int number, int x_offset, int y_offset) {
    uint32_t digit = font5x5[number];

    draw_box(x_offset, y_offset, 5, 5, background_color); //Clear background 
    for (int8_t y = 0; y < 5; y++) {
      for (int8_t x = 0; x < 5 ; x++) {
        if (digit & 0x8000000) {
          set_pixel(x + x_offset, y + y_offset, foreground_color);
        }
        digit = digit << 1;
      }
    }
}

void draw_box(uint8_t x, uint8_t y, uint8_t width, uint8_t height, CRGB color) {
  for (int i = y ; i < (height+y) ;i++) {
    for (int j = x ; j < (width+x) ; j++) {
      set_pixel(j, i, color);
    }
  }
}

void set_pixel(int8_t x, int8_t y, CRGB color) {
  if ((x >= SCREEN_WIDTH) || (y >= SCREEN_HEIGHT) || (x < 0) || (y < 0)) return;
  leds[(x*SCREEN_WIDTH)+( x % 2 == 0 ? y : SCREEN_HEIGHT-1-y)] = color;
}
