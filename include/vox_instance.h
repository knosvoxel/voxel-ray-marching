#pragma once

#include <vector>
#include <memory>
#include <iostream>

#include "glm/glm.hpp"

#include "timer.h"

using namespace glm;

// per instance measurements
typedef struct MeasurementData {
	float64 preprocessingDuration = 0.0;
	float64 sectorGenerationDuration = 0.0;
	float64 treeGenerationDuration = 0.0;
};

 //8 x 8 x 8 voxels sorted on x z y order
typedef struct Brick {
	static constexpr int32 sizeXZ = 8, sizeY = 8;
	static constexpr int32 numVoxels = sizeXZ * sizeXZ * sizeY; // 512
	uint8 data[numVoxels] = {};

	// index getter to fetch a brick's voxel at position
	// x + z * 8 + y * 64 (XZY order)
	static int32 getIndex(int32 x, int32 y, int32 z) {
		return (x & 7) | ((z & 7) << 3) | ((y & 7) << 6);
	}

	// Create 64-bit mask of "data[i] != 0"
	// 1 = existing color at i, 0 = empty voxel
	static uint64 packBits64(const uint8 data[64]) {
		uint64 mask = 0;
#pragma omp simd reduction(|:mask)
		for (int32 i = 0; i < 64; i++)
			mask |= (uint64)(data[i] != 0) << i;
		return mask;
	}
};

// 4 x 4 x 4 bricks
typedef struct Sector {
	Brick* bricks[64] = {};

	bool isEmpty() const {
		for (Brick* brick : bricks)
			if (brick != nullptr) return false;
		return true;
	}
};

typedef struct TreeNode {
	uint32 header; // 1 bit: isLeaf | 31 bits: childPtr
	uint32 childMaskLow;
	uint32 childMaskHigh;

	bool isLeaf() const {
		return (header & 1u) != 0;  // bit 0
	}

	uint64 getChildMask() {
		return (uint64)childMaskLow | ((uint64)childMaskHigh << 32);
	}

	void setChildMask(uint64 mask) {
		childMaskLow = (uint32)mask;
		childMaskHigh = (uint32)(mask >> 32);
	}

	uint32 getChildPtr() const {
		return header >> 1;  // bits 1-31
	}

	void setIsLeaf(bool leaf) {
		header = (header & ~1u) | (static_cast<uint32>(leaf) & 1u);  // bit 0
	}

	void setChildPtr(uint32 ptr) {
		header = (header & 1u) | (ptr << 1);  // bits 1-31
	}
};

struct VoxInstance {
//public:
	VoxInstance() {};
	VoxInstance(const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData, MeasurementData& measurements);
	~VoxInstance() {};

	const int32 getPoolIndex(int32 bx, int32 by, int32 bz);

	int32 posInArray = -1;

	// instance dimensions and transform
	vec3 lowerBounds; // world transform
	vec3 upperBounds;

	ivec3 size;

	ivec3 occupiedBricks;
	// voxel data 
	uint8* rawVoxelData;

};