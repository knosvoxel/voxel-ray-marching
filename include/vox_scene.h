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

typedef struct VoxInstance {
	vec3 lowerBounds; // world transform
	vec3 upperBounds;

	uint8* rawVoxelData;

	std::vector<TreeNode> nodes;
	std::vector<uint8> leafs;

	vec3 getModelSize() {
		return upperBounds - lowerBounds;
	}
};

typedef struct TreeNode {
	uint32 header; // 1 bit: isLeaf | 31 bits: childPtr
	uint64 childMask;

	bool isLeaf() const {
		return (header & 0x80000000) != 0;
	}

	uint32 getChildPtr() const {
		return header & 0x7FFFFFFF;
	}

	void set(bool isLeaf, uint32 ptr) {
		header = (static_cast<uint32>(isLeaf) << 31) | (ptr & 0x7FFFFFFF);
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

	uint32 getClosestInstanceTreeLevel(vec3 modelSize);
	uint32 getClosestRootTreeLevel(vec3 modelSize);
	TreeNode generateInstanceTree(std::vector<TreeNode>& nodes, std::vector<uint8>& leafs, int32 level);

	// buffers
	//uint32 voxelDataBuffer, modelDataBuffer;
	uint32 palette;

	std::vector<uint32> instanceTreeLevelSizes{ 256, 64, 16, 4 };
	std::vector<uint32> rootTreeLevelSizes{ 2048, 512, 128, 32, 8 };

};