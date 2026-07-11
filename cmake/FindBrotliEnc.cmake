# Copyright 2017 Igalia S.L. All Rights Reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

# Try to find BrotliEnc. Once done, this will define
#
#  BROTLIENC_FOUND - system has BrotliEnc.
#  BROTLIENC_INCLUDE_DIRS - the BrotliEnc include directories
#  BROTLIENC_LIBRARIES - link these to use BrotliEnc.

find_package(PkgConfig)

pkg_check_modules(PC_BROTLIENC libbrotlienc)

if(NOT PC_BROTLIENC_LIBRARIES)
	# Fall-back for systems without pkg-config; both libraries must
	# be present, otherwise linking will likely fail for static builds.
	list(APPEND PC_BROTLIENC_LIBRARIES brotlienc brotlicommon)
endif()

find_path(BROTLIENC_INCLUDE_DIRS
    NAMES brotli/encode.h
    HINTS ${PC_BROTLIENC_INCLUDEDIR}
)

set(BROTLIENC_LIBRARIES "")
foreach(_lib ${PC_BROTLIENC_LIBRARIES})
	find_library(BROTLIENC_PATH_${_lib} ${_lib}
		HINTS ${PC_BROTLIENC_LIBRARY_DIRS})
	if(NOT BROTLIENC_PATH_${_lib})
		unset(BROTLIENC_LIBRARIES)
		break()
	endif()
	list(APPEND BROTLIENC_LIBRARIES "${BROTLIENC_PATH_${_lib}}")
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BrotliEnc
    REQUIRED_VARS BROTLIENC_INCLUDE_DIRS BROTLIENC_LIBRARIES
    FOUND_VAR BROTLIENC_FOUND
    VERSION_VAR PC_BROTLIENC_VERSION)

mark_as_advanced(
    BROTLIENC_INCLUDE_DIRS
    BROTLIENC_LIBRARIES
)
