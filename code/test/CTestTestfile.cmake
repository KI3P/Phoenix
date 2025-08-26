# CMake generated Testfile for
# Source directory: /app/code/test
# Build directory: /app/code/test
#
# This file includes the relevant testing commands required for
# testing this directory and lists subdirectories to be tested as well.
include("/app/code/test/all_RFboard_tests[1]_include.cmake")
include("/app/code/test/all_ModeSm_tests[1]_include.cmake")
include("/app/code/test/all_UISm_tests[1]_include.cmake")
include("/app/code/test/all_Loop_tests[1]_include.cmake")
include("/app/code/test/all_SigProc_tests[1]_include.cmake")
include("/app/code/test/all_TransmitChain_tests[1]_include.cmake")
include("/app/code/test/all_FrontPanel_tests[1]_include.cmake")
subdirs("_deps/googletest-build")
