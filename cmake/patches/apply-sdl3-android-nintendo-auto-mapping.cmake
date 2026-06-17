if (NOT DEFINED SDL_SOURCE_DIR)
  message(FATAL_ERROR "SDL_SOURCE_DIR is required")
endif ()

set(_sdl_gamepad "${SDL_SOURCE_DIR}/src/joystick/SDL_gamepad.c")
file(READ "${_sdl_gamepad}" _contents)

set(_old_block [=[
    if (vendor == USB_VENDOR_NINTENDO) {
        if (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT || product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_RIGHT) {
            // FIXME: Should we have a separate hint for non-HIDAPI JoyCon handling?
            // Android doesn't report JoyCon SL/SR presses for some reason, so no horizontal triggers/vertical paddles
            if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS, false)) {
                // Vertical mode
                if (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT) {
                    SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (L),back:b4,dpdown:b12,dpleft:b13,dpright:b14,dpup:b11,leftshoulder:b9,leftstick:b7,lefttrigger:b15,leftx:a0,lefty:a1,misc1:b18,", sizeof(mapping_string));
                } else {
                    SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (R),a:b0,b:b1,guide:b5,rightshoulder:b10,rightstick:b8,righttrigger:b16,rightx:a0,righty:a1,start:b6,x:b3,y:b2,", sizeof(mapping_string));
                }
            } else {
                // Mini gamepad mode
                if (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT) {
                    SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (L),a:b13,b:b12,guide:b18,leftstick:b7,leftx:a1,lefty:a0~,start:b4,x:b11,y:b14,paddle2:b9,paddle4:b15,", sizeof(mapping_string));
                } else {
                    SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (R),a:b1,b:b2,guide:b5,leftstick:b8,leftx:a1~,lefty:a0,start:b6,x:b0,y:b3,paddle1:b10,paddle3:b16,", sizeof(mapping_string));
                }
            }
        }
    } else {
]=])

set(_new_block [=[
    if (vendor == USB_VENDOR_NINTENDO &&
        (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT || product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_RIGHT)) {
        // Android doesn't report JoyCon SL/SR presses for some reason, so no horizontal triggers/vertical paddles
        if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS, false)) {
            // Vertical mode
            if (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT) {
                SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (L),back:b4,dpdown:b12,dpleft:b13,dpright:b14,dpup:b11,leftshoulder:b9,leftstick:b7,lefttrigger:b15,leftx:a0,lefty:a1,misc1:b18,", sizeof(mapping_string));
            } else {
                SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (R),a:b0,b:b1,guide:b5,rightshoulder:b10,rightstick:b8,righttrigger:b16,rightx:a0,righty:a1,start:b6,x:b3,y:b2,", sizeof(mapping_string));
            }
        } else {
            // Mini gamepad mode
            if (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT) {
                SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (L),a:b13,b:b12,guide:b18,leftstick:b7,leftx:a1,lefty:a0~,start:b4,x:b11,y:b14,paddle2:b9,paddle4:b15,", sizeof(mapping_string));
            } else {
                SDL_strlcat(mapping_string, "Nintendo Switch Joy-Con (R),a:b1,b:b2,guide:b5,leftstick:b8,leftx:a1~,lefty:a0,start:b6,x:b0,y:b3,paddle1:b10,paddle3:b16,", sizeof(mapping_string));
            }
        }
    } else {
]=])

set(_new_intro [=[
    if (vendor == USB_VENDOR_NINTENDO &&
        (product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_LEFT || product == USB_PRODUCT_NINTENDO_SWITCH_JOYCON_RIGHT)) {
]=])

string(FIND "${_contents}" "${_new_intro}" _already_patched)
if (_already_patched GREATER_EQUAL 0)
  message(STATUS "SDL3 Android Nintendo auto-mapping patch already applied")
  return()
endif ()

string(FIND "${_contents}" "${_old_block}" _old_pos)
if (_old_pos LESS 0)
  message(FATAL_ERROR "Failed to apply SDL3 Android Nintendo auto-mapping patch")
endif ()

string(REPLACE "${_old_block}" "${_new_block}" _contents "${_contents}")
file(WRITE "${_sdl_gamepad}" "${_contents}")
message(STATUS "Applied SDL3 Android Nintendo auto-mapping patch")
