#include "vox_instance.h"

VoxInstance::VoxInstance(const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData, MeasurementData& measurements) : rawVoxelData(voxelData)
{
	Timer timer;
	timer.start();

	lowerBounds = worldOffset - floor(vec3(rotatedModelSize) / 2.0f);
	upperBounds = lowerBounds + vec3(rotatedModelSize);

	size = upperBounds - lowerBounds;

	ivec3 brickLower = ivec3(lowerBounds + vec3(1024)) / 8;
	ivec3 brickUpper = ivec3(upperBounds + vec3(1024 + 7)) / 8;

	occupiedBricks = brickUpper - brickLower;

	timer.stop();
}

const int32 VoxInstance::getPoolIndex(int32 bx, int32 by, int32 bz)
{
	return bx + bz * occupiedBricks.x + by * occupiedBricks.x * occupiedBricks.z;
}