# - Try to find FFTW3
# Once done this will define
#
# FFTW3_FOUND - system has libfftw3
# FFTW3_INCLUDE_DIRS - the libfftw3 include directory
# FFTW3_LIBRARIES - The libfftw3 libraries

if(PKG_CONFIG_FOUND)
  pkg_check_modules (FFTW3 libfftw3-3>=3.3.0)
else()
  find_path(FFTW3_INCLUDE_DIRS fftw3.h)
  find_library(FFTW3_LIBRARIES fftw3)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFTW3 DEFAULT_MSG FFTW3_INCLUDE_DIRS FFTW3_LIBRARIES)

list(APPEND FFTW3_DEFINITIONS -DHAVE_LIBFFTW3=1)
mark_as_advanced(FFTW3_INCLUDE_DIRS FFTW3_DEFINITIONS FFTW3_LIBRARIES)