/*
     Copyright 2024 Felix Weinmann

     Licensed under the Apache License, Version 2.0 (the "License");
     you may not use this file except in compliance with the License.
     You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

     Unless required by applicable law or agreed to in writing, software
     distributed under the License is distributed on an "AS IS" BASIS,
     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
     See the License for the specific language governing permissions and
     limitations under the License.
 */
#pragma once

#include <string>
#include <vector>

#include "proto/ssl_vision_detection.pb.h"


std::vector<SSL_DetectionFrame> parseGroundTruth(const std::string& source);
const SSL_DetectionFrame& getCorrespondingFrame(const std::vector<SSL_DetectionFrame>& groundTruth, uint32_t frameId);

