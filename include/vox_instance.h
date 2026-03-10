#pragma once

#include <vector>

#include "glm/glm.hpp"

using namespace glm;

 //8 x 8 x 8 voxels sorted on x z y order
typedef struct Brick {
	static constexpr int32 sizeXZ = 8, sizeY = 8;
	static constexpr int32 numVoxels = sizeXZ * sizeXZ * sizeY; // 512
	uint8 data[numVoxels] = {};

	// color index getter: x + z * 8 + y * 64 (XZY order)
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
	VoxInstance() {};
	VoxInstance(const ivec3 modelSize, const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData);

	static uint32 getClosestTreeLevelSize(ivec3 modelSize);

	void cleanup();

	ivec3 sizeInBricks;
	std::vector<Brick*> bricks; // sparse: nullptr = empty brick

	vec3 lowerBounds; // world transform
	vec3 upperBounds;

	ivec3 size;

	uint32 biggestLevelSize;
	uint8* rawVoxelData;

	std::vector<TreeNode> nodes;
	std::vector<uint8> leafs;
};

static TreeNode generateTreeInstance(VoxInstance& instance, int32 levelSize, ivec3 pos = {})
{
	TreeNode node{};

	// Create leaf
	if (levelSize == 4) {
		bool anyVoxel = false;
		uint64 currentMask = 0;
		std::vector<uint8> tempLeafData;

		//#pragma omp for schedule(dynamic)
		for (int32 i = 0; i < 64; i++) {
			ivec3 offset = ivec3(i % 4, i / 16, (i / 4) % 4);
			ivec3 globalPos = pos + offset;

			uint8 colorIdx = 0;
			if (globalPos.x < instance.size.x && globalPos.y < instance.size.y && globalPos.z < instance.size.z) {
				uint32 srcIdx = globalPos.x + (globalPos.y * instance.size.x) + (globalPos.z * instance.size.x * instance.size.y);
				colorIdx = instance.rawVoxelData[srcIdx];
			}

			if (colorIdx != 0) {
				currentMask |= (1ull << i);
				anyVoxel = true;
				tempLeafData.push_back(colorIdx);
			}
		}

		if (anyVoxel) {
			node.setIsLeaf(true);
			node.setChildMask(currentMask);
			node.setChildPtr(instance.leafs.size());
			instance.leafs.insert(instance.leafs.end(), tempLeafData.begin(), tempLeafData.end());
			return node;
		}
		else {
			node.setChildMask(0);
			return node;
		}
	}

	levelSize /= 4;

	std::vector<TreeNode> children;
	children.reserve(64);

	for (int32 i = 0; i < 64; i++) {
		ivec3 childPos = ivec3(i % 4, i / 16, (i / 4) % 4);
		TreeNode child = generateTreeInstance(instance, levelSize, pos + (childPos * levelSize));

		if (child.getChildMask() != 0) {
			uint64 mask = node.getChildMask();
			node.setChildMask(mask |= 1ull << i);
			node.setChildPtr(instance.nodes.size());
			children.push_back(child);
		}
	}

	instance.nodes.insert(instance.nodes.end(), children.begin(), children.end());

	return node;
}