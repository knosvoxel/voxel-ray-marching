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

    modelData.resize(voxScene->num_instances);

    uint32 totalVoxelCount = 0;
	float64 rotationDurationTotal = 0;

    modelArraySize = voxScene->num_instances;
    cam_compute.setInt("model_array_size", modelArraySize);

    for (size_t i = 0; i < voxScene->num_instances; i++)
    {
		Timer local;
        const ogt_vox_instance* currInstance = &voxScene->instances[i];
        //if (currInstance->hidden == true || voxScene->layers[currInstance->layer_index].hidden == true) continue;

        const ogt_vox_model* currModel = voxScene->models[currInstance->model_index];

		ogt_vox_transform transform = ogt_vox_sample_instance_transform(currInstance, 0, voxScene);
		vec4 instanceOffset = vec4(transform.m30, transform.m31, transform.m32, 0);

		ivec3 rotatedModelSize;

		local.start();
        uint8* currModelVoxelsRotated = createRotatedModelCPU(voxScene, i, rotatedModelSize);
		rotationDurationTotal += local.elapsedMilliseconds();

        // voxel model data
        InstanceData currModelData;
		currModelData.bit_offset = totalVoxelCount; // in loop current total count is equal to current offset
		currModelData.position_offset = instanceOffset;
		currModelData.size = rotatedModelSize;
        modelData[i] = currModelData;

        // voxel uint8_t data
		const ivec3 currModelSize = ivec3(currModel->size_x, currModel->size_y, currModel->size_z);
		uint32_t currVoxelCount = currModelSize.x * currModelSize.y * currModelSize.z;

        voxelData.insert(voxelData.end(), currModelVoxelsRotated, currModelVoxelsRotated + currVoxelCount);

		instances.emplace_back();
		instances.back().modelSize = currModelSize;
		instances.back().voxelData = currModelVoxelsRotated;

		totalVoxelCount += currVoxelCount;
    }

	std::cout << " Rotation duration total: " << rotationDurationTotal << "ms" << std::endl;

    glCreateBuffers(1, &voxelDataBuffer);
    glNamedBufferStorage(voxelDataBuffer, sizeof(uint8_t) * totalVoxelCount, voxelData.data(), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, voxelDataBuffer);

    glCreateBuffers(1, &modelDataBuffer);
    glNamedBufferStorage(modelDataBuffer, sizeof(InstanceData) * modelData.size(), modelData.data(), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, modelDataBuffer);

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
	glDeleteBuffers(1, &modelDataBuffer);
	glDeleteBuffers(1, &voxelDataBuffer);
	glDeleteTextures(1, &palette);

	for (int i = 0; i < instances.size(); i++)
	{
		free(instances[i].voxelData);
		instances[i].voxelData = nullptr;
	}
}

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
