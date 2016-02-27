/* Adapter for an XT Keyboard
 * License: 2-Clause BSD, see file LICENSE
 *
 * The clock-output of the keyboard triggers an interrupt which samples the
 * data-output.
 * When 10 bits were received a counter is incremented, so that the loop() can
 * pick up the keycode and transfer to USB.
 *
 * The keycodes of the XT keyboard will have the last bit set if the key was
 * released.
 *
 * This will only ever send one key at a time. Modifiers (Ctrl, Shift) are sent
 * seperatly. Probably gaming would not be fun.
 */

#include <usb_keyboard.h>
#include <string.h>

const int data_pin = 8;
const int clock_pin =  5;
const int led_pin =  13;

/* This counter is incremented every time a key is pressed */
volatile int ctr = 0;
/* The code of the last key */
volatile unsigned int c_data = 0;

/* The mapping of the old keuboards keycodes to the modern keycodes */
struct {unsigned int old; KEYCODE_TYPE n;} xt_kb_map[] = {{0x102, KEY_F6}, {0x104, KEY_D}, {0x108, KEY_Q}, {0x10A, KEYPAD_2}, {0x10C, KEY_B}, {0x110, KEY_7}, {0x112, KEYPAD_8}, {0x114, KEY_QUOTE}, {0x118, KEY_O}, {0x120, KEY_3}, {0x122, KEY_F10}, {0x124, KEY_J}, {0x128, KEY_T}, {0x12C, KEY_PERIOD}, {0x130, KEY_MINUS}, {0x132, KEYPAD_5}, {0x134, KEY_Z}, {0x138, KEY_ENTER}, {0x13C, KEY_F2}, {0x140, KEY_1}, {0x142, KEY_F8}, {0x144, KEY_G}, {0x148, KEY_E}, {0x14A, KEYPAD_0}, {0x14C, KEY_M}, {0x150, KEY_9}, {0x152, KEYPAD_MINUS}, {0x158, KEY_LEFT_BRACE}, {0x15C, KEY_CAPS_LOCK}, {0x160, KEY_5}, {0x162, KEY_SCROLL_LOCK}, {0x164, KEY_L}, {0x168, KEY_U}, {0x170, KEY_BACKSPACE}, {0x172, KEYPAD_PLUS}, {0x174, KEY_C}, {0x178, KEY_A}, {0x17C, KEY_F4}, {0x180, KEY_ESC}, {0x182, KEY_F7}, {0x184, KEY_F}, {0x188, KEY_W}, {0x18A, KEYPAD_3}, {0x18C, KEY_N}, {0x190, KEY_8}, {0x192, KEYPAD_9}, {0x194, KEY_BACKSLASH}, {0x198, KEY_P}, {0x19C, KEY_SPACE}, {0x1A0, KEY_4}, {0x1A2, KEY_NUM_LOCK}, {0x1A4, KEY_K}, {0x1A8, KEY_Y}, {0x1AC, KEY_SLASH}, {0x1B0, KEY_EQUAL}, {0x1B2, KEYPAD_6}, {0x1B4, KEY_X}, {0x1BC, KEY_F3}, {0x1C0, KEY_2}, {0x1C2, KEY_F9}, {0x1C4, KEY_H}, {0x1C8, KEY_R}, {0x1CA, KEYPAD_PERIOD}, {0x1CC, KEY_COMMA}, {0x1D0, KEY_0}, {0x1D2, KEYPAD_4}, {0x1D4, 0}, {0x1D8, KEY_RIGHT_BRACE}, {0x1DC, KEY_F1}, {0x1E0, KEY_6}, {0x1E2, KEYPAD_7}, {0x1E4, KEY_SEMICOLON}, {0x1E8, KEY_I}, {0x1EC, KEY_PRINTSCREEN}, {0x1F0, KEY_TAB}, {0x1F2, KEYPAD_1}, {0x1F4, KEY_V}, {0x1F8, KEY_S}, {0x1FC, KEY_F5}};
/* The mapping of the old keuboards keycodes for the modifier keys to the modern
 * keycodes
 */
struct {unsigned int old; KEYCODE_TYPE n;} xt_kb_modmap[] = {{0x11C, MODIFIERKEY_LEFT_ALT}, {0x16C, MODIFIERKEY_RIGHT_SHIFT}, {0x1B8, MODIFIERKEY_LEFT_CTRL}, {0x154, MODIFIERKEY_LEFT_SHIFT}};

/* The current state of the modifiers */
KEYCODE_TYPE curr_mod = 0;

void clock_isr(void) {
  /* Sample the data pin and shift its value into the receive "buffer"
   * After 10 bits store the keycode in the global adn increment the counter.
   */
  static uint32_t r_len = 0;
  static uint16_t data = 0;
  uint8_t pin = digitalRead(data_pin);

  data <<= 1;
  data |= pin & 1;
  r_len ++;
  if (r_len == 1) digitalWrite(led_pin, HIGH);
  if (r_len == 10) {
    c_data = data;
    ctr++;
    data = 0;
    r_len = 0;
    digitalWrite(led_pin, LOW);
  }
}

void setup() {
  pinMode(clock_pin, INPUT_PULLUP);
  pinMode(data_pin, INPUT);
  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, LOW);

  /* For some reason this is needed between initializing the pin and activating
   * the interrupt
   */
  delay(500);

  attachInterrupt(clock_pin, clock_isr, FALLING);
}

void loop() {
  /* The value of the counter that was seen last */
  static int cctr = 0;
  /* Wait until the counter changes */
  while (cctr == ctr);
  /* Store the current counter value */
  cctr = ctr;
  /* Was the key released or pressed? */
  bool release = c_data & 1;

  /* Check if the key is a modifier */
  for (size_t i = 0; i < sizeof xt_kb_modmap/sizeof xt_kb_modmap[0]; i++) {
    if (xt_kb_modmap[i].old == (c_data & 0xffe)) {
      /* If it was released, remove it from the modifier list */
      if (release) curr_mod &= ~xt_kb_modmap[i].n;
      /* Else add it */
      else curr_mod |= xt_kb_modmap[i].n;
    }
  }
  /* Send the current modifier list */
  Keyboard.set_modifier(curr_mod & 0xff);
  Keyboard.send_now();

  /* Ignore release events of "normal" keys */
  if (release) return;

  c_data = c_data & 0xffe;
  uint8_t keyname = 0;

  /* Search for the keycode in the map */
  for (size_t i = 0; i < sizeof xt_kb_map/sizeof xt_kb_map[0]; i++) {
    if (xt_kb_map[i].old == c_data) {
      keyname = xt_kb_map[i].n;
      break;
    }
  }
  /* If the key was found, send a press event followed by a release event */
  if (keyname) {
    Keyboard.set_key1(keyname);
    Keyboard.send_now();
    Keyboard.set_key1(0);
    Keyboard.send_now();
  }
}
