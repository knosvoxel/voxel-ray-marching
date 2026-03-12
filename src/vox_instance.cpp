#include "vox_instance.h"

// Left-pack data according to mask. Folling bytes are undefined
void leftPack(uint8 data[64], uint64 mask) {
#if SIMD_AVX512
	_mm512_storeu_epi8(data, _mm512_maskz_compress_epi8(mask, _mm512_loadu_epi8(data)));
	return;
#endif

	for (uint32 i = 0, j = 0; mask != 0; i++) {
		data[j] = data[i];
		j += (mask & 1);
		mask >>= 1;
	}
}

// levelSize is bit representation of amount of voxels per dimension in level
// levelSize = 2: 4³ voxels per section (0 - 3) -- leaf
// levelSize = 4: 16³ voxels per section (0 - 15)
// levelSize = 6: 64³ voxels per section (0 - 63)
// levelSize = 8: 256³ voxels per section (0 - 255);
static TreeNode generateTreeInstance(VoxInstance& instance, int32 levelSize, ivec3 pos = {})
{
	TreeNode node{};

	// check if any sector in region at position of current levelSize, otherwise fully skip subtree
	if (levelSize >= 4) {
		// positions in sector space
		ivec3 sectorMin = pos / ivec3(32);
		ivec3 sectorMax = (pos + ivec3((1 << levelSize) - 1)) / ivec3(32);

		if (!instance.anySectorExits(sectorMin, sectorMax)) return node;
	}

	// Create leaf
	// 4 x 4 x 4 voxel
	if (levelSize == 2) {
		const Brick* brick = instance.getBrick(pos);
		if (brick == nullptr) return node; // empty brick

		// brick origin
		ivec3 localOrigin = pos % ivec3(8);

		// 4 x 4 x 4 brick subtile
		uint8 bricklet[64];
		for (int32 i = 0; i < 64; i++)
		{
			ivec3 offset = ivec3(i % 4, i / 16, (i / 4) % 4);
			bricklet[i] = brick->data[Brick::getIndex(
				localOrigin.x + offset.x,
				localOrigin.y + offset.y,
				localOrigin.z + offset.z
			)];
		}

		uint64 mask = Brick::packBits64(bricklet);
		if (mask == 0) return node; // no voxels in subtile

		// pack subtile voxel data to the left
		leftPack(bricklet, mask);

		node.setIsLeaf(true);
		node.setChildMask(mask);
		node.setChildPtr(instance.leafs.size());
		instance.leafs.insert(instance.leafs.end(), bricklet, bricklet + std::popcount(mask));
		return node;
	}

	levelSize -= 2;

	std::vector<TreeNode> children;
	children.reserve(64);

	for (int32 i = 0; i < 64; i++) {
		ivec3 childPos = i >> ivec3(0, 4, 2) & 3; // position xyz range: 0 - 3
		TreeNode child = generateTreeInstance(instance, levelSize, pos + (childPos << levelSize));

		if (child.getChildMask() != 0) {
			uint64 mask = node.getChildMask();
			node.setChildMask(mask |= 1ull << i); // 1ull = 1 as 64 bit value
			children.push_back(child);
		}
	}

	node.setChildPtr(instance.nodes.size());
	instance.nodes.insert(instance.nodes.end(), children.begin(), children.end());

	return node;
}

VoxInstance::VoxInstance(const ivec3 modelSize, const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData, MeasurementData& measurements) : rawVoxelData(voxelData)
{
	Timer timer;
	timer.start();

	lowerBounds = worldOffset - floor(vec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x) / 2.0f);
	upperBounds = lowerBounds + vec3(modelSize.y, modelSize.z, modelSize.x);

	size = upperBounds - lowerBounds;
	sizeInSectors = (size + ivec3(31)) / ivec3(32);
	sizeInBricks = (size + ivec3(7)) / ivec3(8);

	measurements.totalSectorCount += (sizeInSectors.x * sizeInSectors.y * sizeInSectors.z);
	measurements.totalBrickCount += (sizeInBricks.x * sizeInBricks.y * sizeInBricks.z);

	biggestLevelSize = getClosestTreeLevelSize(size);

	timer.stop();
	measurements.preprocessingDuration += timer.elapsedMilliseconds();
	timer.start();

	generateSectors();

	timer.stop();
	measurements.sectorGenerationDuration += timer.elapsedMilliseconds();
	timer.start();

	// root node
	nodes.resize(1);
	TreeNode root = generateTreeInstance(*this, biggestLevelSize, ivec3(0));
	nodes[0] = root;
	timer.stop();
	measurements.treeGenerationDuration += timer.elapsedMilliseconds();
}

const int32 VoxInstance::getPoolIndex(int32 bx, int32 by, int32 bz)
{
	return bx + bz * sizeInBricks.x + by * sizeInBricks.x * sizeInBricks.z;
}

const int32 VoxInstance::getSectorIndex(int32 sx, int32 sy, int32 sz)
{
	return sx + sz * sizeInSectors.x + sy * sizeInSectors.x * sizeInSectors.z;
}

const int32 VoxInstance::getLocalBrickIndex(int32 lx, int32 ly, int32 lz)
{
	return lx + lz * 4 + ly * 16;
}

const Brick* VoxInstance::getBrick(ivec3 voxelPos)
{
	ivec3 sectorPos = voxelPos / ivec3(32);
	if (sectorPos.x >= sizeInSectors.x ||
		sectorPos.y >= sizeInSectors.y ||
		sectorPos.z >= sizeInSectors.z)
		return nullptr;

	Sector* sector = sectors[getSectorIndex(sectorPos.x, sectorPos.y, sectorPos.z)];
	if (sector == nullptr) return nullptr;

	// get brick of voxel within sector
	// local between 0 and 3 in all three directions
	// 
	// sector has 32 x 32 x 32 voxels, brick has 8 x 8 x 8
	// first get offset within sector, then fetch which brick is at that local position in the sector
	ivec3 localBrickPos = (voxelPos % ivec3(32)) / ivec3(8);
	return sector->bricks[getLocalBrickIndex(localBrickPos.x, localBrickPos.y, localBrickPos.z)];
}

const bool VoxInstance::anySectorExits(ivec3 sectorMin, ivec3 sectorMax)
{
	for (int32 sy = sectorMin.y; sy <= sectorMax.y; sy++)
	{
		for (int32 sz = sectorMin.z; sz <= sectorMax.z; sz++)
		{
			for (int32 sx = sectorMin.x; sx <= sectorMax.x; sx++)
			{
				if (
					sx < sizeInSectors.x &&
					sy < sizeInSectors.y &&
					sz < sizeInSectors.z)
				{
					if (sectors[getSectorIndex(sx, sy, sz)] != nullptr)
						return true;
				}
			}
		}
	}
	return false;
}

void VoxInstance::generateSectors()
{
	int32 totalSectors = sizeInSectors.x * sizeInSectors.y * sizeInSectors.z;
	int32 totalBricks = sizeInBricks.x * sizeInBricks.y * sizeInBricks.z;

	sectors.assign(totalSectors, nullptr);
	std::vector<std::unique_ptr<Brick>> brickPool(totalBricks);

#pragma omp parallel for collapse(3) schedule(static)
	for (int32 by = 0; by < sizeInBricks.y; by++)
	{
		for (int32 bz = 0; bz < sizeInBricks.z; bz++)
		{
			for (int32 bx = 0; bx < sizeInBricks.x; bx++)
			{	
				Brick local{};
				bool anySet = false;

				for (int32 ly = 0; ly < 8; ly++)
				{
					for (int32 lz = 0; lz < 8; lz++)
					{
						for (int32 lx = 0; lx < 8; lx++)
						{
							ivec3 globalPos = ivec3(bx * 8 + lx, by * 8 + ly, bz * 8 + lz);
							if (globalPos.x >= size.x || globalPos.y >= size.y || globalPos.z >= size.z)
								continue;

							uint32 srcIdx = globalPos.x + globalPos.y * size.x + globalPos.z * size.x * size.y;
							uint8 colorIdx = rawVoxelData[srcIdx];
							if (colorIdx != 0) {
								local.data[Brick::getIndex(lx, ly, lz)] = colorIdx;
								anySet = true;
							}
						}
					}
				}

				if (anySet) {
					int32 idx = getPoolIndex(bx, by, bz);
					brickPool[idx] = std::make_unique<Brick>(local);
				}
			}
		}
	}

	// assign bricks to sectors
	for (int32 by = 0; by < sizeInBricks.y; by++)
	{
		for (int32 bz = 0; bz < sizeInBricks.z; bz++)
		{
			for (int32 bx = 0; bx < sizeInBricks.x; bx++)
			{
				int32 poolIdx = getPoolIndex(bx, by, bz);
				if (!brickPool[poolIdx]) continue;

				ivec3 sectorPos = ivec3(bx, by, bz) / ivec3(4);
				int32 sectorIdx = getSectorIndex(sectorPos.x, sectorPos.y, sectorPos.z);
				if (!sectors[sectorIdx]) sectors[sectorIdx] = new Sector();

				ivec3 localPos = ivec3(bx, by, bz) % ivec3(4);
				int32 localIdx = getLocalBrickIndex(localPos.x, localPos.y, localPos.z);
				sectors[sectorIdx]->bricks[localIdx] = brickPool[poolIdx].release();
			}	
		}
	}
}

void VoxInstance::cleanup() {
	free(rawVoxelData);
	rawVoxelData = nullptr;

	for (Sector* sector : sectors) {
		if (sector == nullptr) continue;
		for (Brick* brick : sector->bricks)
			delete brick;
		delete sector;
	}
}

uint32 VoxInstance::getClosestTreeLevelSize(ivec3 modelSize)
{
	int32 maxDim = max(modelSize.x, modelSize.y);
	maxDim = max(maxDim, modelSize.z);

	if (maxDim <= 4) return 2;
	if (maxDim <= 16) return 4;
	if (maxDim <= 64) return 6;
	if (maxDim <= 256) return 8;

	assert(false && "Model size exceeds expected maximum size.");
	return -1;
}
