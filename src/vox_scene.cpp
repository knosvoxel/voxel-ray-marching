#include "vox_scene.h"

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

void VoxScene::load(const char* path, ComputeShader& cam_compute)
{
    const ogt_vox_scene* voxScene = load_vox_scene(path);

	float64 rotationDurationTotal = 0;

	Timer timer;
	timer.start();

	//std::cout << voxScene->num_instances << std::endl;
    for (int32 i = 0; i < voxScene->num_instances; i++)
    {
		Timer local;
        const ogt_vox_instance* currInstance = &voxScene->instances[i];
        //if (currInstance->hidden == true || voxScene->layers[currInstance->layer_index].hidden == true) continue;

        const ogt_vox_model* currModel = voxScene->models[currInstance->model_index];

		ogt_vox_transform transform = ogt_vox_sample_instance_transform(currInstance, 0, voxScene);
		vec3 instanceOffset = vec3(transform.m31, transform.m32, transform.m30);

		VoxInstance newInstance{};

		ivec3 rotatedModelSize;
		local.start();
        newInstance.rawVoxelData = createRotatedModelCPU(voxScene, i, rotatedModelSize);
		rotationDurationTotal += local.elapsedMilliseconds();

		newInstance.lowerBounds = instanceOffset - floor(vec3(rotatedModelSize.y, rotatedModelSize.z, rotatedModelSize.x) / 2.0f);
		newInstance.upperBounds = newInstance.lowerBounds + vec3(currModel->size_y, currModel->size_z, currModel->size_x);
		newInstance.size = newInstance.upperBounds - newInstance.lowerBounds;

		uint32 biggestLevelSize = getClosestInstanceLevelSize(newInstance.size);
		std::cout << biggestLevelSize << std::endl;
		newInstance.biggestLevelSize = (int)log2(float(biggestLevelSize));

		newInstance.nodes.resize(1);
		TreeNode root = generateInstanceTree(newInstance, biggestLevelSize, ivec3(0));
		newInstance.nodes[0] = root;

		//std::cout << newInstance.leafs.size() << std::endl;

		//std::cout << newInstance.getTotalSizeInByte() << std::endl;

		instances.push_back(newInstance);
    }

	std::cout << " Rotation duration total: " << rotationDurationTotal << "ms" << std::endl;
	std::cout << "Total instance load time: " << timer.elapsedSeconds() << "s" << std::endl;

	glCreateBuffers(1, &treeNodesBuffer);
	glNamedBufferStorage(treeNodesBuffer, sizeof(TreeNode) * instances[0].nodes.size(), instances[0].nodes.data(), GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, treeNodesBuffer);

	glCreateBuffers(1, &leafsBuffer);
	glNamedBufferStorage(leafsBuffer, sizeof(uint8) * instances[0].leafs.size(), instances[0].leafs.data(), GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, leafsBuffer);

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

    ogt_vox_destroy_scene(voxScene);
}

void VoxScene::cleanup()
{
	glDeleteBuffers(1, &treeNodesBuffer);
	glDeleteBuffers(1, &leafsBuffer);
	glDeleteTextures(1, &palette);

	for (int i = 0; i < instances.size(); i++)
	{
		free(instances[i].rawVoxelData);
		instances[i].rawVoxelData = nullptr;
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

std::vector<uint32> instanceTreeLevelSizes{ 4, 16, 64, 256 };
uint32 VoxScene::getClosestInstanceLevelSize(vec3 modelSize)
{
	float32 maxDimFloat = max(modelSize.x, modelSize.y);
	maxDimFloat = max(maxDimFloat, modelSize.z);

	uint32 maxDim = static_cast<uint32>(maxDimFloat);
	uint32 correctLevel = 0;

	for (uint32 level = 0; level < instanceTreeLevelSizes.size(); ++level)
	{
		uint32 levelSize = instanceTreeLevelSizes[level];
		if (maxDim <= levelSize)
			return levelSize;
	}

	assert(false && "Model size exceed expected maximum size.");
	return -1;
}

uint32 VoxScene::getClosestRootLevelSize(vec3 modelSize)
{
	return uint32();
}

TreeNode VoxScene::generateInstanceTree(VoxInstance& instance, int32 levelSize, ivec3 pos)
{
	TreeNode node{};

	// Create leaf
	if (levelSize == 4) {
		bool anyVoxel = false;
		uint64 currentMask = 0;
		std::vector<uint8> tempLeafData;

		for (int32 i = 0; i < 64; i++) {
			ivec3 offset = ivec3(i % 4, i / 16, (i / 4) % 4);
			ivec3 globalPos = pos + offset;

			uint8 colorIdx = 0;
			if (globalPos.x < instance.size.x && globalPos.y < instance.size.y && globalPos.z < instance.size.z) {
				uint32 srcIdx = globalPos.x + (globalPos.y * instance.size.x) + (globalPos.z * instance.size.x * instance.size.y);
				colorIdx = instance.rawVoxelData[srcIdx];
			}

			if (colorIdx != 0) {
				currentMask |= (1ull << i);
				anyVoxel = true;
				tempLeafData.push_back(colorIdx);
			}
		}

		if (anyVoxel) {
			node.setIsLeaf(true);
			node.setChildMask(currentMask);
			node.setChildPtr(instance.leafs.size());
			instance.leafs.insert(instance.leafs.end(), tempLeafData.begin(), tempLeafData.end());
			return node;
		}
		else {
			node.setChildMask(0);
			return node;
		}
	}

	levelSize /= 4;

	std::vector<TreeNode> children;
	children.reserve(64);

	for (int32 i = 0; i < 64; i++) {
		ivec3 childPos = ivec3(i % 4, i / 16, (i / 4) % 4);
		TreeNode child = generateInstanceTree(instance, levelSize, pos + (childPos * levelSize));

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
