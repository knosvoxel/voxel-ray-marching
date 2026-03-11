#include "vox_instance.h"

// Left-pack data according to mask. Folling bytes are undefined
void LeftPack(uint8 data[64], uint64 mask) {
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

static TreeNode generateTreeInstance(VoxInstance& instance, int32 levelSize, ivec3 pos = {})
{
	TreeNode node{};

	// Create leaf
	// 4 x 4 x 4 voxel
	if (levelSize == 4) {
		ivec3 brickPos = pos / ivec3(8);
		ivec3 localOrigin = pos % ivec3(8); // offset within brick (0 or 4 per axis)

		if (brickPos.x >= instance.sizeInBricks.x ||
			brickPos.y >= instance.sizeInBricks.y ||
			brickPos.z >= instance.sizeInBricks.z)
			return node;

		int32 brickIdx = instance.getBrickIndex(brickPos.x, brickPos.y, brickPos.z);
		Brick* brick = instance.bricks[brickIdx];

		if (brick == nullptr) return node; // empty brick

		// 4 x 4 x 4 brick subtile
		uint8 temp[64];
		for (int32 i = 0; i < 64; i++)
		{
			ivec3 offset = ivec3(i % 4, i / 16, (i / 4) % 4);
			temp[i] = brick->data[Brick::getIndex(
				localOrigin.x + offset.x,
				localOrigin.y + offset.y,
				localOrigin.z + offset.z
			)];
		}

		uint64 mask = Brick::packBits64(temp);
		if (mask == 0) return node; // no voxels in subtile

		// pack subtile voxel data to the left
		LeftPack(temp, mask);

		node.setIsLeaf(true);
		node.setChildMask(mask);
		node.setChildPtr(instance.leafs.size());
		instance.leafs.insert(instance.leafs.end(), temp, temp + std::popcount(mask));
		return node;
	}

	levelSize /= 4;

	std::vector<TreeNode> children;
	children.reserve(64);

	for (int32 i = 0; i < 64; i++) {
		ivec3 childPos = ivec3(i % 4, i / 16, (i / 4) % 4);
		TreeNode child = generateTreeInstance(instance, levelSize, pos + (childPos * levelSize));

		if (child.getChildMask() != 0) {
			uint64 mask = node.getChildMask();
			node.setChildMask(mask |= 1ull << i);
			node.setChildPtr(instance.nodes.size());
			children.push_back(child);
		}
	}

	instance.nodes.insert(instance.nodes.end(), children.begin(), children.end());

	return node;
}

VoxInstance::VoxInstance(const ivec3 modelSize, const ivec3 rotatedModelSize, const vec3 worldOffset, uint8* voxelData) : rawVoxelData(voxelData)
{
	lowerBounds = worldOffset - floor(vec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x) / 2.0f);
	upperBounds = lowerBounds + vec3(modelSize.y, modelSize.z, modelSize.x);

	size = upperBounds - lowerBounds;
	sizeInBricks = (size + ivec3(7)) / ivec3(8);

	biggestLevelSize = getClosestTreeLevelSize(size);
	
	generateBrickGrid();

	// root node
	nodes.resize(1);
	TreeNode root = generateTreeInstance(*this, biggestLevelSize, ivec3(0));
	nodes[0] = root;
}

const int32 VoxInstance::getBrickIndex(int32 bx, int32 by, int32 bz)
{
	return bx + bz * sizeInBricks.x + by * sizeInBricks.x * sizeInBricks.z;
}

void VoxInstance::generateBrickGrid()
{
	int32 numBricksX = sizeInBricks.x;
	int32 numBricksY = sizeInBricks.y;
	int32 numBricksZ = sizeInBricks.z;
	int32 totalBricks = numBricksX * numBricksY * numBricksZ;

	std::vector<std::unique_ptr<Brick>> bricksTemp(totalBricks);

#pragma omp parallel for collapse(3) schedule(static)
	for (int32 by = 0; by < numBricksY; by++)
	{
		for (int32 bz = 0; bz < numBricksZ; bz++) 
		{
			for (int32 bx = 0; bx < numBricksX; bx++)
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
					int32 idx = getBrickIndex(bx, by, bz);
					bricksTemp[idx] = std::make_unique<Brick>(local);
				}
			}
		}
	}
	bricks.resize(totalBricks, nullptr);
	for (int32 i = 0; i < totalBricks; i++)
	{
		bricks[i] = bricksTemp[i].release();
	}
}

void VoxInstance::cleanup() {
	free(rawVoxelData);
	rawVoxelData = nullptr;

	for (Brick* brick : bricks) {
		delete brick;
	}
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
