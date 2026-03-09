#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "compute.h"

#include <vector>

#include "ogt_wrapper.h"
#include "timer.h"

using namespace glm;

typedef struct RotationData {
	vec4 instanceSize;
	vec4 rotatedSize;
	vec4 minBounds;
	mat4 transform;
};

typedef struct InstanceData {
	vec3 position_offset;
	uint32 bit_offset;
	ivec3 size;
	uint32 padding;
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

typedef struct VoxInstance {
	vec3 lowerBounds; // world transform
	vec3 upperBounds;
	vec3 size;

	uint32 biggestLevelSize;
	uint8* rawVoxelData;

	std::vector<TreeNode> nodes;
	std::vector<uint8> leafs;

	size_t getTotalSizeInByte() {
		size_t totalBytes = sizeof(VoxInstance);
		std::cout << "num Nodes: " << nodes.size() << std::endl;
		totalBytes += nodes.size() * sizeof(TreeNode);
		std::cout << "num Leafs: " << leafs.size() << std::endl;
		totalBytes += leafs.size() * sizeof(uint8);

		return totalBytes;
	}
};

class VoxScene {
public:
	VoxScene() {};
	~VoxScene() {};

	void load(const char* path, ComputeShader& cam_compute);

	void cleanup();

	//std::vector<uint8> voxelData;
	//std::vector<InstanceData> modelData;

	std::vector<VoxInstance> instances;

	//uint32_t modelArraySize;

private:
	uint8* createRotatedModelCPU(const ogt_vox_scene* scene, uint32 instanceIdx, ivec3& rotatedModelSize);

	uint32 getClosestInstanceLevelSize(vec3 modelSize);
	uint32 getClosestRootLevelSize(vec3 modelSize);
	TreeNode generateInstanceTree(VoxInstance& instance, int32 levelSize, ivec3 pos = {});

	// buffers
	//uint32 voxelDataBuffer, modelDataBuffer;
	uint32 treeNodesBuffer, leafsBuffer;
	uint32 palette;

	std::vector<uint32> rootTreeLevelSizes{ 8, 32, 128, 512, 2048 };

};