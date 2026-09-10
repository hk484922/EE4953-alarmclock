#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);

enum class State{
  SETUP
  RUNNING,
  MENU,
  ALARM,
}

State currentState = State::SETUP;

void enterState(State newState) {
  currentState = newState;
  switch (currentState) {
    case State::RUNNING:
      // Code to execute when entering RUNNING state
      break;
    case State::MENU:
      // Code to execute when entering MENU state
      break;
    case State::ALARM:
      // Code to execute when entering ALARM state
      break;
  }
}

void setup() {
  // put your setup code here, to run once:
  int result = myFunction(2, 3);
}

void loop() {
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}