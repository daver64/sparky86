#pragma once
#include <cstdint>
#include <sl.h>

// Maps simlib's normalised keys to the guest key codes (USB HID usage IDs). 0 = not mapped.
inline uint8_t map_key_to_guest(sl::Event::Key key)
{
    using K = sl::Event::Key;
    switch (key)
    {
#define KEY(name, code) case K::name: return code;
    KEY(letter_a, 4) KEY(letter_b, 5) KEY(letter_c, 6) KEY(letter_d, 7) KEY(letter_e, 8)
    KEY(letter_f, 9) KEY(letter_g, 10) KEY(letter_h, 11) KEY(letter_i, 12) KEY(letter_j, 13)
    KEY(letter_k, 14) KEY(letter_l, 15) KEY(letter_m, 16) KEY(letter_n, 17) KEY(letter_o, 18)
    KEY(letter_p, 19) KEY(letter_q, 20) KEY(letter_r, 21) KEY(letter_s, 22) KEY(letter_t, 23)
    KEY(letter_u, 24) KEY(letter_v, 25) KEY(letter_w, 26) KEY(letter_x, 27) KEY(letter_y, 28)
    KEY(letter_z, 29)
    KEY(digit_1, 30) KEY(digit_2, 31) KEY(digit_3, 32) KEY(digit_4, 33) KEY(digit_5, 34)
    KEY(digit_6, 35) KEY(digit_7, 36) KEY(digit_8, 37) KEY(digit_9, 38) KEY(digit_0, 39)
    KEY(return_key, 40) KEY(escape, 41) KEY(backspace, 42) KEY(tab, 43) KEY(space, 44)
    KEY(minus, 45) KEY(equals, 46) KEY(left_bracket, 47) KEY(right_bracket, 48) KEY(backslash, 49)
    KEY(semicolon, 51) KEY(quote, 52) KEY(backquote, 53) KEY(comma, 54) KEY(period, 55) KEY(slash, 56)
    KEY(caps_lock, 57)
    KEY(f1, 58) KEY(f2, 59) KEY(f3, 60) KEY(f4, 61) KEY(f5, 62) KEY(f6, 63)
    KEY(f7, 64) KEY(f8, 65) KEY(f9, 66) KEY(f10, 67) KEY(f11, 68) KEY(f12, 69)
    KEY(insert, 73) KEY(home, 74) KEY(page_up, 75) KEY(delete_key, 76) KEY(end, 77) KEY(page_down, 78)
    KEY(arrow_right, 79) KEY(arrow_left, 80) KEY(arrow_down, 81) KEY(arrow_up, 82)
    KEY(keypad_enter, 88)
    KEY(left_control, 224) KEY(left_shift, 225) KEY(left_alt, 226)
    KEY(right_control, 228) KEY(right_shift, 229) KEY(right_alt, 230)
#undef KEY
    default: return 0;
    }
}
