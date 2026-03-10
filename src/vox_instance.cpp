#include "vox_instance.h"

VoxInstance::VoxInstance(const ivec3 modelSize, const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData) : rawVoxelData(voxelData)
{
	sizeInBricks = (size + ivec3(7)) / ivec3(8);

	lowerBounds = worldOffset - floor(vec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x) / 2.0f);
	upperBounds = lowerBounds + vec3(modelSize.y, modelSize.z, modelSize.x);

	size = upperBounds - lowerBounds;

	biggestLevelSize = getClosestTreeLevelSize(size);
	
	// root node
	nodes.resize(1);
}

void VoxInstance::cleanup() {
	free(rawVoxelData);
	rawVoxelData = nullptr;
}

std::vector<int32> instanceTreeLevelSizes{ 4, 16, 64, 256 };
uint32 VoxInstance::getClosestTreeLevelSize(ivec3 modelSize)
{
	int32 maxDim = max(modelSize.x, modelSize.y);
	maxDim = max(maxDim, modelSize.z);

	for (int32 level = 0; level < instanceTreeLevelSizes.size(); ++level)
	{
		int32 levelSize = instanceTreeLevelSizes[level];
		if (maxDim <= levelSize)
			return levelSize;
	}

	assert(false && "Model size exceed expected maximum size.");
	return -1;
}