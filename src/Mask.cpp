#include "Mask.h"

static void addForHeight(RLEVector& mask, Perspective& perspective, const double height) {
	double halfLength = perspective.getFieldLength()/2.0 + perspective.getBoundaryWidth();
	double halfWidth = perspective.getFieldWidth()/2.0 + perspective.getBoundaryWidth();
	for(int y = 0; y < perspective.getHeight(); y++) {
		bool inRun = false;
		int runStart = 0;
		for(int x = 0; x < perspective.getWidth(); x++) {
			V2 groundPos = perspective.image2field({(double)x, (double)y}, height);
			if(groundPos.x < -halfLength || groundPos.x > halfLength || groundPos.y < -halfWidth || groundPos.y > halfWidth) {
				if (inRun) {
					mask.add({runStart, y, x - runStart});
					inRun = false;
				}
			} else {
				if(!inRun) {
					runStart = x;
					inRun = true;
				}
			}
		}

		if (inRun)
			mask.add({runStart, y, perspective.getWidth() - runStart});
	}
}

void Mask::geometryCheck() {
	if(geometryVersion == perspective->getGeometryVersion())
		return;

	geometryVersion = perspective->getGeometryVersion();
	mask.clear();

	addForHeight(mask, *perspective, ballRadius);
	//addForHeight(mask, *perspective, maxBotHeight);
}

std::shared_ptr<CLArray> Mask::scanArea(AlignedArrayPool& arrayPool) {
	return mask.scanArea(arrayPool);
}
