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
	uint64 childMask;

	bool isLeaf() const {
		return (header & 0x80000000) != 0;
	}

	uint32 getChildPtr() const {
		return header & 0x7FFFFFFF;
	}

	void setIsLeaf(bool isLeaf) {
		header = (static_cast<uint32>(isLeaf) << 31) | (header & 0x7FFFFFFF);
	}

	void setChildPtr(uint32 ptr) {
		header = (header & 0x80000000) | (ptr & 0x7FFFFFFF);
	}
};

typedef struct VoxInstance {
	vec3 lowerBounds; // world transform
	vec3 upperBounds;
	vec3 size;

	uint8* rawVoxelData;

	std::vector<TreeNode> nodes;
	std::vector<uint8> leafs;
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
	uint32 palette;

	std::vector<uint32> instanceTreeLevelSizes{ 4, 16, 64, 256 };
	std::vector<uint32> rootTreeLevelSizes{ 8, 32, 128, 512, 2048 };

};