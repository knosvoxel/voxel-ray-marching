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
	uint8* voxelData;
	ivec3 modelSize;
};

class VoxScene {
public:
	VoxScene() {};
	~VoxScene() {};

	void load(const char* path, ComputeShader& cam_compute);

	void cleanup();

	std::vector<uint8> voxelData;
	std::vector<InstanceData> modelData;

	std::vector<VoxInstance> instances;

	uint32_t modelArraySize;

private:
	uint8* createRotatedModelCPU(const ogt_vox_scene* scene, uint32 instanceIdx, ivec3& rotatedModelSize);

	// buffers
	uint32 voxelDataBuffer, modelDataBuffer;
	uint32 palette;

};