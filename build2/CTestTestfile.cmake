# CMake generated Testfile for 
# Source directory: /home/user/octaline
# Build directory: /home/user/octaline/build2
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[engine]=] "/home/user/octaline/build2/octaline_tests")
set_tests_properties([=[engine]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/user/octaline/CMakeLists.txt;26;add_test;/home/user/octaline/CMakeLists.txt;0;")
subdirs("_deps/juce-build")
