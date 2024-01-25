#pragma once

#include <memory>
#include <utility>

#include "RLEVector.h"
#include "Perspective.h"

class Mask {
public:
	Mask(std::shared_ptr<Perspective> perspective, double maxBotHeight): perspective(std::move(perspective)), maxBotHeight(maxBotHeight) {}

	void geometryCheck();
	std::shared_ptr<CLArray> scanArea(AlignedArrayPool& arrayPool);

	RLEVector& getRuns() { return mask; }

private:
	double maxBotHeight;
	std::shared_ptr<Perspective> perspective;

	RLEVector mask;
	int geometryVersion = 0;
};
