add_executable(test_static_vector test_static_vector.cpp doctest_main.cpp)
target_link_libraries(test_static_vector PRIVATE ingot)
add_test(NAME test_static_vector COMMAND test_static_vector)

add_executable(test_string test_string.cpp doctest_main.cpp)
target_link_libraries(test_string PRIVATE ingot)
add_test(NAME test_string COMMAND test_string)

add_executable(test_view test_view.cpp doctest_main.cpp)
target_link_libraries(test_view PRIVATE ingot)
add_test(NAME test_view COMMAND test_view)
