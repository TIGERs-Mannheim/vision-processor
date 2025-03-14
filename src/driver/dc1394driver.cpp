/*
     Copyright 2025 Felix Weinmann

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
#include "dc1394driver.h"

static int compareGUID(const void* a, const void* b) {
	int aGUID = ((dc1394camera_id_t*)a)->guid;
	int bGUID = ((dc1394camera_id_t*)b)->guid;
	return (aGUID > bGUID) - (aGUID < bGUID);
}

DC1394Driver::DC1394Driver(unsigned int id, double exposure, double gain, double gamma, WhiteBalanceType wbType, const std::vector<double>& wbValues) {
	dc1394 = dc1394_new();

	dc1394camera_list_t* cam_list;
	if (dc1394_camera_enumerate(dc1394, &cam_list) != DC1394_SUCCESS || cam_list == nullptr) {
		std::cerr << "[DC1394] Couldn't get camera list" << std::endl;
		exit(1);
	}

	if(cam_list->num <= id) {
		std::cerr << "[DC1394] Insufficient camera amount for given cam_id: " << id << "/" << cam_list->num << std::endl;
		exit(1);
	}

	qsort(cam_list->ids, cam_list->num, sizeof(dc1394camera_id_t), &compareGUID);
	camera = dc1394_camera_new(dc1394, cam_list->ids[id].guid);
	if (camera == nullptr) {
		std::cerr << "[DC1394] Couldn't get camera " << id << std::endl;
		exit(1);
	}

	int channel;
	if(dc1394_iso_allocate_channel(camera, 0xffff , &channel) == DC1394_SUCCESS && dc1394_video_set_iso_channel(camera, channel) == DC1394_SUCCESS) {
		std::cout << "[DC1394] Got iso channel: " << channel << std::endl;
	} else {
		std::cerr << "[DC1394] Couldn't get iso channel, using two cameras on one bus won't work." << std::endl;
	}


}

DC1394Driver::~DC1394Driver() {
	dc1394_capture_stop(camera);
	dc1394_video_set_transmission(camera, DC1394_OFF);
	dc1394_camera_free(camera);
	dc1394_free(dc1394);
}