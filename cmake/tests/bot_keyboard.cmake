#
# Bot movement keys must be ones a keyboard can send
#

add_executable(test_bot_keyboard
    ${SOURCE_DIR}/fgame/tests/test_bot_keyboard.cpp
)

add_test(NAME test_bot_keyboard COMMAND test_bot_keyboard)
set_tests_properties(test_bot_keyboard PROPERTIES TIMEOUT 15)
