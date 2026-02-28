#include "vox_scene.h"

static inline glm::mat4 compute_transform_mat(const glm::mat4 transform, const glm::vec3& pivot) {
    static const glm::mat4 shift_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
    const glm::mat4 pivot_matrix = glm::translate(glm::mat4(1.0f), -pivot);
    const glm::mat4 combined_matrix = transform * shift_matrix * pivot_matrix;
    return combined_matrix;
}

static inline glm::vec4 instance_pivot(const ogt_vox_model* model) {
    return floor(glm::vec4(model->size_x / 2, model->size_y / 2, model->size_z / 2, 0.0f));
}

static glm::mat4 ogt_transform_to_glm(const ogt_vox_scene* scene, const ogt_vox_instance& instance, const ogt_vox_model* model)
{
    ogt_vox_transform t = ogt_vox_sample_instance_transform(&instance, 0, scene);
    const glm::vec4 col0(t.m00, t.m01, t.m02, t.m03);
    const glm::vec4 col1(t.m10, t.m11, t.m12, t.m13);
    const glm::vec4 col2(t.m20, t.m21, t.m22, t.m23);
    const glm::vec4 col3(t.m30, t.m31, t.m32, t.m33);
    const glm::vec3& pivot = instance_pivot(model);
    return compute_transform_mat(glm::mat4(col0, col1, col2, col3), pivot);
}

void VoxScene::load(const char* path, ComputeShader& cam_compute)
{
    applyRotationsCompute = ComputeShader("../shaders/apply_rotations.comp");

    const ogt_vox_scene* voxScene = load_vox_scene(path);

    modelData.resize(voxScene->num_instances);

    uint32_t total_voxel_index_count = 0;

    modelArraySize = voxScene->num_instances;
    cam_compute.setInt("model_array_size", modelArraySize);

    for (size_t i = 0; i < voxScene->num_instances; i++)
    {
        const ogt_vox_instance* currInstance = &voxScene->instances[i];
        //if (currInstance->hidden == true || voxScene->layers[currInstance->layer_index].hidden == true) continue;

        const ogt_vox_model* currModel = voxScene->models[currInstance->model_index];

		ogt_vox_transform transform = ogt_vox_sample_instance_transform(currInstance, 0, voxScene);
		vec4 instanceOffset = vec4(transform.m30, transform.m31, transform.m32, 0);

		ivec3 rotatedModelSize;
		float64 rotationDuration = 0.0;

        ogt_vox_model currModelRotated = createRotatedModel(voxScene, i, applyRotationsCompute, rotatedModelSize, rotationDuration);

        // voxel model aata
        InstanceData currModelData;
		currModelData.bit_offset = total_voxel_index_count; // in loop current total count is equal to current offset
		currModelData.position_offset = instanceOffset;
		currModelData.size = rotatedModelSize;
        modelData[i] = currModelData;

        // voxel uint8_t data
        const uint8_t* currModelVoxels = currModelRotated.voxel_data;
		const ivec3 currModelSize = ivec3(currModel->size_x, currModel->size_y, currModel->size_z);
		uint32_t currVoxelCount = currModelSize.x * currModelSize.y * currModelSize.z;

        voxelData.insert(voxelData.end(), currModelVoxels, currModelVoxels + currVoxelCount);

        total_voxel_index_count += currVoxelCount;
    }

    glCreateBuffers(1, &voxelDataBuffer);
    glNamedBufferStorage(voxelDataBuffer, sizeof(uint8_t) * total_voxel_index_count, voxelData.data(), GL_DYNAMIC_STORAGE_BIT);
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
}

ogt_vox_model VoxScene::createRotatedModel(const ogt_vox_scene* scene, uint32 instanceIdx, ComputeShader& compute, ivec3& rotatedModelSize, float64& dispatchDuration)
{
	const ogt_vox_instance& instance = scene->instances[instanceIdx];
	const ogt_vox_model* model = scene->models[instance.model_index];
	mat4& transformMat = ogt_transform_to_glm(scene, instance, model);

	vec3 corners[8] = {
		{0, 0, 0},
		{model->size_x - 1, 0, 0},
		{0, model->size_y - 1, 0},
		{0, 0, model->size_z - 1},
		{model->size_x - 1, model->size_y - 1, 0},
		{model->size_x - 1, 0, model->size_z - 1},
		{0, model->size_y - 1, model->size_z - 1},
		{model->size_x - 1, model->size_y - 1, model->size_z - 1},
	};

	// transform each corner of bouding box individually
	vec3 minBounds(FLT_MAX);
	vec3 maxBounds(-FLT_MAX);

	for (int i = 0; i < 8; ++i) {
		vec4 transformedCorner = transformMat * vec4(corners[i], 1.0f);
		vec3 flooredCorner = floor(vec3(transformedCorner));
		minBounds = min(minBounds, flooredCorner);
		maxBounds = max(maxBounds, flooredCorner);
	}

	rotatedModelSize = ivec3(maxBounds - minBounds) + ivec3(1); // +1 since voxel grids are inclusive

	// apply_rotations_compute
	const uint8* voxelData = model->voxel_data;

	uint32 instanceTempSSBO, rotatedModelSSBO;
	glCreateBuffers(1, &instanceTempSSBO);
	glCreateBuffers(1, &rotatedModelSSBO);

	glNamedBufferStorage(instanceTempSSBO, sizeof(uint8_t) * model->size_x * model->size_y * model->size_z, voxelData, GL_DYNAMIC_STORAGE_BIT);
	glNamedBufferStorage(rotatedModelSSBO, sizeof(uint8_t) * model->size_x * model->size_y * model->size_z, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT);
	glClearNamedBufferData(rotatedModelSSBO, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr); // all values are initially 0. 0 = empty voxel

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceTempSSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rotatedModelSSBO);

	RotationData rotationData{};
	rotationData.instanceSize = vec4(model->size_x, model->size_y, model->size_z, 1.0);
	rotationData.rotatedSize = vec4(rotatedModelSize, 1.0);
	rotationData.minBounds = vec4(minBounds, 1.0);
	rotationData.transform = transformMat;

	uint32 rotationDataUBO;
	glCreateBuffers(1, &rotationDataUBO);

	glNamedBufferStorage(rotationDataUBO, sizeof(RotationData), &rotationData, GL_DYNAMIC_STORAGE_BIT);

	glBindBufferBase(GL_UNIFORM_BUFFER, 2, rotationDataUBO);

	compute.use();

	//uint32 rotationQuery;
	//glGenQueries(1, &rotationQuery);
	//glBeginQuery(GL_TIME_ELAPSED, rotationQuery);

	uint32 dispatchSizeX = (model->size_x + 15) / 16;
	uint32 dispatchSizeY = (model->size_y + 15) / 16;

	// apply_rotations_compute
	glDispatchCompute(dispatchSizeX, dispatchSizeY, model->size_z);

	//glEndQuery(GL_TIME_ELAPSED);

	glMemoryBarrier(
		GL_SHADER_STORAGE_BARRIER_BIT
	);

	//int32 available = 0;
	//while (!available) {
	//	glGetQueryObjectiv(rotationQuery, GL_QUERY_RESULT_AVAILABLE, &available);
	//}

	//uint64 elapsedGPU;
	//glGetQueryObjectui64v(rotationQuery, GL_QUERY_RESULT, &elapsedGPU);
	//// dispatch time in us
	//dispatchDuration = elapsedGPU / 1000;

	//glDeleteQueries(1, &rotationQuery);
	ogt_vox_model rotatedModel{
	rotatedModel.size_x = rotationData.rotatedSize.x,
	rotatedModel.size_y = rotationData.rotatedSize.y,
	rotatedModel.size_z = rotationData.rotatedSize.z,
	rotatedModel.voxel_hash = 0
	};

	 // read back model data
	void* ptr = glMapNamedBuffer(rotatedModelSSBO, GL_READ_ONLY);
	if (ptr) {
		const uint8_t* model_data = (const uint8_t*)ptr;
		rotatedModel.voxel_data = model_data;
		glUnmapNamedBuffer(rotatedModelSSBO);
	}
	else {
		uint8_t null = 0;
		const uint8_t* null_ptr = &null;
		rotatedModel.voxel_data = null_ptr;
	}

	glDeleteBuffers(1, &instanceTempSSBO);
	glDeleteBuffers(1, &rotationDataUBO);

	return rotatedModel;
}
