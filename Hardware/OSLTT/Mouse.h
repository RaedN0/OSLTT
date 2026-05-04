#ifndef MOUSE_H
#define MOUSE_H

#include <HID.h>

class Mouse_ {
public:
  Mouse_(void);
  void begin(void);
  void end(void);
  void click(uint8_t b = 1);        // MOUSE_LEFT = 1
  void move(signed char x, signed char y, signed char wheel = 0);
  void press(uint8_t b = 1);
  void release(uint8_t b = 1);
  bool isPressed(uint8_t b = 1);

private:
  uint8_t _buttons;
  void sendReport(void);
};

extern Mouse_ Mouse;

// Button constants
#define MOUSE_LEFT    1
#define MOUSE_RIGHT   2
#define MOUSE_MIDDLE  4
#define MOUSE_ALL     7

#endif
