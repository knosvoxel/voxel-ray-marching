#include "vox_instance.h"

VoxInstance::VoxInstance(const ivec3 modelSize, const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData, MeasurementData& measurements) : rawVoxelData(voxelData)
{
	Timer timer;
	timer.start();

	lowerBounds = worldOffset - floor(vec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x) / 2.0f);
	upperBounds = lowerBounds + vec3(modelSize.y, modelSize.z, modelSize.x);

	size = upperBounds - lowerBounds;
	sizeInBricks = (size + ivec3(7)) / ivec3(8);

	timer.stop();
	measurements.treeGenerationDuration += timer.elapsedMilliseconds();
}

const int32 VoxInstance::getPoolIndex(int32 bx, int32 by, int32 bz)
{
	return bx + bz * sizeInBricks.x + by * sizeInBricks.x * sizeInBricks.z;
}