
// Example Scenarios:

// 1. Player holding W, then presses D
frame1_input_flags    = INPUT_FORWARD | INPUT_JUST_PRESSED;      // 0000 0001 | 0x0100
frame2_input_flags    = INPUT_FORWARD | INPUT_HELD;             // 0000 0001 | 0x0400
frame3_input_flags    = INPUT_FORWARD | INPUT_RIGHT | INPUT_JUST_PRESSED | INPUT_HELD;  
                       // W held (0x01) + D new (0x08) + D pressed (0x0100) + W held (0x0400)

// 2. Player holding W+D, then releases W (but keeps D)
previous_flags        = INPUT_FORWARD | INPUT_RIGHT | INPUT_HELD;  // Both keys held
current_input_flags   = INPUT_RIGHT | INPUT_HELD;                  // Only D held
changed_flags        = INPUT_FORWARD | INPUT_JUST_RELEASED;       // W was released
// Final message contains:
// input_flags  = 0x08 (just D)
// changed_flags = INPUT_FORWARD | INPUT_JUST_RELEASED (W release event)

// 3. Player taps A while holding W
frame1_input_flags    = INPUT_FORWARD | INPUT_HELD;              // W being held
frame2_input_flags    = INPUT_FORWARD | INPUT_LEFT | INPUT_JUST_PRESSED | INPUT_HELD;  
                       // W held + A new + A pressed + W still held
frame3_input_flags    = INPUT_FORWARD | INPUT_LEFT | INPUT_HELD; // Both held
frame4_input_flags    = INPUT_FORWARD | INPUT_HELD | (INPUT_LEFT | INPUT_JUST_RELEASED);
                       // W still held + A released

// Client-side pseudocode for handling multiple keys:
struct KeyState {
    bool is_down;
    bool was_down;
    uint16_t flags;
};

KeyState key_states[256] = {0};  // Track all keys
uint16_t current_input = 0;
uint16_t changed_flags = 0;

void onKeyDown(int key) {
    if (!key_states[key].is_down) {
        key_states[key].is_down = true;
        current_input |= getInputFlag(key);
        changed_flags |= (getInputFlag(key) | INPUT_JUST_PRESSED);
    }
}

void onKeyUp(int key) {
    if (key_states[key].is_down) {
        key_states[key].is_down = false;
        current_input &= ~getInputFlag(key);
        changed_flags |= (getInputFlag(key) | INPUT_JUST_RELEASED);
    }
}

void updateInputState() {
    uint16_t held_flags = 0;
    
    // Update held states
    for (int i = 0; i < 256; i++) {
        if (key_states[i].is_down && key_states[i].was_down) {
            held_flags |= (getInputFlag(i) | INPUT_HELD);
        }
        key_states[i].was_down = key_states[i].is_down;
    }
    
    // Send update if anything changed
    if (current_input || changed_flags || held_flags) {
        sendInputMessage(current_input, changed_flags | held_flags);
    }
    
    // Reset change flags for next frame
    changed_flags = 0;
}
