#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "compute.h"
#include "vox_instance.h"

#include <vector>
#include <unordered_map>
#include <memory>
#include <bit>

#include "ogt_wrapper.h"
#include "timer.h"

using namespace glm;

typedef struct RotationData {
	vec4 instanceSize;
	vec4 rotatedSize;
	vec4 minBounds;
	mat4 transform;
};

typedef struct InstanceComputeData {
	vec3 worldOffset;
	uint32 nodeOffset;
	ivec3 size;
	uint32 leafOffset;
	//uint32 biggestLevelSize;
};

struct SceneTimings
{
	float64 sceneFileLoadMs = 0;
	float64 paletteOverheadMs = 0;
	float64 sectorGenerationLoopMs = 0;
	float64 rotationTotalMs = 0;
	float64 sectorGenerationTotalMs = 0;
	float64 sectorGenerationAvgMs = 0;
	float64 treeGenerationMs = 0;
	float64 sceneBufferBuildMs = 0;
};

class VoxScene {
public:
	VoxScene() {};
	~VoxScene() {};

	void load(const char* path, ComputeShader& cam_compute);

	void cleanup();

	const Brick* getBrick(ivec3 voxelPos);
	const bool anySectorExits(ivec3 sectorMin, ivec3 sectorMax);

	std::vector<VoxInstance> instances;

	MeasurementData measurements;
	SceneTimings timings;
	uint32 numInstances;

	uint32 biggestLevelSize = 12;

	std::vector<TreeNode> nodes;
	std::vector<uint8> leafs;

private:
	uint8* createRotatedModelCPU(const ogt_vox_scene* scene, uint32 instanceIdx, ivec3& rotatedModelSize);

	//uint32 getClosestRootLevelSize(vec3 modelSize);

	// sectors
	const int32 getSectorIndex(int32 sx, int32 sy, int32 sz);
	// local brick within sector
	const int32 getLocalBrickIndex(int32 lx, int32 ly, int32 lz);

	// generate bricks from instance voxel data
	void generateSectors(VoxInstance& instance);

	// buffers
	//uint32 voxelDataBuffer, modelDataBuffer;
	uint32 treeNodesBuffer, leafsBuffer;
	uint32 palette;

	ivec3 sizeInBricks;
	ivec3 sizeInSectors;

	// sparse: nullptr = empty
	std::unordered_map<uint64, Sector*> sectors; // key = packed sx|sy|sz

	//std::vector<uint32> rootTreeLevelSizes{ 8, 32, 128, 512, 2048 };
};