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


#include <thread>
#include <google/protobuf/message.h>
#include "proto/ssl_vision_geometry.pb.h"
#include "proto/ssl_vision_detection.pb.h"

#ifdef _WIN32
#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include <Ws2tcpip.h> // needed for ip_mreq definition for multicast
#include <Windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


/** General purpose protobuf multicast UDP socket wrapper */
class UDPSocket {
public:
	UDPSocket(const std::string& ip, uint16_t port);
	~UDPSocket();

	void send(const google::protobuf::Message& msg);

private:
	virtual void parse(char* data, int length) = 0;
	void run();

	bool closing = false;
	int socket_;
	struct sockaddr addr_;

	std::thread receiver;
};


/** Internal detection wrapper for position prediction (tracking). */
struct TrackingState {
	int id; // -1: ball, 0-15: yellow bot, 16-31: blue bot
	double timestamp;
	float x, y, z, w;
	float vx, vy, vz, vw;
	float confidence;
};


/** Socket handling vision messages. */
class VisionSocket: public UDPSocket {
public:
	VisionSocket(const std::string &ip, uint16_t port, int camId, float defaultBotHeight): UDPSocket(ip, port), camId(camId), defaultBotHeight(defaultBotHeight) {}

	/** Check if a new geometry update has been received and update geometry and geometryVersion accordingly. */
	void geometryCheck();
	int getGeometryVersion() const { return geometryVersion; }
	SSL_GeometryData& getGeometry() { return geometry; }

	std::map<int, std::vector<TrackingState>> getTrackedObjects();

	/** Update the clock time offset. */
	void updateTime();
	std::vector<float> getReceivedOffsets();
private:
	void parse(char* data, int length) override;

	/** Update trackedObjects with the contents of the DetectionFrame. */
	void detectionTracking(const SSL_DetectionFrame& detection);
	/** Synchronize the time with the timestamps and offsets from the DetectionFrame. */
	void timeSynchronization(const SSL_DetectionFrame& detection);

	/** Camera id of this socket. */
	const int camId;
	/** Default bot height to be used if the bot height is missing from received detection frames. */
	const float defaultBotHeight;

	/** Increments each time the geometry has changed. */
	int geometryVersion = 0;
	/** Ball radius according to the geometry. */
	float ballRadius = 21.5f;
	/** Current geometry to be used by other tasks. */
	SSL_GeometryData geometry;
	/** Last received geometry, should only be accessed when having geometryMutex locked. */
	SSL_GeometryData receivedGeometry;
	std::mutex geometryMutex;

	/** Currently tracked objects. Should only be accessed when having trackedMutex locked. */
	std::map<int, std::vector<TrackingState>> trackedObjects;
	std::mutex trackedMutex;

	/** Time offsets measured relative to or reported by other cameras. Should only be accessed when having offsetMutex locked. */
	std::vector<float> sentOffsets; // local.t_sent - other.time
	std::vector<float> receivedOffsets; // other.t_sent - local.time
	std::mutex offsetMutex;
};

/** Socket handling game controller messages. */
class GCSocket: public UDPSocket {
public:
	/** Bot heights functions as database of known team names -> bot height mappings. */
	GCSocket(const std::string &ip, uint16_t port, const std::map<std::string, double>& botHeights);

	/** Highest bot height in botHeights. */
	double maxBotHeight;
	/** Mean bot height in botHeights, used for teams not present in botHeights. */
	double defaultBotHeight;
	/** Current yellow bot height according to the game controller team name. */
	double yellowBotHeight;
	/** Current blue bot height according to the game controller team name. */
	double blueBotHeight;

private:
	void parse(char* data, int length) override;

	std::map<std::string, double> botHeights;
};