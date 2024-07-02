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

unset(mvIMPACT_INCLUDE_DIRS)
unset(mvIMPACT_LIBS)

if (WITH_MVIMPACT)
find_path(mvIMPACT_INCLUDE_DIRS NAMES
        mvIMPACT_CPP/mvIMPACT_acquire.h
        HINTS
        /opt/mvIMPACT_Acquire/)

find_library(mvIMPACT_LIBS NAMES
        mvDeviceManager
        HINTS
        /opt/mvIMPACT_Acquire/lib/x86_64)

if (mvIMPACT_LIBS AND mvIMPACT_INCLUDE_DIRS)
    add_definitions( -DMVIMPACT )
    message(STATUS "mvIMPACT found, activating mvIMPACT support.")
endif (mvIMPACT_LIBS AND mvIMPACT_INCLUDE_DIRS)

endif (WITH_MVIMPACT)
