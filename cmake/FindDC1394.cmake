#    Copyright 2024 Felix Weinmann
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.

if (WITH_DC1394)
    pkg_check_modules(DC1394 libdc1394-2)

    if (NOT (DC1394_LIBRARIES STREQUAL "DC1394_LIBRARIES-NOTFOUND"))
        add_definitions( -DDC1394 )
        message(STATUS "DC1394 found, activating DC1394 support.")
    else()
        set(DC1394_LIBRARIES "")
    endif ()

else()
    set(DC1394_LIBRARIES "")
endif (WITH_DC1394)