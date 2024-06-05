#pragma once


#include <memory>
#include <string>
#include <opencv2/core/mat.hpp>
#include <utility>
#include "opencl.h"


class CVMap;


class Image : public CLArray {
public:
	Image(const Image& other) = default;
	Image(const PixelFormat* format, int width, int height): CLArray(width*height*format->pixelSize()), format(format), width(width), height(height), timestamp(0), name() {}
	Image(const PixelFormat* format, int width, int height, std::string name): CLArray(width*height*format->pixelSize()), format(format), width(width), height(height), timestamp(0), name(std::move(name)) {}
	Image(const PixelFormat* format, int width, int height, double timestamp): CLArray(width*height*format->pixelSize()), format(format), width(width), height(height), timestamp(timestamp), name() {}

	//Only use these constructors if not possible otherwise due to necessary copy (because of potential alignment mismatch for zero-copy support)
	Image(const PixelFormat* format, int width, int height, unsigned char* data): CLArray(data, width*height*format->pixelSize()), format(format), width(width), height(height), timestamp(0) {}
	Image(const PixelFormat* format, int width, int height, double timestamp, unsigned char* data): CLArray(data, width*height*format->pixelSize()), format(format), width(width), height(height), timestamp(timestamp) {}

	virtual ~Image() = default;

	[[nodiscard]] CVMap cvRead() const;
	[[nodiscard]] CVMap cvWrite();
	[[nodiscard]] CVMap cvReadWrite();

	[[nodiscard]] Image toGrayscale() const;
	[[nodiscard]] Image toBGR() const;
	[[nodiscard]] Image toRGGB() const;
	[[nodiscard]] Image toUpscaleRGGB() const;

	void save(const std::string& suffix, float factor = 1.0f) const;

	const PixelFormat* format;
	const int width;
	const int height;
	// timestamp of 0 indicates unavailability
	const double timestamp;
	const std::string name;
};


class CVMap {
public:
	explicit CVMap(const Image& image, int clRWType);
	~CVMap() = default;

	CVMap(CVMap&& other) noexcept = default;
	CVMap ( const CVMap & ) = delete;
	CVMap& operator= ( const CVMap & ) = delete;
	cv::Mat& operator*() { return mat; }
	cv::Mat* operator-> () { return &mat; }
	const cv::Mat& operator*() const { return mat; }
	const cv::Mat* operator-> () const { return &mat; }

private:
	CLMap<uint8_t> map;
	cv::Mat mat;
};