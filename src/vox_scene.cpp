#include "vox_scene.h"

// half of 2048 / 32
static constexpr int32 SECTOR_BIAS = 64;

static inline glm::mat4 computeTransformMat(const glm::mat4 transform, const glm::vec3& pivot) {
    static const glm::mat4 shift_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
    const glm::mat4 pivot_matrix = glm::translate(glm::mat4(1.0f), -pivot);
    const glm::mat4 combined_matrix = transform * shift_matrix * pivot_matrix;
    return combined_matrix;
}

static inline glm::vec4 instancePivot(const ogt_vox_model* model) {
    return floor(glm::vec4(model->size_x / 2, model->size_y / 2, model->size_z / 2, 0.0f));
}

static glm::mat4 ogtTransformToGLM(const ogt_vox_scene* scene, const ogt_vox_instance& instance, const ogt_vox_model* model)
{
    ogt_vox_transform t = ogt_vox_sample_instance_transform(&instance, 0, scene);
    const glm::vec4 col0(t.m00, t.m01, t.m02, t.m03);
    const glm::vec4 col1(t.m10, t.m11, t.m12, t.m13);
    const glm::vec4 col2(t.m20, t.m21, t.m22, t.m23);
    const glm::vec4 col3(t.m30, t.m31, t.m32, t.m33);
    const glm::vec3& pivot = instancePivot(model);
    return computeTransformMat(glm::mat4(col0, col1, col2, col3), pivot);
}

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
static TreeNode generateTreeInstance(VoxScene& scene, int32 levelSize, ivec3 pos = {})
{
	TreeNode node{};

	// check if any sector in region at position of current levelSize, otherwise fully skip subtree
	if (levelSize >= 4) {
		// positions in sector space
		ivec3 sectorMin = pos / ivec3(32);
		ivec3 sectorMax = (pos + ivec3((1 << levelSize) - 1)) / ivec3(32);

		if (!scene.anySectorExits(sectorMin, sectorMax)) return node;
	}

	// Create leaf
	// 4 x 4 x 4 voxel
	if (levelSize == 2) {
		const Brick* brick = scene.getBrick(pos);
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
		node.setChildPtr(scene.leafs.size());
		scene.leafs.insert(scene.leafs.end(), bricklet, bricklet + std::popcount(mask));
		return node;
	}

	levelSize -= 2;

	std::vector<TreeNode> children;
	children.reserve(64);

	for (int32 i = 0; i < 64; i++) {
		ivec3 childPos = i >> ivec3(0, 4, 2) & 3; // position xyz range: 0 - 3
		TreeNode child = generateTreeInstance(scene, levelSize, pos + (childPos << levelSize));

		if (child.getChildMask() != 0) {
			uint64 mask = node.getChildMask();
			node.setChildMask(mask |= 1ull << i); // 1ull = 1 as 64 bit value
			children.push_back(child);
		}
	}

	node.setChildPtr(scene.nodes.size());
	scene.nodes.insert(scene.nodes.end(), children.begin(), children.end());

	return node;
}

void VoxScene::load(const char* path, ComputeShader& cam_compute)
{
	Timer timer;
	timer.start();

    const ogt_vox_scene* voxScene = load_vox_scene(path);
	if (!voxScene)
	{
		std::cerr << "Failed to load vox file at path: " << path << std::endl;
		exit(-1);
	}
	std::cout << "Scene load done: " << timer.elapsedSeconds() << " s" << std::endl;

	// load palette into texture
	ogt_vox_palette ogt_palette = voxScene->palette;

	// texture generation with DSA
	glCreateTextures(GL_TEXTURE_2D, 1, &palette);

	glTextureParameteri(palette, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(palette, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(palette, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(palette, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTextureStorage2D(palette, 1, GL_RGBA8, 256, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(palette, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, ogt_palette.color);
	glBindTextureUnit(4, palette);

	numInstances = voxScene->num_instances;
	instances.reserve(numInstances);

	std::cout << "Scene load & palette overhead total: " << timer.elapsedSeconds() << " s\n" << std::endl;

	std::cout << numInstances << " instance(s)\n" << std::endl;

	// DEBUG INFORMATION //
	uint64 totalSizeX = 0;
	uint64 totalSizeY = 0;
	uint64 totalSizeZ = 0;

	// TODO: per instance metrics
	//float64 instanceTreeGenerationTotal = 0.0;
	//float64 instanceTreeGenerationMin = DBL_MAX;
	//float64 instanceTreeGenerationMax = 0.0;
	
	//float64 worldTreeGeneration = 0.0;

	sizeInSectors = ivec3(2048 / 32); // 64
	sizeInBricks = ivec3(2048 / 8); // 256

	float64 rotationDurationTotal = 0;

    for (int32 i = 0; i < numInstances; i++)
    {
		Timer local;
        const ogt_vox_instance* currInstance = &voxScene->instances[i];
        //if (currInstance->hidden == true || voxScene->layers[currInstance->layer_index].hidden == true) continue;

        const ogt_vox_model* currModel = voxScene->models[currInstance->model_index];

		ogt_vox_transform transform = ogt_vox_sample_instance_transform(currInstance, 0, voxScene);
		vec3 instanceOffset = vec3(transform.m31, transform.m32, transform.m30);

		ivec3 modelSize = ivec3(currModel->size_x, currModel->size_y, currModel->size_z);
		ivec3 rotatedModelSize;
		local.start();
		uint8* rawVoxelData = createRotatedModelCPU(voxScene, i, rotatedModelSize);
		local.stop();
		rotationDurationTotal += local.elapsedMilliseconds();
		local.start();
		VoxInstance newInstance{modelSize, rotatedModelSize, instanceOffset, rawVoxelData, measurements};
		newInstance.posInArray = i;
		instances.push_back(newInstance);

		generateSectors(newInstance);

		totalSizeX += currModel->size_x;
		totalSizeY += currModel->size_y;
		totalSizeZ += currModel->size_z;
    }

	nodes.resize(1);
	TreeNode root = generateTreeInstance(*this, biggestLevelSize, ivec3(0));
	nodes[0] = root;

	std::cout << "Average instance size: " << totalSizeX / numInstances << " " << totalSizeY / numInstances << " " << totalSizeZ / numInstances << "\n" << std::endl;

	std::cout << "---------- instance creation -------" << std::endl;
	std::cout << "Total instance load time: " << timer.elapsedSeconds() << "s (Average: " << timer.elapsedMilliseconds() / numInstances << "ms)" << std::endl;
	std::cout << " Rotation duration total: " << rotationDurationTotal << "ms\n" << std::endl;
	std::cout << " Preprocessing: " << measurements.preprocessingDuration << "ms (Average: " << measurements.preprocessingDuration / numInstances << "ms)" << std::endl;
	std::cout << " Sector generation: " << measurements.sectorGenerationDuration << "ms (Average: " << measurements.sectorGenerationDuration / numInstances << "ms)" << std::endl;
	std::cout << " Tree generation: " << measurements.treeGenerationDuration << "ms (Average: " << measurements.treeGenerationDuration / numInstances << "ms)" << std::endl;

	std::cout << "------------------------------------" << std::endl;

	glCreateBuffers(1, &treeNodesBuffer);
	glNamedBufferStorage(treeNodesBuffer, sizeof(TreeNode) * nodes.size(), nodes.data(), GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, treeNodesBuffer);

	glCreateBuffers(1, &leafsBuffer);
	glNamedBufferStorage(leafsBuffer, sizeof(uint8) * leafs.size(), leafs.data(), GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, leafsBuffer);

    ogt_vox_destroy_scene(voxScene);

	timer.stop();
	std::cout << "Scene creation total: " << timer.elapsedSeconds() << " s" << std::endl;
}

void VoxScene::cleanup()
{
	glDeleteBuffers(1, &treeNodesBuffer);
	glDeleteBuffers(1, &leafsBuffer);
	glDeleteTextures(1, &palette);

	//for (Sector* sector : sectors.)
	//{

	//}
}

const int32 VoxScene::getSectorIndex(int32 sx, int32 sy, int32 sz)
{
	return sx + sz * sizeInSectors.x + sy * sizeInSectors.x * sizeInSectors.z;
}

const int32 VoxScene::getLocalBrickIndex(int32 lx, int32 ly, int32 lz)
{
	return lx + lz * 4 + ly * 16;
}

static uint64 packSectorKey(int32 sx, int32 sy, int32 sz) {
	return ((uint64)(sx + SECTOR_BIAS) | (uint64)(sy + SECTOR_BIAS) << 21 | (uint64)(sz + SECTOR_BIAS) << 42);
}

const Brick* VoxScene::getBrick(ivec3 voxelPos)
{
	ivec3 brickPos = voxelPos / ivec3(8);
	ivec3 sectorPos = brickPos / ivec3(4);
	ivec3 localBrickPos = brickPos % ivec3(4);

	uint64 key = packSectorKey(sectorPos.x, sectorPos.y, sectorPos.z);
	auto sector = sectors.find(key);
	if (sector == sectors.end() || sector->second == nullptr) return nullptr;

	return sector->second->bricks[getLocalBrickIndex(localBrickPos.x, localBrickPos.y, localBrickPos.z)];

	//// get brick of voxel within sector
	//// local between 0 and 3 in all three directions
	//// 
	//// sector has 32 x 32 x 32 voxels, brick has 8 x 8 x 8
	//// first get offset within sector, then fetch which brick is at that local position in the sector
	//return sector->bricks[getLocalBrickIndex(localBrickPos.x, localBrickPos.y, localBrickPos.z)];
}

const bool VoxScene::anySectorExits(ivec3 sectorMin, ivec3 sectorMax)
{
	for (int32 sy = sectorMin.y; sy <= sectorMax.y; sy++)
	{
		for (int32 sz = sectorMin.z; sz <= sectorMax.z; sz++)
		{
			for (int32 sx = sectorMin.x; sx <= sectorMax.x; sx++)
			{
				uint64 key = packSectorKey(sx, sy, sz);
				auto sector = sectors.find(key);
				if (sector != sectors.end() && sector->second != nullptr)
					return true;
			}
		}
	}
	return false;
}

void VoxScene::generateSectors(VoxInstance& instance)
{
	const ivec3 brickWorldOrigin = ivec3(floor(instance.lowerBounds) + vec3(2048)) / 8;

	int32 totalBricks = instance.sizeInBricks.x * instance.sizeInBricks.y * instance.sizeInBricks.z;
	std::vector<std::unique_ptr<Brick>> brickPool(totalBricks);

#pragma omp parallel for collapse(3) schedule(static)
	for (int32 by = 0; by < instance.sizeInBricks.y; by++)
	{
		for (int32 bz = 0; bz < instance.sizeInBricks.z; bz++)
		{
			for (int32 bx = 0; bx < instance.sizeInBricks.x; bx++)
			{
				Brick local{};
				bool anySet = false;

				for (int32 ly = 0; ly < 8; ly++)
				{
					for (int32 lz = 0; lz < 8; lz++)
					{
						for (int32 lx = 0; lx < 8; lx++)
						{
							ivec3 localPos = ivec3(bx * 8 + lx, by * 8 + ly, bz * 8 + lz);
							if (localPos.x >= instance.size.x || localPos.y >= instance.size.y || localPos.z >= instance.size.z)
								continue;

							uint32 srcIdx = localPos.x + localPos.y * instance.size.x + localPos.z * instance.size.x * instance.size.y;
							uint8 colorIdx = instance.rawVoxelData[srcIdx];
							if (colorIdx != 0) {
								local.data[Brick::getIndex(lx, ly, lz)] = colorIdx;
								anySet = true;
							}
						}
					}
				}

				if (anySet) {
					int32 idx = instance.getPoolIndex(bx, by, bz);
					brickPool[idx] = std::make_unique<Brick>(local);
				}
			}
		}
	}

	// assign bricks to sectors
	for (int32 by = 0; by < instance.sizeInBricks.y; by++)
	{
		for (int32 bz = 0; bz < instance.sizeInBricks.z; bz++)
		{
			for (int32 bx = 0; bx < instance.sizeInBricks.x; bx++)
			{
				int32 poolIdx = instance.getPoolIndex(bx, by, bz);
				if (!brickPool[poolIdx]) continue;

				ivec3 brickWorldPos = brickWorldOrigin + ivec3(bx, by, bz);
				ivec3 sectorWorldPos = ivec3(floor(vec3(brickWorldPos) / 4.0f));
				ivec3 localBrickPos = brickWorldPos % 4;

				uint64 key = packSectorKey(sectorWorldPos.x, sectorWorldPos.y, sectorWorldPos.z);

				if (sectors.find(key) == sectors.end())
					sectors[key] = new Sector();

				int32 localIdx = getLocalBrickIndex(localBrickPos.x, localBrickPos.y, localBrickPos.z);
				sectors[key]->bricks[localIdx] = brickPool[poolIdx].release();
			}
		}
	}
}

// TODO: Swizzle coordinates
uint8* VoxScene::createRotatedModelCPU(const ogt_vox_scene* scene, uint32 instanceIdx, ivec3& rotatedModelSize)
{
	const ogt_vox_instance& instance = scene->instances[instanceIdx];
	const ogt_vox_model* model = scene->models[instance.model_index];
	mat4 transformMat = ogtTransformToGLM(scene, instance, model);

	vec3 corners[8] = {
		{0, 0, 0}, {model->size_x - 1, 0, 0},
		{0, model->size_y - 1, 0},
		{0, 0, model->size_z - 1},
		{model->size_x - 1, model->size_y - 1, 0},
		{model->size_x - 1, 0, model->size_z - 1},
		{0, model->size_y - 1, model->size_z - 1},
		{model->size_x - 1, model->size_y - 1, model->size_z - 1}
	};

	vec3 minBounds(FLT_MAX);
	vec3 maxBounds(-FLT_MAX);

	for (int i = 0; i < 8; ++i) {
		vec4 transformedCorner = transformMat * vec4(corners[i], 1.0f);
		vec3 flooredCorner = floor(vec3(transformedCorner));
		minBounds = min(minBounds, flooredCorner);
		maxBounds = max(maxBounds, flooredCorner);
	}

	rotatedModelSize = ivec3(maxBounds - minBounds) + ivec3(1);
	rotatedModelSize = ivec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x); // swizzle size into correct coordinate space

	size_t numVoxels = (size_t)rotatedModelSize.x * rotatedModelSize.y * rotatedModelSize.z;
	uint8* outData = (uint8*)calloc(numVoxels, sizeof(uint8));

	uint32 sizeX = model->size_x;
	uint32 sizeY = model->size_y;
	uint32 sizeZ = model->size_z;

#pragma omp parallel for collapse(2) schedule(static)
	for (int32 z = 0; z < sizeZ; ++z) {
		for (int32 y = 0; y < sizeY; ++y) {
			for (int32 x = 0; x < sizeX; ++x) {

				uint32 srcIdx = x + (y * sizeX) + (z * sizeX * sizeY);
				uint8 colIdx = model->voxel_data[srcIdx];

				if (colIdx == 0) continue;

				// Apply transform
				vec4 rotatedPos = floor(transformMat * vec4((float32)x, (float32)y, (float32)z, 1.0f));
				ivec3 finalPos = ivec3(vec3(rotatedPos.x, rotatedPos.y, rotatedPos.z) - minBounds);
				finalPos = ivec3(finalPos.y, finalPos.z, finalPos.x);

				// Bounds check
				if (finalPos.x >= 0 && finalPos.x < rotatedModelSize.x &&
					finalPos.y >= 0 && finalPos.y < rotatedModelSize.y &&
					finalPos.z >= 0 && finalPos.z < rotatedModelSize.z)
				{
					uint32 dstIdx = finalPos.x + (finalPos.y * rotatedModelSize.x) + (finalPos.z * rotatedModelSize.x * rotatedModelSize.y);
					outData[dstIdx] = colIdx;
				}
			}
		}
	}

	auto endTime = std::chrono::high_resolution_clock::now();

	return outData;
}

//uint32 VoxScene::getClosestRootLevelSize(vec3 modelSize)
//{
//	return uint32();
//}
