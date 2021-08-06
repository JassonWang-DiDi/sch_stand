CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
project(sch_stand)

add_executable(rt_tst rt_tst.c)
target_link_libraries(rt_tst pthread)

add_executable(sch_stand sch_stand.c)
target_link_libraries(sch_stand pthread)

add_executable(sch_unix_server sch_unix_server.c)
target_link_libraries(sch_unix_server pthread)

add_executable(sch_unix_client sch_unix_client.c)
target_link_libraries(sch_unix_client pthread)

add_executable(com_tst com_tst.c)
target_link_libraries(com_tst pthread)
