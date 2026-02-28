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
    apply_rotations_compute = ComputeShader("../shaders/apply_rotations.comp");

    const ogt_vox_scene* scene_ptr = load_vox_scene(path);

    model_data.resize(scene_ptr->num_instances);

    uint32_t total_voxel_index_count = 0;

    model_array_size = scene_ptr->num_instances;
    cam_compute.setInt("model_array_size", model_array_size);

    for (size_t i = 0; i < scene_ptr->num_instances; i++)
    {
        const ogt_vox_instance* curr_instance_ptr = &scene_ptr->instances[i];
        if (curr_instance_ptr->hidden == true || scene_ptr->layers[curr_instance_ptr->layer_index].hidden == true) continue;

        const ogt_vox_model* curr_model_ptr = scene_ptr->models[curr_instance_ptr->model_index];

        const glm::ivec3 curr_model_size = glm::ivec3(curr_model_ptr->size_x, curr_model_ptr->size_y, curr_model_ptr->size_z);

        ogt_vox_transform curr_instance_transform = ogt_vox_sample_instance_transform(curr_instance_ptr, 0, scene_ptr);
        glm::vec3 curr_instance_offset(curr_instance_transform.m30, curr_instance_transform.m31, curr_instance_transform.m32);

        RotationData curr_rotation_data{};
        ogt_vox_model curr_model_rotated = apply_rotations(scene_ptr, curr_rotation_data, i, apply_rotations_compute);

        // voxel model aata
        VoxelModel curr_model_data;
        curr_model_data.bit_offset = total_voxel_index_count; // in loop current total count is equal to current offset
        curr_model_data.position_offset = curr_instance_offset;
        curr_model_data.size = glm::ivec3(curr_rotation_data.rotated_size);
        model_data[i] = curr_model_data;

        // voxel uint8_t data
        const uint8_t* curr_voxels_ptr = curr_model_rotated.voxel_data;
        uint32_t curr_voxel_count = curr_model_size.x * curr_model_size.y * curr_model_size.z;

        voxel_data.insert(voxel_data.end(), curr_voxels_ptr, curr_voxels_ptr + curr_voxel_count);

        total_voxel_index_count += curr_voxel_count;
    }

    glCreateBuffers(1, &voxel_data_buffer);
    glNamedBufferStorage(voxel_data_buffer, sizeof(uint8_t) * total_voxel_index_count, voxel_data.data(), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, voxel_data_buffer);

    glCreateBuffers(1, &model_data_buffer);
    glNamedBufferStorage(model_data_buffer, sizeof(VoxelModel) * model_data.size(), model_data.data(), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, model_data_buffer);

    // load palette into texture
    ogt_vox_palette ogt_palette = scene_ptr->palette;

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

    ogt_vox_destroy_scene(scene_ptr);
}

ogt_vox_model VoxScene::apply_rotations(const ogt_vox_scene* scene, RotationData& rotation_data, uint32_t instance_idx, ComputeShader& compute)
{
    const ogt_vox_instance& instance = scene->instances[instance_idx];
    const ogt_vox_model* model = scene->models[instance.model_index];
    glm::mat4& transform_mat = ogt_transform_to_glm(scene, instance, model);

    glm::vec3 corners[8] = {
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
    glm::vec3 min_bounds(FLT_MAX);
    glm::vec3 max_bounds(-FLT_MAX);

    for (int i = 0; i < 8; ++i) {
        glm::vec4 transformed_corner = transform_mat * glm::vec4(corners[i], 1.0f);
        glm::vec3 floored_corner = glm::floor(glm::vec3(transformed_corner));
        min_bounds = glm::min(min_bounds, floored_corner);
        max_bounds = glm::max(max_bounds, floored_corner);
    }

    glm::ivec3 rotated_instance_size = glm::ivec3(max_bounds - min_bounds) + glm::ivec3(1); // +1 since voxel grids are inclusive

    // apply_rotations_compute
    const uint8_t* voxel_data = model->voxel_data;

    glCreateBuffers(1, &instance_temp_ssbo);
    glCreateBuffers(1, &rotated_temp_ssbo);

    glNamedBufferStorage(instance_temp_ssbo, sizeof(uint8_t) * model->size_x * model->size_y * model->size_z, voxel_data, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferStorage(rotated_temp_ssbo, sizeof(uint8_t) * model->size_x * model->size_y * model->size_z, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT);
    glClearNamedBufferData(rotated_temp_ssbo, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr); // all values are initially 0. 0 = empty voxel

    rotation_data.instance_size = glm::vec4(model->size_x, model->size_y, model->size_z, 1.0);
    rotation_data.rotated_size = glm::vec4(rotated_instance_size, 1.0);
    rotation_data.min_bounds = glm::vec4(min_bounds, 1.0);
    rotation_data.transform = transform_mat;

    glCreateBuffers(1, &rotation_data_temp_buffer);

    glNamedBufferStorage(rotation_data_temp_buffer, sizeof(RotationData), &rotation_data, GL_DYNAMIC_STORAGE_BIT);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instance_temp_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rotated_temp_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, rotation_data_temp_buffer);

    compute.use();

    uint32 dispatchSizeX = (model->size_x + 15) / 16;
    uint32 dispatchSizeY = (model->size_y + 15) / 16;

    // apply_rotations_compute
    glDispatchCompute(model->size_x, model->size_y, model->size_z);

    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT
    );

    ogt_vox_model rotated_model{
        rotated_model.size_x = rotated_instance_size.x,
        rotated_model.size_y = rotated_instance_size.y,
        rotated_model.size_z = rotated_instance_size.z,
        rotated_model.voxel_hash = 0
    };

    // read back model data
    void* ptr = glMapNamedBuffer(rotated_temp_ssbo, GL_READ_ONLY);
    if (ptr) {
        const uint8_t* model_data = (const uint8_t*)ptr;
        rotated_model.voxel_data = model_data;
        glUnmapNamedBuffer(rotated_temp_ssbo);
    }
    else {
        uint8_t null = 0;
        const uint8_t* null_ptr = &null;
        rotated_model.voxel_data = null_ptr;
    }

    glDeleteBuffers(1, &instance_temp_ssbo);
    glDeleteBuffers(1, &rotated_temp_ssbo);
    glDeleteBuffers(1, &rotation_data_temp_buffer);

    return rotated_model;
}
