#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "compute.h"
#include "vox_instance.h"

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

class VoxScene {
public:
	VoxScene() {};
	~VoxScene() {};

	void load(const char* path, ComputeShader& cam_compute);

	void cleanup();

	std::vector<VoxInstance> instances;

private:
	uint8* createRotatedModelCPU(const ogt_vox_scene* scene, uint32 instanceIdx, ivec3& rotatedModelSize);

	uint32 getClosestRootLevelSize(vec3 modelSize);

	// buffers
	//uint32 voxelDataBuffer, modelDataBuffer;
	uint32 treeNodesBuffer, leafsBuffer;
	uint32 palette;

	std::vector<uint32> rootTreeLevelSizes{ 8, 32, 128, 512, 2048 };

};