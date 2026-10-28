
  SET(ENV{PYTHONPATH} "/home/thaison/avogadro/extern/openbabel/scripts/python:/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib")
  if(WIN32)
    SET(ENV{PATH} "/home/thaison/avogadro/extern/openbabel/scripts/python:/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib;$ENV{PATH}")
  else()
    SET(ENV{LD_LIBRARY_PATH} "/home/thaison/avogadro/extern/openbabel/scripts/python:/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib:$ENV{LD_LIBRARY_PATH}")
  endif()
  if(NOT "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib/" STREQUAL "")
    SET(ENV{BABEL_LIBDIR} /home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib/)
  endif()
  SET(ENV{BABEL_DATADIR} /home/thaison/avogadro/extern/openbabel/data)
  MESSAGE("/home/thaison/avogadro/extern/openbabel/scripts/python:/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib")
  if(NOT "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib/" STREQUAL "")
    MESSAGE("BABEL_LIBDIR=/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build/lib/")
  endif()
  EXECUTE_PROCESS(
  	COMMAND /usr/bin/python3 /home/thaison/avogadro/extern/openbabel/test/testfastsearch.py 
  	#WORKING_DIRECTORY @LIBRARY_OUTPUT_PATH@
  	RESULT_VARIABLE import_res
  	OUTPUT_VARIABLE import_output
  	ERROR_VARIABLE  import_output
  )
  
  # Pass the output back to ctest
  IF(import_output)
    MESSAGE(${import_output})
  ENDIF(import_output)
  IF(import_res)
    MESSAGE(SEND_ERROR ${import_res})
  ENDIF(import_res)
